#ifndef FILEZILLA_ENGINE_DIRECTORYLISTINGPARSER_HEADER
#define FILEZILLA_ENGINE_DIRECTORYLISTINGPARSER_HEADER

/* This class is responsible for parsing the directory listings returned by
 * the server.
 * Unfortunately, RFC959 did not specify the format of directory listings, so
 * each server uses its own format. In addition to that, in most cases the
 * listings were not designed to be machine-parsable, they were meant to be
 * human readable by users of that particular server.
 * By far the most common format is the one returned by the Unix "ls -l"
 * command. However, legacy systems are still in place, especially in big
 * companies. These often use very exotic listing styles.
 * Another problem are localized listings containing date strings. In some
 * cases these listings are ambiguous and cannot be distinguished.
 * Example for an ambiguous date: 04-05-06. All of the 6 permutations for
 * the location of year, month and day are valid dates.
 * Some servers send multiline listings where a single entry can span two
 * lines, this has to be detected as well, as far as possible.
 *
 * Some servers send MVS style listings which can consist of just the
 * filename without any additional data. In order to prevent problems, this
 * format is only parsed if the server is in fact recognized as MVS server.
 *
 * Please see tests/dirparsertest.cpp for a list of supported formats and the
 * expected parser result.
 *
 * If adding data to the parser, it first decomposes the raw data into lines,
 * which then are processed further. Each line gets consecutively tested for
 * different formats, starting with the most common Unix style format.
 * Lines not containing a recognized format (e.g. a part of a multiline
 * entry) are remembered and if the next line cannot be parsed either, they
 * get concatenated to be parsed again (and discarded if not recognized).
 */

#include "../include/directorylisting.h"
#include "../include/server.h"

#include <libfilezilla/buffer.hpp>

#include <limits>
#include <optional>
#include <string>
#include <vector>

class CLine;
class CToken;
class CControlSocket;

namespace listingEncoding
{
	enum type
	{
		unknown,
		normal,
		ebcdic
	};
}


class CToken final
{
protected:
	enum flags : unsigned char {
		numeric_left = 0x01,
		non_numeric_left = 0x02,
		numeric_right = 0x04,
		non_numeric_right = 0x08,
		numeric  = 0x10,
		non_numeric = 0x20
	};


public:
	CToken() = default;

	enum t_numberBase {
		decimal,
		hex
	};

	CToken(std::wstring_view data)
		: data_(data)
	{}

	CToken(wchar_t const* data, size_t len)
		: data_(data, len)
	{}

	wchar_t const* data() const {
		return data_.data();
	}

	size_t size() const {
		return data_.size();
	}

	explicit operator bool() const { return !data_.empty(); }

	wchar_t operator[](size_t i) const { return data_[i]; }

	std::wstring_view get_view() const { return data_; }

	bool IsNumeric(t_numberBase base = decimal);
	bool IsNumeric(size_t start, size_t len);
	bool IsLeftNumeric();
	bool IsRightNumeric();

	int Find(wchar_t const* chr, size_t start = 0) const;
	int Find(wchar_t chr, size_t start = 0) const;

	int64_t GetNumber(size_t start, int len);
	int64_t GetNumber(t_numberBase base = decimal);

	static int64_t GetNumber(std::wstring_view s, t_numberBase base = decimal, bool trailingDataIsError = false);

protected:
	int64_t m_number{std::numeric_limits<int64_t>::min()};

	std::wstring_view data_;
	unsigned char flags_{};
};

class CLine final
{
public:
	CLine() = default;

	explicit CLine(std::wstring_view const& line, size_t trailing_whitespace = std::wstring::npos);

	CLine(CLine&&) noexcept = default;
	CLine& operator=(CLine&&) noexcept = default;

	CToken GetToken(unsigned int n);
	CToken GetEndToken(unsigned int n, bool include_whitespace = false);

