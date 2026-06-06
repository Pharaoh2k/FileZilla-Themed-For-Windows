#ifndef FILEZILLA_ENGINE_SERVERPATH_HEADER
#define FILEZILLA_ENGINE_SERVERPATH_HEADER

#include "server.h"

#include <libfilezilla/optional.hpp>
#include <libfilezilla/shared.hpp>

#include <vector>

class FZC_PUBLIC_SYMBOL CServerPathData final
{
public:
	std::vector<std::wstring> m_segments;
	fz::sparse_optional<std::wstring> m_prefix;

	bool operator==(const CServerPathData& cmp) const;
};

class FZC_PUBLIC_SYMBOL CServerPath final
{
public:
	CServerPath();
	explicit CServerPath(std::wstring const& path, ServerType type = UNIX);
	CServerPath(CServerPath const& path, std::wstring subdir); // Ignores parent on absolute subdir
	CServerPath(CServerPath const& path) = default;
	CServerPath(CServerPath && path) noexcept = default;

	CServerPath& operator=(CServerPath const& op) = default;
	CServerPath& operator=(CServerPath && op) noexcept = default;

	explicit operator bool() const { return !empty(); }
	bool empty() const { return !m_data; }
	void clear();

	bool SetPath(std::wstring newPath);
	bool SetPath(std::wstring& newPath, bool isFile);
	bool SetSafePath(std::wstring const& path);

	// If ChangePath returns false, the object will be left
	// empty.
	bool ChangePath(std::wstring const& subdir);
	bool ChangePath(std::wstring &subdir, bool isFile);

	std::wstring GetPath() const;
	std::wstring GetSafePath() const;

	bool HasParent() const;
	CServerPath GetParent() const;
	CServerPath& MakeParent();
	std::wstring GetFirstSegment() const;
	std::wstring GetLastSegment() const;

	CServerPath GetCommonParent(CServerPath const& path) const;

	bool SetType(ServerType type);
	ServerType GetType() const;

	bool IsSubdirOf(CServerPath const& path, bool cmpNoCase, bool allowEqual = false) const;
	bool IsParentOf(CServerPath const& path, bool cmpNoCase, bool allowEqual = false) const;

	bool operator==(CServerPath const& op) const;
	bool operator!=(CServerPath const& op) const;
	bool operator<(CServerPath const& op) const;

	bool equal_nocase(CServerPath const& op) const; // Faster than compare_nocase==0
	int compare_nocase(CServerPath const& op) const;
	int compare_case(CServerPath const& op) const;

	// omitPath is just a hint. For example dataset member names on MVS servers
	// always use absolute filenames including the full path
	std::wstring FormatFilename(std::wstring const& filename, bool omitPath = false) const;

	// Returns identity on all but VMS. On VMS it escapes dots
	std::wstring FormatSubdir(std::wstring const& subdir) const;

	bool AddSegment(std::wstring const& segment);

	size_t SegmentCount() const;

	static CServerPath GetChanged(CServerPath const& oldPath, CServerPath const& newPath, std::wstring const& newSubdir);

	static ServerType GuessType(std::wstring_view const& path, bool isFile);
private:
	bool FZC_PRIVATE_SYMBOL IsSeparator(wchar_t c) const;

	bool FZC_PRIVATE_SYMBOL DoSetSafePath(std::wstring const& path);
	bool FZC_PRIVATE_SYMBOL DoChangePath(std::wstring &subdir, bool isFile);

	typedef std::vector<std::wstring> tSegmentList;
	typedef tSegmentList::iterator tSegmentIter;
	typedef tSegmentList::const_iterator tConstSegmentIter;

	bool FZC_PRIVATE_SYMBOL Segmentize(std::wstring const& str, tSegmentList& segments);
	bool FZC_PRIVATE_SYMBOL SegmentizeAddSegment(std::wstring & segment, tSegmentList& segments, bool& append);
	bool FZC_PRIVATE_SYMBOL ExtractFile(std::wstring& dir, std::wstring& file);

	fz::shared_optional<CServerPathData> m_data;
	ServerType m_type;
};

#endif