	size_t TrailingWhitespace() const { return trailing_whitespace_; }

protected:
	std::vector<CToken> m_Tokens;
	std::wstring_view line_;
	size_t m_parsePos{};
	size_t trailing_whitespace_{std::wstring::npos};
};


class FZC_PUBLIC_SYMBOL CDirectoryListingParser final
{
public:
	CDirectoryListingParser(CControlSocket* pControlSocket, const CServer& server, listingEncoding::type encoding = listingEncoding::unknown);
	~CDirectoryListingParser();

	CDirectoryListingParser(CDirectoryListingParser const&) = delete;
	CDirectoryListingParser& operator=(CDirectoryListingParser const&) = delete;

	CDirectoryListing Parse(const CServerPath &path);

	fz::buffer& GetInputBuffer() { return inbuf_; }
	bool ProcessAddedData();

	bool AddLine(std::wstring && line, std::wstring && name, fz::datetime const& time, std::optional<uint64_t> const& size, std::optional<int> flags);

	void Reset();

	// This is the auto-deduced offset for listing formats that don't mandate use of UTC
	void SetTimezoneOffset(fz::duration const& span) { m_timezoneOffset = span; }

	void SetServer(const CServer& server) { m_server = server; };

protected:
	std::wstring GetLine(bool breakAtEnd, bool& error);
	void TrimLeadingWhitespace();

	bool ParseData(bool partial);

	bool ParseLine(CLine &line, ServerType const serverType, bool concatenated, CDirentry const* override = nullptr);

	bool ParseAsUnix(CLine &line, CDirentry &entry, bool expect_date);
	bool ParseAsDos(CLine &line, CDirentry &entry);
	bool ParseAsEplf(CLine &line, CDirentry &entry);
	bool ParseAsVms(CLine &line, CDirentry &entry);
	bool ParseAsIbm(CLine &line, CDirentry &entry);
	bool ParseOther(CLine &line, CDirentry &entry);
	bool ParseAsWfFtp(CLine &line, CDirentry &entry);
	bool ParseAsIBM_MVS(CLine &line, CDirentry &entry);
	bool ParseAsIBM_MVS_PDS(CLine &line, CDirentry &entry);
	bool ParseAsIBM_MVS_PDS2(CLine &line, CDirentry &entry);
	bool ParseAsIBM_MVS_Migrated(CLine &line, CDirentry &entry);
	bool ParseAsIBM_MVS_Tape(CLine &line, CDirentry &entry);
	int ParseAsMlsd(CLine &line, CDirentry &entry);
	bool ParseAsOS9(CLine &line, CDirentry &entry);

	// Only call this if servertype set to ZVM since it conflicts
	// with other formats.
	bool ParseAsZVM(CLine &line, CDirentry &entry);

	// Only call this if servertype set to HPNONSTOP since it conflicts
	// with other formats.
	bool ParseAsHPNonstop(CLine &line, CDirentry &entry);

	// Date / time parsers
	bool ParseUnixDateTime(CLine &line, int &index, CDirentry &entry);
	bool ParseShortDate(CToken &token, CDirentry &entry, bool saneFieldOrder = false);
	bool ParseTime(CToken &token, CDirentry &entry);

	// Parse file sizes given like this: 123.4M
	bool ParseComplexFileSize(CToken& token, int64_t& size, int blocksize = -1);

	bool GetMonthFromName(std::wstring_view const& name, int &month);

	void DeduceEncoding();
	void ConvertEncoding();

	CControlSocket* m_pControlSocket;

	fz::buffer inbuf_;
	size_t parse_offset_{};
	size_t converted_{};

	std::vector<fz::shared_value<CDirentry>> entries_;

	std::wstring prevLine_;

	CServer m_server;

	bool m_fileListOnly{true};
	std::vector<std::wstring> m_fileList;

	bool m_maybeMultilineVms{};

	fz::duration m_timezoneOffset;

	listingEncoding::type m_listingEncoding;

	size_t limit_{size_t(-1)};
	bool truncated_{};
};

#endif
