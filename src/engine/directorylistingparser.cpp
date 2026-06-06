#include "filezilla.h"
#include "directorylistingparser.h"
#include "controlsocket.h"
#include "../include/engine_options.h"

#include <libfilezilla/format.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <vector>
#include <limits>

#include <assert.h>

using namespace std::literals;

//#define LISTDEBUG_MVS
//#define LISTDEBUG
#ifdef LISTDEBUG
static std::string_view const data[] = {
	""sv // Has to be terminated with empty string
};

#endif

namespace {
struct ObjectCache
{
	fz::shared_value<std::wstring> get(std::wstring_view const& v)
	{
		fz::scoped_lock l(m_);
		auto it = std::lower_bound(cache.begin(), cache.end(), v, [&](auto const& a, auto const& b) { return *a < b; });

		if (it != cache.end() && **it == v) {
			return *it;
		}

		auto ret = *cache.emplace(it, std::wstring(v));
		prune();
		return ret;
	}

	fz::shared_value<std::wstring> get(std::wstring && v)
	{
		fz::scoped_lock l(m_);
		auto it = std::lower_bound(cache.begin(), cache.end(), v);

		if (it != cache.end() && *it == v) {
			return *it;
		}

		auto ret = *cache.emplace(it, std::move(v));
		prune();
		return ret;
	}

private:
	void prune()
	{
		constexpr size_t threshold = 5000;
		constexpr size_t target = threshold - 1000;

		if (cache.size() > threshold) {
			cache.erase(
				std::remove_if(cache.begin(), cache.end(), [&](auto &e) {
					return e.use_count() <= 1;
				}),	cache.end());

			if (cache.size() > threshold) {
				cache.erase(cache.begin(), cache.begin() + (cache.size() - target));
			}
		}
	}

	fz::mutex m_{false};
	// Vector coupled with binary search and sorted insertion is fastest
	// alternative as we expect a relatively low amount of inserts.
	std::vector<fz::shared_value<std::wstring>> cache;
};


ObjectCache objcache;
}

bool CToken::IsNumeric(t_numberBase base)
{
	switch (base)
	{
	case decimal:
	default:
		if (!(flags_ & (numeric | non_numeric))) {
			if (data_.empty()) {
				flags_ |= non_numeric;
			}
			else {
				flags_ |= numeric;
				for (size_t i = 0; i < data_.size(); ++i) {
					if (data_[i] < '0' || data_[i] > '9') {
						flags_ ^= numeric | non_numeric;
						break;
					}
				}
			}
		}
		return flags_ & numeric;
	case hex:
		if (data_.empty()) {
			return false;
		}
		for (size_t i = 0; i < data_.size(); ++i) {
			auto const c = data_[i];
			if ((c < '0' || c > '9') && (c < 'A' || c > 'F') && (c < 'a' || c > 'f')) {
				return false;
			}
		}
		return true;
	}
}

bool CToken::IsNumeric(size_t start, size_t len)
{
	if (start >= data_.size()) {
		return false;
	}
	if (!len || data_.size() - start < len) {
		return false;
	}
	for (size_t i = start; i < start + len; ++i) {
		if (data_[i] < '0' || data_[i] > '9') {
			return false;
		}
	}
	return true;
}

bool CToken::IsLeftNumeric()
{
	if (!(flags_ & (numeric_left | non_numeric_left))) {
		if (data_.size() < 2 || data_[0] < '0' || data_[0] > '9') {
			flags_ |= non_numeric_left;
		}
		else {
			flags_ |= numeric_left;
		}
	}
	return flags_ & numeric_left;
}

bool CToken::IsRightNumeric()
{
	if (!(flags_ & (numeric_right | non_numeric_right))) {
		if (data_.size() < 2 || data_.back() < '0' || data_.back() > '9') {
			flags_ |= non_numeric_right;
		}
		else {
			flags_ |= numeric_right;
		}
	}
	return flags_ & numeric_right;
}

int CToken::Find(wchar_t const* chr, size_t start) const
{
	if (!chr) {
		return -1;
	}

	for (size_t i = start; i < data_.size(); ++i) {
		for (size_t c = 0; chr[c]; ++c) {
			if (data_[i] == chr[c]) {
				return i;
			}
		}
	}
	return -1;
}

int CToken::Find(wchar_t chr, size_t start) const
{
	for (size_t i = start; i < data_.size(); ++i) {
		if (data_[i] == chr) {
			return i;
		}
	}

	return -1;
}

int64_t CToken::GetNumber(std::wstring_view s, t_numberBase base, bool trailingDataIsError)
{
	if (s.empty()) {
		return -1;
	}
	constexpr int64_t max = std::numeric_limits<int64_t>::max();

	int64_t v{};

	switch (base) {
	default:
	case decimal:
		{
			constexpr int64_t max10 = max / 10;
			for (size_t i = 0; i < s.size(); ++i) {
				auto const c = s[i];
				if (c < '0' || c > '9') {
					if (!i || trailingDataIsError) {
						return -1;
					}
					break;
				}

				if (v > max10) {
					return -1;
				}
				v *= 10;
				auto digit = c - '0';
				if (max - digit < v) {
					return -1;
				}
				v += c - '0';
			}
			return v;
		}
	case hex:
		{
			constexpr int64_t max = std::numeric_limits<int64_t>::max();
			constexpr int64_t max16 = max / 16;
			for (size_t i = 0; i < s.size(); ++i) {
				auto const c = s[i];

				int64_t digit;
				if (c >= '0' && c <= '9') {
					digit = c - '0';
				}
				else if (c >= 'a' && c <= 'f') {
					digit= c - 'a' + 10;
				}
				else if (c >= 'A' && c <= 'F') {
					digit = c - 'A' + 10;
				}
				else {
					if (!i || trailingDataIsError) {
						return -1;
					}
					break;
				}

				if (v > max16) {
					return -1;
				}
				v *= 16;
				if (max - digit < v) {
					return -1;
				}
				v += digit;
			}
			return v;
		}
	}

	return -1;
}

int64_t CToken::GetNumber(size_t start, int len)
{
	if (start >= data_.size()) {
		return -1;
	}

	if (len == -1) {
		len = data_.size() - start;
	}
	else if (len < 1) {
		return -1;
	}
	else {
		if (data_.size() - start < static_cast<size_t>(len)) {
			return -1;
		}
	}
	return GetNumber(data_.substr(start, static_cast<size_t>(len)), decimal);
}

int64_t CToken::GetNumber(t_numberBase base)
{
	switch (base) {
	default:
	case decimal:
		if (m_number == std::numeric_limits<int64_t>::min()) {
			if (IsNumeric() || IsLeftNumeric()) {
				m_number = GetNumber(data_, base);
			}
			else if (IsRightNumeric()) {
				m_number = 0;
				size_t start = data_.size() - 1;
				// start-1 cannot underflow, as otherwise IsNumeric() would have been true.
				while (data_[start - 1] >= '0' && data_[start - 1] <= '9') {
					--start;
				}
				m_number = GetNumber(data_.substr(start));
			}
		}
		return m_number;
	case hex:
		return GetNumber(data_, base, true);
	}
}

CLine::CLine(std::wstring_view const& line, size_t trailing_whitespace)
	: line_(line)
	, trailing_whitespace_(trailing_whitespace)
{
	m_Tokens.reserve(10);
	while (m_parsePos < line_.size() && (line_[m_parsePos] == ' ' || line_[m_parsePos] == '\t')) {
		++m_parsePos;
	}
}

CToken CLine::GetToken(unsigned int n)
{
	if (m_Tokens.size() > n) {
		return m_Tokens[n];
	}

	size_t start = m_parsePos;
	while (m_parsePos < line_.size()) {
		if (line_[m_parsePos] == ' ' || line_[m_parsePos] == '\t') {
			m_Tokens.emplace_back(line_.substr(start, m_parsePos - start));

			while (m_parsePos < line_.size() && (line_[m_parsePos] == ' ' || line_[m_parsePos] == '\t')) {
				++m_parsePos;
			}

			if (m_Tokens.size() > n) {
				return m_Tokens[n];
			}

			start = m_parsePos;
		}
		++m_parsePos;
	}
	if (m_parsePos != start) {
		m_Tokens.emplace_back(line_.substr(start, m_parsePos - start));
	}

	if (m_Tokens.size() > n) {
		return m_Tokens[n];
	}

	return CToken();
}

CToken CLine::GetEndToken(unsigned int n, bool include_whitespace)
{
	if (include_whitespace) {
		int prev = n;
		if (prev) {
			--prev;
		}

		CToken ref = GetToken(prev);
		if (!ref) {
			return ref;
		}
		wchar_t const* p = ref.data() + ref.size() + 1;

		if (static_cast<size_t>(p - line_.data()) >= line_.size()) {
			return CToken();
		}

		auto newLen = line_.size() - (p - line_.data());
		return CToken(p, newLen);
	}

	if (trailing_whitespace_ == std::string::npos && !line_.empty()) {
		trailing_whitespace_ = 0;
		size_t i = line_.size() - 1;
		while (i < line_.size() && (line_[i] == ' ' || line_[i] == '\t')) {
			--i;
			++trailing_whitespace_;
		}
	}


	CToken t = GetToken(n);
	if (!t) {
		return {};
	}

	size_t len = line_.size() - trailing_whitespace_ - (t.data() - line_.data());
	return CToken(t.data(), len);
}

CDirectoryListingParser::CDirectoryListingParser(CControlSocket* pControlSocket, const CServer& server, listingEncoding::type encoding)
	: m_pControlSocket(pControlSocket)
	, m_server(server)
	, m_listingEncoding(encoding)
{
#ifdef LISTDEBUG
	for (size_t i = 0; !data[i].empty(); ++i) {
		GetInputBuffer().append(data[i]);
		GetInputBuffer().append("\r\n"sv);
	}
#endif

	if (m_pControlSocket) {
		limit_ = static_cast<size_t>(m_pControlSocket->GetEngine().GetOptions().get<size_t>(OPTION_DIRECTORY_LISTING_ITEM_LIMIT));
	}
}

CDirectoryListingParser::~CDirectoryListingParser() = default;

bool CDirectoryListingParser::ParseData(bool partial)
{
	ConvertEncoding();

	bool error = false;
	std::wstring raw_line = GetLine(partial, error);
	while (!raw_line.empty()) {
		CLine line(raw_line);
		bool res = ParseLine(line, m_server.GetType(), false);
		if (!res) {
			if (!prevLine_.empty()) {
				prevLine_ += ' ';
				prevLine_ += raw_line;
				CLine concatedLine(prevLine_, line.TrailingWhitespace());
				res = ParseLine(concatedLine, m_server.GetType(), true);
				if (res) {
					prevLine_.clear();
				}
				else {
					prevLine_ = std::move(raw_line);
				}
			}
			else {
				prevLine_ = std::move(raw_line);
			}
		}
		else {
			prevLine_.clear();
		}
		raw_line = GetLine(partial, error);
	};

	if (converted_ > inbuf_.size()) {
		converted_ = inbuf_.size();
	}

	return !error;
}

CDirectoryListing CDirectoryListingParser::Parse(const CServerPath &path)
{
	CDirectoryListing listing;
	listing.path = path;
	listing.m_firstListTime = fz::monotonic_clock::now();

	if (!ParseData(false)) {
		listing.m_flags |= CDirectoryListing::listing_failed;
		return listing;
	}

	if (!m_fileList.empty()) {
		assert(entries_.empty());

		entries_.reserve(m_fileList.size());
		for (auto const& file : m_fileList) {
			CDirentry entry;
			entry.name = file;
			entry.flags = 0;
			entry.size = -1;
			entries_.emplace_back(std::move(entry));
		}
	}

	listing.Assign(std::move(entries_));

	return listing;
}

bool CDirectoryListingParser::ParseLine(CLine &line, ServerType const serverType, bool concatenated, CDirentry const* override)
{
	fz::shared_value<CDirentry> refEntry;
	CDirentry & entry = refEntry.get();

	bool res;
	int ires;

	if (serverType == ZVM) {
		res = ParseAsZVM(line, entry);
		if (res) {
			goto done;
		}
	}
	else if (serverType == HPNONSTOP) {
		res = ParseAsHPNonstop(line, entry);
		if (res) {
			goto done;
		}
	}

	ires = ParseAsMlsd(line, entry);
	if (ires == 1) {
		goto done;
	}
	else if (ires == 2) {
		goto skip;
	}
	res = ParseAsUnix(line, entry, true); // Common 'ls -l'
	if (res) {
		goto done;
	}
	res = ParseAsDos(line, entry);
	if (res) {
		goto done;
	}
	res = ParseAsEplf(line, entry);
	if (res) {
		goto done;
	}
	res = ParseAsVms(line, entry);
	if (res) {
		goto done;
	}
	res = ParseOther(line, entry);
	if (res) {
		goto done;
	}
	res = ParseAsIbm(line, entry);
	if (res) {
		goto done;
	}
	res = ParseAsWfFtp(line, entry);
	if (res) {
		goto done;
	}
	res = ParseAsIBM_MVS(line, entry);
	if (res) {
		goto done;
	}
	res = ParseAsIBM_MVS_PDS(line, entry);
	if (res) {
		goto done;
	}
	res = ParseAsOS9(line, entry);
	if (res) {
		goto done;
	}
#ifndef LISTDEBUG_MVS
	if (serverType == MVS)
#endif //LISTDEBUG_MVS
	{
		res = ParseAsIBM_MVS_Migrated(line, entry);
		if (res) {
			goto done;
		}
		res = ParseAsIBM_MVS_PDS2(line, entry);
		if (res) {
			goto done;
		}
		res = ParseAsIBM_MVS_Tape(line, entry);
		if (res) {
			goto done;
		}
	}
	res = ParseAsUnix(line, entry, false); // 'ls -l' but without the date/time
	if (res) {
		goto done;
	}

	// Some servers just send a list of filenames. If a line could not be parsed,
	// check if it's a filename. If that's the case, store it for later, else clear
	// list of stored files.
	// If parsing finishes and no entries could be parsed and none of the lines
	// contained a space, assume it's a raw filelisting.

	if (!concatenated) {
		CToken token = line.GetEndToken(0);
		if (!token || token.Find(' ') != -1) {
			m_maybeMultilineVms = false;
			m_fileList.clear();
			m_fileListOnly = false;
		}
		else {
			m_maybeMultilineVms = token.Find(';') != -1;
			if (m_fileListOnly) {
				if (m_fileList.size() < limit_) {
					m_fileList.emplace_back(token.get_view());
				}
				else {
					if (!truncated_) {
						if (m_pControlSocket) {
							m_pControlSocket->log(logmsg::error, _("Truncating directory listing to %u items, you can increase this limit in the settings file."), limit_);
						}
						truncated_ = true;
					}
				}
			}
		}
	}
	else {
		m_maybeMultilineVms = false;
	}

	if (!override || override->name.empty()) {
		return false;
	}

	entry = *override;
	goto done2;

done:

	if (override) {
		// If SFTP is used we already have precise data for some fields
		if (!override->name.empty()) {
			entry.name = override->name;
		}
		if (!override->time.empty()) {
			entry.time = override->time;
		}
		if (!entry.is_dir() && override->size != -1) {
			entry.size = override->size;
		}
		// Not doing flags for now, would need to stat each entry to resolve links
	}

done2:

	m_maybeMultilineVms = false;
	m_fileList.clear();
	m_fileListOnly = false;

	// Don't add . or ..
	if (entry.name == L"." || entry.name == L"..") {
		return true;
	}

	if (serverType == VMS && entry.is_dir()) {
		// Trim version information from directories
		auto pos = entry.name.rfind(';');
		if (pos != std::wstring::npos && pos > 0) {
			entry.name = entry.name.substr(0, pos);
		}
	}

	{
		// Apply user-supplied offset to adjust for incorrectly set server clocks
		auto const timezoneOffset = m_server.GetTimezoneOffset();
		if (timezoneOffset) {
			entry.time += fz::duration::from_minutes(timezoneOffset);
		}
	}

	if (entries_.size() < limit_) {
		entries_.emplace_back(std::move(refEntry));
	}
	else {
		if (!truncated_) {
			if (m_pControlSocket) {
				m_pControlSocket->log(logmsg::error, _("Truncating directory listing to %u items, you can increase this limit in the settings file."), limit_);
			}
			truncated_ = true;
		}
	}


skip:
	m_maybeMultilineVms = false;
	m_fileList.clear();
	m_fileListOnly = false;

	return true;
}

bool CDirectoryListingParser::ParseAsUnix(CLine &line, CDirentry &entry, bool expect_date)
{
	int index = 0;
	CToken permissionToken = line.GetToken(index);
	if (!permissionToken) {
		return false;
	}

	wchar_t chr = permissionToken[0];
	if (chr != 'b' &&
		chr != 'c' &&
		chr != 'd' &&
		chr != 'l' &&
		chr != 'p' &&
		chr != 's' &&
		chr != '-')
	{
		return false;
	}

	auto permissions = std::wstring(permissionToken.get_view());

	entry.flags = 0;

	if (chr == 'd' || chr == 'l') {
		entry.flags |= CDirentry::flag_dir;
	}

	if (chr == 'l') {
		entry.flags |= CDirentry::flag_link;
	}

	// Check for netware servers, which split the permissions into two parts
	bool netware = false;
	if (permissionToken.size() == 1) {
		CToken cont_perm = line.GetToken(++index);
		if (!cont_perm) {
			return false;
		}
		permissions += ' ';
		permissions += cont_perm.get_view();
		netware = true;
	}

	int numOwnerGroup = 3;
	if (!netware) {
		// Filter out link count, we don't need it
		CToken linkCount = line.GetToken(++index);
		if (!linkCount) {
			return false;
		}

		if (!linkCount.IsNumeric()) {
			--index;
		}
	}

	// Repeat until numOwnerGroup is 0 since not all servers send every possible field
	int startindex = index;
	do {
		// Reset index
		index = startindex;

		std::wstring ownerGroup;
		for (int i = 0; i < numOwnerGroup; ++i) {
			CToken ownerGroupToken = line.GetToken(++index);
			if (!ownerGroupToken) {
				return false;
			}
			if (i) {
				ownerGroup += L" ";
			}
			ownerGroup += ownerGroupToken.get_view();
		}


		CToken sizeToken = line.GetToken(++index);
		if (!sizeToken) {
			return false;
		}

		// Check for concatenated groupname and size fields
		if (!ParseComplexFileSize(sizeToken, entry.size)) {
			if (!sizeToken.IsRightNumeric()) {
				continue;
			}
			entry.size = sizeToken.GetNumber();

			// Append missing group to ownerGroup
			if (!ownerGroup.empty()) {
				ownerGroup += L" ";
			}

			auto group = sizeToken.get_view();
			while (!group.empty() && group.back() >= '0' && group.back() <= '9') {
				group.remove_suffix(1);
			}
			ownerGroup += group;
		}

		if (expect_date) {
			entry.time = fz::datetime();
			if (!ParseUnixDateTime(line, index, entry))
				continue;
		}

		// Get the filename
		CToken nameToken = line.GetEndToken(++index);
		if (!nameToken) {
			continue;
		}

		entry.name = nameToken.get_view();

		// Filter out special chars at the end of the filenames
		chr = nameToken[nameToken.size() - 1];
		if (chr == '/' ||
			chr == '|' ||
			chr == '*')
		{
			entry.name.pop_back();
		}

		if (entry.is_link()) {
			size_t pos;
			if ((pos = entry.name.find(L" -> ")) != std::wstring::npos) {
				entry.target = fz::sparse_optional<std::wstring>(entry.name.substr(pos + 4));
				entry.name = entry.name.substr(0, pos);
			}
		}

		entry.time += m_timezoneOffset;

		entry.permissions = objcache.get(permissions);
		entry.ownerGroup = objcache.get(ownerGroup);
		return true;
	}
	while (numOwnerGroup--);

	return false;
}

bool CDirectoryListingParser::ParseUnixDateTime(CLine & line, int &index, CDirentry &entry)
{
	bool mayHaveTime = true;
	bool bHasYearAndTime = false;

	// Get the month date field
	CToken token = line.GetToken(++index);
	if (!token) {
		return false;
	}

	int year = -1;
	int month = -1;
	int day = -1;
	long hour = -1;
	long minute = -1;

	CToken dateMonth;

	// Some servers use the following date formats:
	// 26-05 2002, 2002-10-14, 01-jun-99 or 2004.07.15
	// slashes instead of dashes are also possible
	int pos = token.Find(L"-/.");
	if (pos != -1) {
		int pos2 = token.Find(L"-/.", pos + 1);
		if (pos2 == -1) {
			if (token[pos] != '.') {
				// something like 26-05 2002
				day = token.GetNumber(pos + 1, token.size() - pos - 1);
				if (day < 1 || day > 31) {
					return false;
				}
				dateMonth = CToken(token.data(), pos);
			}
			else {
				dateMonth = token;
			}
		}
		else if (token[pos] != token[pos2]) {
			return false;
		}
		else {
			if (!ParseShortDate(token, entry)) {
				return false;
			}

			if (token[pos] == '.') {
				return true;
			}

			tm t = entry.time.get_tm(fz::datetime::utc);
			year = t.tm_year + 1900;
			month = t.tm_mon + 1;
			day = t.tm_mday;
		}
	}
	else if (token.IsNumeric()) {
		if (token.GetNumber() > 1000 && token.GetNumber() < 10000) {
			// Two possible variants:
			// 1) 2005 3 13
			// 2) 2005 13 3
			// assume first one.
			year = token.GetNumber();
			dateMonth = line.GetToken(++index);
			if (!dateMonth) {
				return false;
			}
			mayHaveTime = false;
		}
		else {
			dateMonth = token;
		}
	}
	else {
		if (token.IsLeftNumeric() && (unsigned int)token[token.size() - 1] > 127 &&
			token.GetNumber() > 1000)
		{
			if (token.GetNumber() > 10000) {
				return false;
			}

			// Asian date format: 2005xxx 5xx 20xxx with some non-ascii characters following
			year = token.GetNumber();

			dateMonth = line.GetToken(++index);
			if (!dateMonth) {
				return false;
			}
			mayHaveTime = false;
		}
		else {
			dateMonth = token;
		}
	}

	if (day < 1) {
		// Get day field
		CToken dayToken = line.GetToken(++index);
		if (!dayToken) {
			return false;
		}

		int dateDay;

		// Check for non-numeric day
		if (!dayToken.IsNumeric() && !dayToken.IsLeftNumeric()) {
			int offset = 0;
			if (dateMonth.get_view().back() == '.') {
				++offset;
			}
			if (!dateMonth.IsNumeric(0, dateMonth.size() - offset)) {
				return false;
			}
			dateDay = dateMonth.GetNumber(0, dateMonth.size() - offset);
			dateMonth = dayToken;
		}
		else if (dayToken.size() == 5 && dayToken[2] == ':' && dayToken.IsRightNumeric() ) {
			// This is a time. We consumed too much already.
			return false;
		}
		else {
			dateDay = dayToken.GetNumber();
			if (dayToken[dayToken.size() - 1] == ',') {
				bHasYearAndTime = true;
			}
		}

		if (dateDay < 1 || dateDay > 31) {
			return false;
		}
		day = dateDay;
	}

	if (month < 1) {
		auto strMonth = dateMonth.get_view();
		if (dateMonth.IsLeftNumeric() && (unsigned int)strMonth[strMonth.size() - 1] > 127) {
			// Most likely an Asian server sending some unknown language specific
			// suffix at the end of the monthname. Filter it out.
			int i;
			for (i = strMonth.size() - 1; i > 0; --i) {
				if (strMonth[i] >= '0' && strMonth[i] <= '9') {
					break;
				}
			}
			strMonth = strMonth.substr(0, i + 1);
		}
		// Check month name
		while (!strMonth.empty() && (strMonth.back() == ',' || strMonth.back() == '.')) {
			strMonth.remove_suffix(1);
		}
		if (!GetMonthFromName(strMonth, month)) {
			return false;
		}
	}

	// Get time/year field
	CToken timeOrYearToken = line.GetToken(++index);
	if (!timeOrYearToken) {
		return false;
	}

	pos = timeOrYearToken.Find(L":.-");
	if (pos != -1 && mayHaveTime) {
		// token is a time
		if (!pos || static_cast<size_t>(pos) == (timeOrYearToken.size() - 1)) {
			return false;
		}

		auto str = timeOrYearToken.get_view();
		hour = fz::to_integral<int>(str.substr(0, pos), -1);
		minute = fz::to_integral<int>(str.substr(pos + 1), -1);

		if (hour < 0 || hour > 23) {
			// Allow alternate midnight representation
			if (hour != 24 || minute != 0) {
				return false;
			}
		}
		else if (minute < 0 || minute > 59) {
			return false;
		}

		// Some servers use times only for files newer than 6 months
		if (year <= 0) {
			if (month == -1 || day == -1) {
				return false;
			}
			tm const t = fz::datetime::now().get_tm(fz::datetime::utc);
			year = t.tm_year + 1900;
			int const currentDayOfYear = t.tm_mday + 31 * t.tm_mon;
			int const fileDayOfYear = day + 31 * (month - 1);

			// We have to compare with an offset of one. In the worst case,
			// the server's timezone might be up to 24 hours ahead of the
			// client.
			// Problem: Servers which do send the time but not the year even
			// one day away from getting 1 year old. This is far more uncommon
			// however.
			if ((currentDayOfYear + 1) < fileDayOfYear) {
				year -= 1;
			}
		}
	}
	else if (year <= 0) {
		// token is a year
		if (!timeOrYearToken.IsNumeric() && !timeOrYearToken.IsLeftNumeric()) {
			return false;
		}

		year = timeOrYearToken.GetNumber();

		if (year > 3000) {
			return false;
		}
		if (year < 1000) {
			year += 1900;
		}

		if (bHasYearAndTime) {
			CToken timeToken = line.GetToken(++index);
			if (!timeToken) {
				return false;
			}

			if (timeToken.Find(':') == 2 && timeToken.size() == 5 && timeToken.IsLeftNumeric() && timeToken.IsRightNumeric()) {
				pos = timeToken.Find(':');
				// token is a time
				if (!pos || static_cast<size_t>(pos) == (timeToken.size() - 1)) {
					return false;
				}

				auto str = timeToken.get_view();
				hour = fz::to_integral<int>(str.substr(0, pos), -1);
				minute = fz::to_integral<int>(str.substr(pos + 1), -1);

				if (hour < 0 || hour > 23) {
					// Allow alternate midnight representation
					if (hour != 24 || minute != 0) {
						return false;
					}
				}
				else if (minute < 0 || minute > 59) {
					return false;
				}
			}
			else {
				--index;
			}
		}
	}
	else {
		--index;
	}

	if (!entry.time.set(fz::datetime::utc, year, month, day, hour, minute)) {
		return false;
	}

	return true;
}

bool CDirectoryListingParser::ParseShortDate(CToken &token, CDirentry &entry, bool saneFieldOrder)
{
	if (token.size() < 1) {
		return false;
	}

	bool gotYear = false;
	bool gotMonth = false;
	bool gotDay = false;
	bool gotMonthName = false;

	int year = 0;
	int month = 0;
	int day = 0;

	int pos = token.Find(L"-./");
	if (pos < 1) {
		return false;
	}

	if (!token.IsNumeric(0, pos)) {
		// Seems to be monthname-dd-yy

		// Check month name
		auto dateMonth = token.get_view().substr(0, pos);
		if (!GetMonthFromName(dateMonth, month)) {
			return false;
		}
		gotMonth = true;
		gotMonthName = true;
	}
	else if (pos == 4) {
		// Seems to be yyyy-mm-dd
		year = token.GetNumber(0, pos);
		if (year < 1900 || year > 3000) {
			return false;
		}
		gotYear = true;
	}
	else if (pos <= 2) {
		int64_t value = token.GetNumber(0, pos);
		if (token[pos] == '.') {
			// Maybe dd.mm.yyyy
			if (value < 1 || value > 31) {
				return false;
			}
			day = value;
			gotDay = true;
		}
		else {
			if (saneFieldOrder) {
				year = value;
				if (year < 50) {
					year += 2000;
				}
				else {
					year += 1900;
				}
				gotYear = true;
			}
			else {
				// Detect mm-dd-yyyy or mm/dd/yyyy and
				// dd-mm-yyyy or dd/mm/yyyy
				if (value < 1) {
					return false;
				}
				if (value > 12) {
					if (value > 31) {
						return false;
					}

					day = value;
					gotDay = true;
				}
				else {
					month = value;
					gotMonth = true;
				}
			}
		}
	}
	else {
		return false;
	}

	int pos2 = token.Find(L"-./", pos + 1);
	if (pos2 == -1 || (pos2 - pos) == 1) {
		return false;
	}
	if (static_cast<size_t>(pos2) == (token.size() - 1)) {
		return false;
	}

	// If we already got the month and the second field is not numeric,
	// change old month into day and use new token as month
	if (!token.IsNumeric(pos + 1, pos2 - pos - 1) && gotMonth) {
		if (gotMonthName) {
			return false;
		}

		if (gotDay) {
			return false;
		}

		gotDay = true;
		gotMonth = false;
		day = month;
	}

	if (gotYear || gotDay) {
		// Month field in yyyy-mm-dd or dd-mm-yyyy
		// Check month name
		auto dateMonth = token.get_view().substr(pos + 1, pos2 - pos - 1);
		if (!GetMonthFromName(dateMonth, month)) {
			return false;
		}
		gotMonth = true;
	}
	else {
		int64_t value = token.GetNumber(pos + 1, pos2 - pos - 1);
		// Day field in mm-dd-yyyy
		if (value < 1 || value > 31) {
			return false;
		}
		day = value;
		gotDay = true;
	}

	int64_t value = token.GetNumber(pos2 + 1, token.size() - pos2 - 1);
	if (gotYear) {
		// Day field in yyy-mm-dd
		if (value <= 0 || value > 31) {
			return false;
		}
		day = value;
		gotDay = true;
	}
	else {
		if (value < 0 || value > 9999) {
			return false;
		}

		if (value < 50) {
			value += 2000;
		}
		else if (value < 1000) {
			value += 1900;
		}
		year = value;

		gotYear = true;
	}

	if (!gotYear || !gotMonth || !gotDay) {
		return false;
	}

	if (!entry.time.set(fz::datetime::utc, year, month, day)) {
		return false;
	}

	return true;
}

bool CDirectoryListingParser::ParseAsDos(CLine &line, CDirentry &entry)
{
	int index = 0;
	// Get first token, has to be a valid date
	CToken token = line.GetToken(index);
	if (!token) {
		return false;
	}

	entry.flags = 0;

	if (!ParseShortDate(token, entry)) {
		return false;
	}

	// Extract time
	if (!(token = line.GetToken(++index))) {
		return false;
	}

	if (!ParseTime(token, entry)) {
		return false;
	}

	// If next token is <DIR>, entry is a directory
	// else, it should be the filesize.
	if (!(token = line.GetToken(++index)))
		return false;

	if (token.get_view() == L"<DIR>"sv) {
		entry.flags |= CDirentry::flag_dir;
		entry.size = -1;
	}
	else if (token.IsNumeric() || token.IsLeftNumeric()) {
		// Convert size, filter out separators
		int64_t size = 0;
		for (size_t i = 0; i < token.size(); ++i) {
			auto const c = token[i];
			if (c == ',' || c == '.') {
				continue;
			}
			if (c < '0' || c > '9') {
				return false;
			}
			constexpr int64_t max = std::numeric_limits<int64_t>::max();
			constexpr int64_t max10 = max / 10;

			if (size > max10) {
				return false;
			}
			size *= 10;

			auto digit = c - '0';
			if (max - digit < size) {
				return false;
			}
			size += digit;
		}
		entry.size = size;
	}
	else {
		return false;
	}

	// Extract filename
	if (!(token = line.GetEndToken(++index))) {
		return false;
	}
	entry.name = token.get_view();

	entry.target.clear();
	entry.ownerGroup = objcache.get(std::wstring_view());
	entry.permissions = entry.ownerGroup;
	entry.time += m_timezoneOffset;

	return true;
}

bool CDirectoryListingParser::ParseTime(CToken &token, CDirentry &entry)
{
	if (!entry.has_date()) {
		return false;
	}

	int pos = token.Find(':');
	if (pos < 1 || static_cast<unsigned int>(pos) >= (token.size() - 1)) {
		return false;
	}

	int64_t hour = token.GetNumber(0, pos);
	if (hour < 0 || hour > 24) {
		return false;
	}

	// See if we got seconds
	int pos2 = token.Find(':', pos + 1);
	int len;
	if (pos2 == -1) {
		len = -1;
	}
	else {
		len = pos2 - pos - 1;
	}

	if (!len) {
		return false;
	}

	int64_t minute = token.GetNumber(pos + 1, len);
	if (minute < 0 || minute > 59) {
		return false;
	}

	int64_t seconds = -1;
	if (pos2 != -1) {
		// Parse seconds
		seconds = token.GetNumber(pos2 + 1, -1);
		if (seconds < 0 || seconds > 60) {
			return false;
		}
	}

	// Convert to 24h format.
	if (!token.IsRightNumeric()) {
		auto h = token[token.size() - 2];
		if (h == 'P' || h == 'p') {
			if (hour < 12) {
				hour += 12;
			}
		}
		else if (h == 'A' || h == 'a') {
			if (hour == 12) {
				hour = 0;
			}
		}
		else {
			return false;
		}
	}

	// imbue_time checks for alternate midnight and rejects invalid times
	return entry.time.imbue_time(hour, minute, seconds);
}

bool CDirectoryListingParser::ParseAsEplf(CLine &line, CDirentry &entry)
{
	CToken token = line.GetEndToken(0);
	if (!token) {
		return false;
	}

	if (token[0] != '+') {
		return false;
	}

	int pos = token.Find('\t');
	if (pos == -1 || static_cast<size_t>(pos) == (token.size() - 1)) {
		return false;
	}

	entry.name = token.get_view().substr(pos + 1);

	entry.flags = 0;
	entry.size = -1;

	std::wstring_view permissions;

	int fact = 1;
	while (fact < pos) {
		int separator = token.Find(',', fact);
		int len;
		if (separator == -1) {
			len = pos - fact;
		}
		else {
			len = separator - fact;
		}

		if (!len) {
			++fact;
			continue;
		}

		auto const type = token[fact];

		if (type == '/') {
			entry.flags |= CDirentry::flag_dir;
		}
		else if (type == 's') {
			entry.size = token.GetNumber(fact + 1, len - 1);
			if (entry.size < 0) {
				return false;
			}
		}
		else if (type == 'm') {
			int64_t number = token.GetNumber(fact + 1, len - 1);
			if (number < 0) {
				return false;
			}
			entry.time = fz::datetime(static_cast<time_t>(number), fz::datetime::seconds);
		}
		else if (type == 'u' && len > 2 && token[fact + 1] == 'p') {
			permissions = token.get_view().substr(fact + 2, len - 2);
		}

		fact += len + 1;
	}

	entry.permissions = objcache.get(permissions);
	entry.ownerGroup = objcache.get(std::wstring_view());
	return true;
}

namespace {
std::wstring Unescape(const std::wstring& str, wchar_t escape)
{
	std::wstring res;
	for (unsigned int i = 0; i < str.size(); ++i) {
		wchar_t c = str[i];
		if (c == escape) {
			++i;
			if (i == str.size() || !str[i]) {
				break;
			}
			c = str[i];
		}
		res += c;
	}

	return res;
}
}

bool CDirectoryListingParser::ParseAsVms(CLine &line, CDirentry &entry)
{
	int index = 0;
	CToken token = line.GetToken(index);

	if (!token) {
		return false;
	}

	int pos = token.Find(';');
	if (pos == -1) {
		return false;
	}

	entry.flags = 0;

	if (pos > 4 && token.get_view().substr(pos - 4, 4) == L".DIR"sv) {
		entry.flags |= CDirentry::flag_dir;
		if (token.get_view().substr(pos) == L";1"sv) {
			entry.name = token.get_view().substr(0, pos - 4);
		}
		else {
			entry.name = token.get_view().substr(0, pos - 4);
			entry.name += token.get_view().substr(pos);
		}
	}
	else {
		entry.name = token.get_view();
	}

	// Some VMS servers escape special characters like additional dots with ^
	entry.name = Unescape(entry.name, '^');

	if (!(token = line.GetToken(++index))) {
		return false;
	}

	std::wstring ownerGroup;
	std::wstring permissions;

	// This field can either be the filesize, a username (at least that's what I think) enclosed in [] or a date.
	if (!token.IsNumeric() && !token.IsLeftNumeric()) {
		// Must be username
		const size_t len = token.size();
		if (len < 3 || token[0] != '[' || token[len - 1] != ']') {
			return false;
		}
		ownerGroup = token.get_view().substr(1, len - 2);

		if (!(token = line.GetToken(++index))) {
			return false;
		}
		if (!token.IsNumeric() && !token.IsLeftNumeric()) {
			return false;
		}
	}

	// Current token is either size or date
	bool gotSize = false;
	pos = token.Find('/');

	if (!pos) {
		return false;
	}

	if (token.IsNumeric() || (pos != -1 && token.Find('/', pos + 1) == -1)) {
		// Definitely size
		CToken sizeToken;
		if (pos == -1) {
			sizeToken = token;
		}
		else {
			sizeToken = CToken(token.data(), pos);
		}

		if (!ParseComplexFileSize(sizeToken, entry.size, 512)) {
			return false;
		}
		gotSize = true;

		if (!(token = line.GetToken(++index))) {
			return false;
		}
	}
	else if (pos == -1 && token.IsLeftNumeric()) {
		// Perhaps size
		if (ParseComplexFileSize(token, entry.size, 512)) {
			gotSize = true;

			if (!(token = line.GetToken(++index))) {
				return false;
			}
		}
	}

	// Get date
	if (!ParseShortDate(token, entry)) {
		return false;
	}

	// Get time
	if (!(token = line.GetToken(++index))) {
		return true;
	}

	if (!ParseTime(token, entry)) {
		size_t len = token.size();
		if (token[0] == '[' && token[len - 1] != ']') {
			return false;
		}
		if (token[0] == '(' && token[len - 1] != ')') {
			return false;
		}
		if (token[0] != '[' && token[len - 1] == ']') {
			return false;
		}
		if (token[0] != '(' && token[len - 1] == ')') {
			return false;
		}
		--index;
	}

	if (!gotSize) {
		// Get size
		if (!(token = line.GetToken(++index))) {
			return false;
		}

		if (!token.IsNumeric() && !token.IsLeftNumeric()) {
			return false;
		}

		pos = token.Find('/');
		if (!pos) {
			return false;
		}

		CToken sizeToken;
		if (pos == -1) {
			sizeToken = token;
		}
		else {
			sizeToken = CToken(token.data(), pos);
		}
		if (!ParseComplexFileSize(sizeToken, entry.size, 512)) {
			return false;
		}
	}

	// Owner / group and permissions
	while ((token = line.GetToken(++index))) {
		const size_t len = token.size();
		if (len > 2 && token[0] == '(' && token[len - 1] == ')') {
			if (!permissions.empty()) {
				permissions += ' ';
			}
			permissions += token.get_view().substr(1, len - 2);
		}
		else if (len > 2 && token[0] == '[' && token[len - 1] == ']') {
			if (!ownerGroup.empty()) {
				ownerGroup += ' ';
			}
			ownerGroup += token.get_view().substr(1, len - 2);
		}
		else {
			if (!ownerGroup.empty()) {
				ownerGroup += ' ';
			}
			ownerGroup += token.get_view();
		}
	}
	entry.permissions = objcache.get(permissions);
	entry.ownerGroup = objcache.get(ownerGroup);

	entry.time += m_timezoneOffset;

	return true;
}

bool CDirectoryListingParser::ParseAsIbm(CLine &line, CDirentry &entry)
{
	int index = 0;

	// Get owner
	CToken ownerGroupToken = line.GetToken(index);
	if (!ownerGroupToken) {
		return false;
	}

	// Get size
	CToken token = line.GetToken(++index);
	if (!token) {
		return false;
	}

	if (!token.IsNumeric()) {
		return false;
	}

	entry.size = token.GetNumber();

	// Get date
	if (!(token = line.GetToken(++index))) {
		return false;
	}

	entry.flags = 0;

	if (!ParseShortDate(token, entry)) {
		return false;
	}

	// Get time
	if (!(token = line.GetToken(++index))) {
		return false;
	}

	if (!ParseTime(token, entry)) {
		return false;
	}

	// Get filename
	if (!(token = line.GetEndToken(index + 2))) {
		return false;
	}

	entry.name = token.get_view();
	if (token[token.size() - 1] == '/') {
		entry.name.pop_back();
		entry.flags |= CDirentry::flag_dir;
	}

	entry.ownerGroup = objcache.get(ownerGroupToken.get_view());
	entry.permissions = objcache.get(std::wstring_view());

	entry.time += m_timezoneOffset;

	return true;
}

bool CDirectoryListingParser::ParseOther(CLine &line, CDirentry &entry)
{
	int index = 0;
	CToken firstToken = line.GetToken(index);
	if (!firstToken) {
		return false;
	}

	if (!firstToken.IsNumeric()) {
		return false;
	}

	// Possible formats: Numerical unix, VShell or OS/2

	CToken token = line.GetToken(++index);
	if (!token) {
		return false;
	}

	entry.flags = 0;

	// If token is a number, than it's the numerical Unix style format,
	// else it's the VShell, OS/2 or nortel.VxWorks format
	if (token.IsNumeric()) {
		if (firstToken.size() >= 2 && firstToken[1] == '4') {
			entry.flags |= CDirentry::flag_dir;
		}

		auto ownerGroup = std::wstring(token.get_view());

		if (!(token = line.GetToken(++index))) {
			return false;
		}

		ownerGroup += ' ';
		ownerGroup += token.get_view();

		// Get size
		if (!(token = line.GetToken(++index))) {
			return false;
		}

		if (!token.IsNumeric()) {
			return false;
		}

		entry.size = token.GetNumber();

		// Get date/time
		if (!(token = line.GetToken(++index))) {
			return false;
		}

		int64_t number = token.GetNumber();
		if (number < 0) {
			return false;
		}
		entry.time = fz::datetime(static_cast<time_t>(number), fz::datetime::seconds);

		// Get filename
		if (!(token = line.GetEndToken(++index))) {
			return false;
		}

		entry.name = token.get_view();
		entry.target.clear();

		entry.permissions = objcache.get(firstToken.get_view());
		entry.ownerGroup = objcache.get(ownerGroup);
	}
	else {
		// Possible conflict with multiline VMS listings
		if (m_maybeMultilineVms) {
			return false;
		}

		// VShell, OS/2 or nortel.VxWorks style format
		entry.size = firstToken.GetNumber();

		// Get date
		std::wstring_view dateMonth = token.get_view();
		int month = 0;
		if (!GetMonthFromName(dateMonth, month)) {
			// OS/2 or nortel.VxWorks
			int skippedCount = 0;
			do {
				if (token.get_view() == L"DIR"sv) {
					entry.flags |= CDirentry::flag_dir;
				}
				else if (token.Find(L"-/.") != -1) {
					break;
				}

				++skippedCount;

				if (!(token = line.GetToken(++index))) {
					return false;
				}
			} while (true);

			if (!ParseShortDate(token, entry)) {
				return false;
			}

			// Get time
			if (!(token = line.GetToken(++index))) {
				return false;
			}

			if (!ParseTime(token, entry)) {
				return false;
			}

			// Get filename
			if (!(token = line.GetEndToken(++index))) {
				return false;
			}

			entry.name = token.get_view();
			if (entry.name.size() >= 5) {
				std::wstring type = fz::str_tolower_ascii(entry.name.substr(entry.name.size() - 5));
				if (!skippedCount && type == L"<dir>") {
					entry.flags |= CDirentry::flag_dir;
					entry.name = entry.name.substr(0, entry.name.size() - 5);
					while (!entry.name.empty() && entry.name.back() == ' ') {
						entry.name.pop_back();
					}
				}
			}
		}
		else {
			// Get day
			if (!(token = line.GetToken(++index))) {
				return false;
			}

			if (!token.IsNumeric() && !token.IsLeftNumeric()) {
				return false;
			}

			int64_t day = token.GetNumber();
			if (day < 0 || day > 31) {
				return false;
			}

			// Get Year
			if (!(token = line.GetToken(++index))) {
				return false;
			}

			if (!token.IsNumeric()) {
				return false;
			}

			int64_t year = token.GetNumber();
			if (year < 50) {
				year += 2000;
			}
			else if (year < 1000) {
				year += 1900;
			}

			if (!entry.time.set(fz::datetime::utc, year, month, day)) {
				return false;
			}

			// Get time
			if (!(token = line.GetToken(++index))) {
				return false;
			}

			if (!ParseTime(token, entry)) {
				return false;
			}

			// Get filename
			if (!(token = line.GetEndToken(++index))) {
				return false;
			}

			entry.name = token.get_view();
			auto const chr = token[token.size() - 1];
			if (chr == '/' || chr == '\\') {
				entry.flags |= CDirentry::flag_dir;
				entry.name.pop_back();
			}
		}
		entry.target.clear();
		entry.ownerGroup = objcache.get(std::wstring_view());
		entry.permissions = entry.ownerGroup;
		entry.time += m_timezoneOffset;
	}

	return true;
}

bool CDirectoryListingParser::ProcessAddedData()
{
	if (inbuf_.size() < parse_offset_) {
		return false;
	}

	// Need enough data to guess encoding
	if (m_listingEncoding == listingEncoding::unknown && inbuf_.size() < 512u) {
		return true;
	}

	return ParseData(true);
}

bool CDirectoryListingParser::AddLine(std::wstring && line, std::wstring && name, fz::datetime const& time, std::optional<uint64_t> const& size, std::optional<int> flags)
{
	if (line.empty() && name.empty()) {
		return true;
	}

	if (m_pControlSocket) {
		m_pControlSocket->log_raw(logmsg::listing, line.empty() ? name : line);
	}

	CDirentry override;
	override.name = std::move(name);
	override.time = time;
	if (flags) {
		override.flags = *flags;
	}
	if (!override.is_dir()) {
		override.size = (size && *size <= std::numeric_limits<int64_t>::max()) ? *size : -1;
	}
	CLine l(std::move(line));
	ParseLine(l, m_server.GetType(), true, &override);

	return true;
}

void CDirectoryListingParser::TrimLeadingWhitespace()
{
	if (parse_offset_) {
		return;
	}

	size_t i = 0;
	for (; i < inbuf_.size(); ++i) {
		auto c = inbuf_[i];
		if (c != '\r' && c != '\n' && c != ' ' && c != '\t' && c) {
			break;
		}
	}
	inbuf_.consume(i);
}

std::wstring CDirectoryListingParser::GetLine(bool breakAtEnd, bool &error)
{
	while (true) {
		TrimLeadingWhitespace();

		for (; parse_offset_ < inbuf_.size(); ++parse_offset_) {
			auto c  = inbuf_[parse_offset_];
			if (!c || c == '\n' || c == '\r') {
				break;
			}
		}

		if (parse_offset_ > 10000) {
			if (m_pControlSocket) {
				m_pControlSocket->log(logmsg::error, _("Received a line exceeding 10000 characters, aborting."));
			}
			error = true;
			return {};
		}

		if (parse_offset_ >= inbuf_.size()) {
			if (breakAtEnd || inbuf_.empty()) {
				return {};
			}
		}

		std::string_view raw_line = inbuf_.to_view().substr(0, parse_offset_);

		std::wstring buffer;
		if (m_pControlSocket) {
			buffer = m_pControlSocket->ConvToLocal(raw_line.data(), raw_line.size());
			m_pControlSocket->log_raw(logmsg::listing, buffer);
		}
		else {
			buffer = fz::to_wstring_from_utf8(raw_line);
			if (buffer.empty()) {
				buffer = fz::to_wstring(raw_line);
				if (buffer.empty()) {
					buffer = std::wstring(raw_line.data(), raw_line.data() + raw_line.size());
				}
			}
		}
		inbuf_.consume(parse_offset_);
		parse_offset_ = 0;

		// Strip BOM
		if (!buffer.empty() && buffer[0] == 0xfeff) {
			buffer = buffer.substr(1);
		}

		if (!buffer.empty()) {
			return buffer;
		}
	}

	return {};
}

bool CDirectoryListingParser::ParseAsWfFtp(CLine &line, CDirentry &entry)
{
	int index = 0;

	// Get filename
	CToken token = line.GetToken(index);
	if (!token) {
		return false;
	}

	entry.name = token.get_view();

	// Get filesize
	if (!(token = line.GetToken(++index))) {
		return false;
	}

	if (!token.IsNumeric()) {
		return false;
	}

	entry.size = token.GetNumber();

	entry.flags = 0;

	// Parse date
	if (!(token = line.GetToken(++index))) {
		return false;
	}

	if (!ParseShortDate(token, entry)) {
		return false;
	}

	// Unused token
	if (!(token = line.GetToken(++index))) {
		return false;
	}

	if (token.get_view().back() != '.') {
		return false;
	}

	// Parse time
	if (!(token = line.GetEndToken(++index))) {
		return false;
	}

	if (!ParseTime(token, entry)) {
		return false;
	}

	entry.ownerGroup = objcache.get(std::wstring_view());
	entry.permissions = entry.ownerGroup;
	entry.time += m_timezoneOffset;

	return true;
}

bool CDirectoryListingParser::ParseAsIBM_MVS(CLine &line, CDirentry &entry)
{
	int index = 0;
	CToken token;

	// volume
	if (!(token = line.GetToken(index++))) {
		return false;
	}

	// unit
	if (!(token = line.GetToken(index++))) {
		return false;
	}

	// Referred date
	if (!(token = line.GetToken(index++))) {
		return false;
	}

	entry.flags = 0;
	if (token.get_view() != L"**NONE**"sv && !ParseShortDate(token, entry)) {
		// Perhaps of the following type:
		// TSO004 3390 VSAM FOO.BAR
		if (token.get_view() != L"VSAM"sv) {
			return false;
		}

		if (!(token = line.GetToken(index++))) {
			return false;
		}

		entry.name = token.get_view();
		if (entry.name.find(' ') != std::wstring::npos) {
			return false;
		}

		entry.size = -1;
		entry.ownerGroup = objcache.get(std::wstring_view());
		entry.permissions = entry.ownerGroup;

		return true;
	}

	// ext
	if (!(token = line.GetToken(index++))) {
		return false;
	}
	if (!token.IsNumeric()) {
		return false;
	}

	int prevLen = token.size();

	// used
	if (!(token = line.GetToken(index++))) {
		return false;
	}
	if (token.IsNumeric() || token.get_view() == L"????"sv || token.get_view() == L"++++"sv) {
		// recfm
		if (!(token = line.GetToken(index++))) {
			return false;
		}
		if (token.IsNumeric()) {
			return false;
		}
	}
	else {
		if (prevLen < 6) {
			return false;
		}
	}

	// lrecl
	if (!(token = line.GetToken(index++))) {
		return false;
	}
	if (!token.IsNumeric()) {
		return false;
	}

	// blksize
	if (!(token = line.GetToken(index++))) {
		return false;
	}
	if (!token.IsNumeric()) {
		return false;
	}

	// dsorg
	if (!(token = line.GetToken(index++))) {
		return false;
	}

	if (token.get_view() == L"PO"sv || token.get_view() == L"PO-E"sv) {
		entry.flags |= CDirentry::flag_dir;
		entry.size = -1;
	}
	else {
		entry.size = 100;
	}

	// name of dataset or sequential file
	if (!(token = line.GetEndToken(index++))) {
		return false;
	}

	entry.name = token.get_view();

	entry.ownerGroup = objcache.get(std::wstring_view());
	entry.permissions = entry.ownerGroup;

	return true;
}

bool CDirectoryListingParser::ParseAsIBM_MVS_PDS(CLine &line, CDirentry &entry)
{
	int index = 0;
	CToken token = line.GetToken(index);
	if (!token) {
		return false;
	}

	// pds member name
	entry.name = token.get_view();

	// vv.mm
	if (!(token = line.GetToken(++index))) {
		return false;
	}

	entry.flags = 0;

	// creation date
	if (!(token = line.GetToken(++index))) {
		return false;
	}
	if (!ParseShortDate(token, entry)) {
		return false;
	}

	// modification date
	if (!(token = line.GetToken(++index))) {
		return false;
	}
	if (!ParseShortDate(token, entry)) {
		return false;
	}

	// modification time
	if (!(token = line.GetToken(++index))) {
		return false;
	}
	if (!ParseTime(token, entry)) {
		return false;
	}

	// size
	if (!(token = line.GetToken(++index))) {
		return false;
	}
	if (!token.IsNumeric()) {
		return false;
	}
	entry.size = token.GetNumber();

	// init
	if (!(token = line.GetToken(++index))) {
		return false;
	}
	if (!token.IsNumeric()) {
		return false;
	}

	// mod
	if (!(token = line.GetToken(++index))) {
		return false;
	}
	if (!token.IsNumeric()) {
		return false;
	}

	// id
	if (!(token = line.GetEndToken(++index))) {
		return false;
	}

	entry.ownerGroup = objcache.get(std::wstring_view());
	entry.permissions = entry.ownerGroup;
	entry.time += m_timezoneOffset;

	return true;
}

bool CDirectoryListingParser::ParseAsIBM_MVS_Migrated(CLine &line, CDirentry &entry)
{
	// Migrated MVS file
	// "Migrated				SOME.NAME"

	int index = 0;
	CToken token = line.GetToken(index);
	if (!token) {
		return false;
	}

	std::wstring s = fz::str_tolower_ascii(token.get_view());
	if (s != L"migrated"sv) {
		return false;
	}

	if (!(token = line.GetToken(++index))) {
		return false;
	}

	entry.name = token.get_view();

	if (line.GetToken(++index)) {
		return false;
	}

	entry.flags = 0;
	entry.size = -1;
	entry.ownerGroup = objcache.get(std::wstring_view());
	entry.permissions = entry.ownerGroup;

	return true;
}

bool CDirectoryListingParser::ParseAsIBM_MVS_PDS2(CLine &line, CDirentry &entry)
{
	int index = 0;
	CToken token = line.GetToken(index);
	if (!token) {
		return false;
	}

	entry.name = token.get_view();

	entry.flags = 0;
	entry.ownerGroup = objcache.get(std::wstring_view());
	entry.permissions = entry.ownerGroup;
	entry.size = -1;

	if (!(token = line.GetToken(++index))) {
		return true;
	}

	entry.size = token.GetNumber(CToken::hex);
	if (entry.size == -1) {
		return false;
	}

	// Unused hexadecimal token
	if (!(token = line.GetToken(++index))) {
		return false;
	}
	if (!token.IsNumeric(CToken::hex)) {
		return false;
	}

	// Unused numeric token
	if (!(token = line.GetToken(++index))) {
		return false;
	}
	if (!token.IsNumeric()) {
		return false;
	}

	int start = ++index;
	while (line.GetToken(index)) {
		++index;
	}
	if ((index - start < 2)) {
		return false;
	}
	--index;

	if (!(token = line.GetToken(index))) {
		return false;
	}
	if (!token.IsNumeric() && (token.get_view() != L"ANY"sv)) {
		return false;
	}

	if (!(token = line.GetToken(index - 1))) {
		return false;
	}
	if (!token.IsNumeric() && (token.get_view() != L"ANY"sv)) {
		return false;
	}

	for (int i = start; i < index - 1; ++i) {
		if (!(token = line.GetToken(i))) {
			return false;
		}
		size_t len = token.size();
		for (size_t j = 0; j < len; ++j) {
			if (token[j] < 'A' || token[j] > 'Z') {
				return false;
			}
		}
	}

	return true;
}

bool CDirectoryListingParser::ParseAsIBM_MVS_Tape(CLine &line, CDirentry &entry)
{
	int index = 0;
	CToken token;

	// volume
	if (!(token = line.GetToken(index))) {
		return false;
	}

	// unit
	if (!(token = line.GetToken(++index))) {
		return false;
	}

	std::wstring s = fz::str_tolower_ascii(token.get_view());
	if (s != L"tape"sv) {
		return false;
	}

	// dsname
	if (!(token = line.GetToken(++index))) {
		return false;
	}

	entry.name = token.get_view();
	entry.flags = 0;
	entry.ownerGroup = objcache.get(std::wstring_view());
	entry.permissions = objcache.get(std::wstring_view());
	entry.size = -1;

	if (line.GetToken(++index)) {
		return false;
	}

	return true;
}

bool CDirectoryListingParser::ParseComplexFileSize(CToken& token, int64_t& size, int blocksize /*=-1*/)
{
	if (token.IsNumeric()) {
		size = token.GetNumber();
		if (blocksize > 0) {
			if (size > std::numeric_limits<std::decay_t<decltype(size)>>::max() / blocksize) {
				return false;
			}
			size *= blocksize;
		}

		return true;
	}

	size_t len = token.size();
	if (!len) {
		return false;
	}

	auto last = token[len - 1];
	if (last == 'B' || last == 'b') {
		if (len < 2) {
			return false;
		}

		auto const c = token[--len - 1];
		if (c < '0' || c > '9') {
			--len;
			last = c;
		}
		else {
			last = 0;
		}
	}
	else if (last >= '0' && last <= '9') {
		last = 0;
	}
	else {
		if (--len == 0) {
			return false;
		}
	}

	size = 0;

	size_t dot{};
	for (size_t i = 0; i < len; ++i) {
		auto const c = token[i];
		if (c >= '0' && c <= '9') {
			if (size > std::numeric_limits<std::decay_t<decltype(size)>>::max() / 10) {
				return false;
			}
			size *= 10;

			auto digit = c - '0';
			if (std::numeric_limits<std::decay_t<decltype(size)>>::max() - digit < size) {
				return false;
			}
			size += digit;
		}
		else if (c == '.') {
			if (!i || i + 1 == len || dot) {
				return false;
			}
			dot = len - i;
		}
		else {
			return false;
		}
	}

	int64_t mult{};
	switch (last)
	{
	case 'k':
	case 'K':
		mult = 1024;
		break;
	case 'm':
	case 'M':
		mult = 1024 * 1024;
		break;
	case 'g':
	case 'G':
		mult = 1024 * 1024 * 1024;
		break;
	case 't':
	case 'T':
		mult = 1024 * 1024 * 1024 * 1024ll;
		break;
	case 'b':
	case 'B':
		break;
	case 0:
		if (blocksize > 0) {
			mult = blocksize;
		}
		break;
	default:
		return false;
	}
	if (mult) {
		if (size > std::numeric_limits<std::decay_t<decltype(size)>>::max() / mult) {
			return false;
		}
		size *= mult;
	}
	if (dot) {
		while (--dot) {
			size /= 10;
		}
	}

	return true;
}

int CDirectoryListingParser::ParseAsMlsd(CLine &line, CDirentry &entry)
{
	// MLSD format as described here: http://www.ietf.org/internet-drafts/draft-ietf-ftpext-mlst-16.txt

	// Parsing is done strict, abort on slightest error.

	CToken token = line.GetToken(0);
	if (!token) {
		return 0;
	}

	std::wstring_view const facts = token.get_view();
	if (facts.empty()) {
		return 0;
	}

	entry.flags = 0;
	entry.size = -1;
	entry.time.clear();
	entry.target.clear();

	std::wstring_view owner, ownername, group, groupname, user, uid, gid;
	std::wstring ownerGroup;
	std::wstring permissions;

	size_t start = 0;
	while (start < facts.size()) {
		auto delim = facts.find(';', start);
		if (delim == std::wstring::npos) {
			delim = facts.size();
		}
		else if (delim < start + 3) {
			return 0;
		}

		auto const pos = facts.find('=', start);
		if (pos == std::wstring::npos || pos < start + 1 || pos > delim) {
			return 0;
		}

		std::wstring factname = fz::str_tolower_ascii(facts.substr(start, pos - start));
		std::wstring_view value = facts.substr(pos + 1, delim - pos - 1);
		if (factname == L"type"sv) {
			auto colonPos = value.find(':');
			std::wstring valuePrefix;
			if (colonPos == std::wstring::npos) {
				valuePrefix = fz::str_tolower_ascii(value);
			}
			else {
				valuePrefix = fz::str_tolower_ascii(value.substr(0, colonPos));
			}

			if (valuePrefix == L"dir" && colonPos == std::wstring::npos) {
				entry.flags |= CDirentry::flag_dir;
			}
			else if (valuePrefix == L"os.unix=slink"sv || valuePrefix == L"os.unix=symlink"sv) {
				// Sadly we can't distinguish between symlinks to links and dirs, they appear the same in listings. Handled instead via FZ_REPLY_LINKNOTDIR
				entry.flags |= CDirentry::flag_dir | CDirentry::flag_link;
				if (colonPos != std::wstring::npos) {
					std::wstring_view target = value.substr(colonPos + 1);
					entry.target = fz::sparse_optional<std::wstring>(std::wstring(target.begin(), target.end()));
				}
			}
			else if ((valuePrefix == L"cdir"sv || valuePrefix == L"pdir"sv) && colonPos == std::wstring::npos) {
				// Current and parent directory, don't parse it
				return 2;
			}
		}
		else if (factname == L"size"sv) {
			entry.size = CToken::GetNumber(value, CToken::decimal, true);
			if (entry.size < 0) {
				return 0;
			}
		}
		else if (factname == L"modify"sv ||
			(!entry.has_date() && factname == L"create"sv))
		{
			entry.time = fz::datetime(value, fz::datetime::utc);
			if (entry.time.empty()) {
				return 0;
			}
		}
		else if (factname == L"perm"sv) {
			if (!value.empty()) {
				if (!permissions.empty()) {
					std::wstring tmp;
					tmp = value;
					tmp += L" (";
					tmp += permissions;
					tmp += L")";
					permissions = std::move(tmp);
				}
				else {
					permissions = value;
				}
			}
		}
		else if (factname == L"unix.mode"sv) {
			if (!permissions.empty()) {
				permissions += L" (";
				permissions += value;
				permissions += L")";
			}
			else {
				permissions = value;
			}
		}
		else if (factname == L"unix.owner"sv) {
			owner = value;
		}
		else if (factname == L"unix.ownername"sv) {
			ownername = value;
		}
		else if (factname == L"unix.group"sv) {
			group = value;
		}
		else if (factname == L"unix.groupname"sv) {
			groupname = value;
		}
		else if (factname == L"unix.user"sv) {
			user = value;
		}
		else if (factname == L"unix.uid"sv) {
			uid = value;
		}
		else if (factname == L"unix.gid"sv) {
			gid = value;
		}

		start = delim + 1;
	}

	// The order of the facts is undefined, so assemble ownerGroup in correct
	// order
	if (!ownername.empty()) {
		ownerGroup = ownername;
	}
	else if (!owner.empty()) {
		ownerGroup = owner;
	}
	else if (!user.empty()) {
		ownerGroup = user;
	}
	else if (!uid.empty()) {
		ownerGroup = uid;
	}

	if (!groupname.empty()) {
		ownerGroup += ' ';
		ownerGroup += groupname;
	}
	else if (!group.empty()) {
		ownerGroup += ' ';
		ownerGroup += group;
	}
	else if (!gid.empty()) {
		ownerGroup += ' ';
		ownerGroup += gid;
	}

	CToken nameToken = line.GetEndToken(1, true);
	if (!nameToken) {
		return 0;
	}

	entry.name = nameToken.get_view();
	entry.ownerGroup = objcache.get(std::move(ownerGroup));
	entry.permissions = objcache.get(std::move(permissions));

	return 1;
}

bool CDirectoryListingParser::ParseAsOS9(CLine &line, CDirentry &entry)
{
	int index = 0;

	// Get owner
	CToken ownerGroupToken = line.GetToken(index++);
	if (!ownerGroupToken) {
		return false;
	}

	// Make sure it's number.number
	int pos = ownerGroupToken.Find('.');
	if (pos == -1 || !pos || pos == ((int)ownerGroupToken.size() - 1)) {
		return false;
	}

	if (!ownerGroupToken.IsNumeric(0, pos)) {
		return false;
	}

	if (!ownerGroupToken.IsNumeric(pos + 1, ownerGroupToken.size() - pos - 1)) {
		return false;
	}

	entry.flags = 0;

	// Get date
	CToken token;
	if (!(token = line.GetToken(index++))) {
		return false;
	}

	if (!ParseShortDate(token, entry, true)) {
		return false;
	}

	// Unused token
	if (!(token = line.GetToken(index++))) {
		return false;
	}

	// Get perms
	CToken permToken = line.GetToken(index++);
	if (!permToken) {
		return false;
	}

	if (permToken[0] == 'd') {
		entry.flags |= CDirentry::flag_dir;
	}

	// Unused token
	if (!(token = line.GetToken(index++))) {
		return false;
	}

	// Get Size
	if (!(token = line.GetToken(index++))) {
		return false;
	}

	if (!token.IsNumeric()) {
		return false;
	}

	entry.size = token.GetNumber();

	// Filename
	if (!(token = line.GetEndToken(index++))) {
		return false;
	}

	entry.name = token.get_view();
	entry.ownerGroup = objcache.get(ownerGroupToken.get_view());
	entry.permissions = objcache.get(permToken.get_view());

	return true;
}

void CDirectoryListingParser::Reset()
{
	inbuf_.clear();
	parse_offset_ = 0;
	converted_ = 0;
	prevLine_.clear();

	entries_.clear();
	m_fileList.clear();
	m_fileListOnly = true;
	m_maybeMultilineVms = false;
	truncated_ = false;

	// Keep the deduced encoding and m_timezoneOffset, this isn't changing between listings
}

bool CDirectoryListingParser::ParseAsZVM(CLine &line, CDirentry &entry)
{
	int index = 0;
	// Get name
	CToken token = line.GetToken(index);
	if (!token) {
		return false;
	}

	entry.name = token.get_view();

	// Get filename extension
	if (!(token = line.GetToken(++index))) {
		return false;
	}
	entry.name += '.';
	entry.name += token.get_view();

	// File format. Unused
	if (!(token = line.GetToken(++index)))
		return false;
	std::wstring_view format = token.get_view();
	if (format != L"V"sv && format != L"F"sv) {
		return false;
	}

	// Record length
	if (!(token = line.GetToken(++index))) {
		return false;
	}

	if (!token.IsNumeric()) {
		return false;
	}

	entry.size = token.GetNumber();

	// Number of records
	if (!(token = line.GetToken(++index))) {
		return false;
	}

	if (!token.IsNumeric()) {
		return false;
	}

	int64_t records = token.GetNumber();
	if (entry.size > 0) {
		if (records < 0 || std::numeric_limits<decltype(entry.size)>::max() / entry.size < records) {
			return false;
		}
		entry.size *= records;
	}

	// Unused (Block size?)
	if (!(token = line.GetToken(++index))) {
		return false;
	}

	if (!token.IsNumeric()) {
		return false;
	}

	entry.flags = 0;

	// Date
	if (!(token = line.GetToken(++index))) {
		return false;
	}

	if (!ParseShortDate(token, entry, true)) {
		return false;
	}

	// Time
	if (!(token = line.GetToken(++index))) {
		return false;
	}

	if (!ParseTime(token, entry)) {
		return false;
	}

	// Owner
	CToken ownerGroupToken = line.GetToken(++index);
	if (!ownerGroupToken) {
		return false;
	}

	// No further token!
	if (line.GetToken(++index)) {
		return false;
	}

	entry.ownerGroup = objcache.get(ownerGroupToken.get_view());
	entry.permissions = objcache.get(std::wstring_view());
	entry.target.clear();
	entry.time += m_timezoneOffset;

	return true;
}

bool CDirectoryListingParser::ParseAsHPNonstop(CLine &line, CDirentry &entry)
{
	int index = 0;

	// Get name
	CToken token = line.GetToken(index);
	if (!token) {
		return false;
	}

	entry.name = token.get_view();

	// File code, numeric, unused
	if (!(token = line.GetToken(++index))) {
		return false;
	}
	if (!token.IsNumeric()) {
		return false;
	}

	// Size
	if (!(token = line.GetToken(++index))) {
		return false;
	}
	if (!token.IsNumeric()) {
		return false;
	}

	entry.size = token.GetNumber();

	entry.flags = 0;

	// Date
	if (!(token = line.GetToken(++index))) {
		return false;
	}
	if (!ParseShortDate(token, entry, false)) {
		return false;
	}

	// Time
	if (!(token = line.GetToken(++index))) {
		return false;
	}
	if (!ParseTime(token, entry)) {
		return false;
	}

	// Owner
	if (!(token = line.GetToken(++index))) {
		return false;
	}
	auto ownerGroup = std::wstring(token.get_view());

	if (token[token.size() - 1] == ',') {
		// Owner, part 2
		if (!(token = line.GetToken(++index))) {
			return false;
		}
		ownerGroup += ' ';
		ownerGroup += token.get_view();
	}

	// Permissions
	CToken permToken = line.GetToken(++index);
	if (!permToken) {
		return false;
	}

	// Nothing
	if (line.GetToken(++index)) {
		return false;
	}

	entry.permissions = objcache.get(permToken.get_view());
	entry.ownerGroup = objcache.get(ownerGroup);

	return true;
}

bool CDirectoryListingParser::GetMonthFromName(std::wstring_view const& name, int &month)
{
	std::wstring lower = fz::str_tolower_ascii(name);

	static auto const monthNamesMap = [](){
		std::map<std::wstring, int> monthNamesMap;
		//Fill the month names map

		//English month names
		monthNamesMap[L"jan"] = 1;
		monthNamesMap[L"feb"] = 2;
		monthNamesMap[L"mar"] = 3;
		monthNamesMap[L"apr"] = 4;
		monthNamesMap[L"may"] = 5;
		monthNamesMap[L"jun"] = 6;
		monthNamesMap[L"june"] = 6;
		monthNamesMap[L"jul"] = 7;
		monthNamesMap[L"july"] = 7;
		monthNamesMap[L"aug"] = 8;
		monthNamesMap[L"sep"] = 9;
		monthNamesMap[L"sept"] = 9;
		monthNamesMap[L"oct"] = 10;
		monthNamesMap[L"nov"] = 11;
		monthNamesMap[L"dec"] = 12;

		//Numerical values for the month
		monthNamesMap[L"1"] = 1;
		monthNamesMap[L"01"] = 1;
		monthNamesMap[L"2"] = 2;
		monthNamesMap[L"02"] = 2;
		monthNamesMap[L"3"] = 3;
		monthNamesMap[L"03"] = 3;
		monthNamesMap[L"4"] = 4;
		monthNamesMap[L"04"] = 4;
		monthNamesMap[L"5"] = 5;
		monthNamesMap[L"05"] = 5;
		monthNamesMap[L"6"] = 6;
		monthNamesMap[L"06"] = 6;
		monthNamesMap[L"7"] = 7;
		monthNamesMap[L"07"] = 7;
		monthNamesMap[L"8"] = 8;
		monthNamesMap[L"08"] = 8;
		monthNamesMap[L"9"] = 9;
		monthNamesMap[L"09"] = 9;
		monthNamesMap[L"10"] = 10;
		monthNamesMap[L"11"] = 11;
		monthNamesMap[L"12"] = 12;

		//German month names
		monthNamesMap[L"mrz"] = 3;
		monthNamesMap[L"m\xe4r"] = 3;
		monthNamesMap[L"m\xe4rz"] = 3;
		monthNamesMap[L"mai"] = 5;
		monthNamesMap[L"juni"] = 6;
		monthNamesMap[L"juli"] = 7;
		monthNamesMap[L"okt"] = 10;
		monthNamesMap[L"dez"] = 12;

		//Austrian month names
		monthNamesMap[L"j\xe4n"] = 1;

		//French month names
		monthNamesMap[L"janv"] = 1;
		monthNamesMap[L"f\xe9" L"b"] = 2;
		monthNamesMap[L"f\xe9v"] = 2;
		monthNamesMap[L"fev"] = 2;
		monthNamesMap[L"f\xe9vr"] = 2;
		monthNamesMap[L"fevr"] = 2;
		monthNamesMap[L"mars"] = 3;
		monthNamesMap[L"mrs"] = 3;
		monthNamesMap[L"avr"] = 4;
		monthNamesMap[L"avril"] = 4;
		monthNamesMap[L"juin"] = 6;
		monthNamesMap[L"juil"] = 7;
		monthNamesMap[L"jui"] = 7;
		monthNamesMap[L"ao\xfb"] = 8;
		monthNamesMap[L"ao\xfbt"] = 8;
		monthNamesMap[L"aout"] = 8;
		monthNamesMap[L"d\xe9" L"c"] = 12;
		monthNamesMap[L"dec"] = 12;

		//Italian month names
		monthNamesMap[L"gen"] = 1;
		monthNamesMap[L"mag"] = 5;
		monthNamesMap[L"giu"] = 6;
		monthNamesMap[L"lug"] = 7;
		monthNamesMap[L"ago"] = 8;
		monthNamesMap[L"set"] = 9;
		monthNamesMap[L"ott"] = 10;
		monthNamesMap[L"dic"] = 12;

		//Spanish month names
		monthNamesMap[L"ene"] = 1;
		monthNamesMap[L"fbro"] = 2;
		monthNamesMap[L"mzo"] = 3;
		monthNamesMap[L"ab"] = 4;
		monthNamesMap[L"abr"] = 4;
		monthNamesMap[L"agto"] = 8;
		monthNamesMap[L"sbre"] = 9;
		monthNamesMap[L"obre"] = 10;
		monthNamesMap[L"nbre"] = 11;
		monthNamesMap[L"dbre"] = 12;

		//Polish month names
		monthNamesMap[L"sty"] = 1;
		monthNamesMap[L"lut"] = 2;
		monthNamesMap[L"kwi"] = 4;
		monthNamesMap[L"maj"] = 5;
		monthNamesMap[L"cze"] = 6;
		monthNamesMap[L"lip"] = 7;
		monthNamesMap[L"sie"] = 8;
		monthNamesMap[L"wrz"] = 9;
		monthNamesMap[L"pa\x9f"] = 10;
		monthNamesMap[L"pa\xbc"] = 10; // ISO-8859-2
		monthNamesMap[L"paz"] = 10; // ASCII
		monthNamesMap[L"pa\xc5\xba"] = 10; // UTF-8
		monthNamesMap[L"pa\x017a"] = 10; // some servers send this
		monthNamesMap[L"lis"] = 11;
		monthNamesMap[L"gru"] = 12;

		//Russian month names
		monthNamesMap[L"\xff\xed\xe2"] = 1;
		monthNamesMap[L"\xf4\xe5\xe2"] = 2;
		monthNamesMap[L"\xec\xe0\xf0"] = 3;
		monthNamesMap[L"\xe0\xef\xf0"] = 4;
		monthNamesMap[L"\xec\xe0\xe9"] = 5;
		monthNamesMap[L"\xe8\xfe\xed"] = 6;
		monthNamesMap[L"\xe8\xfe\xeb"] = 7;
		monthNamesMap[L"\xe0\xe2\xe3"] = 8;
		monthNamesMap[L"\xf1\xe5\xed"] = 9;
		monthNamesMap[L"\xee\xea\xf2"] = 10;
		monthNamesMap[L"\xed\xee\xff"] = 11;
		monthNamesMap[L"\xe4\xe5\xea"] = 12;

		//Dutch month names
		monthNamesMap[L"mrt"] = 3;
		monthNamesMap[L"mei"] = 5;

		//Portuguese month names
		monthNamesMap[L"out"] = 10;

		//Finnish month names
		monthNamesMap[L"tammi"] = 1;
		monthNamesMap[L"helmi"] = 2;
		monthNamesMap[L"maalis"] = 3;
		monthNamesMap[L"huhti"] = 4;
		monthNamesMap[L"touko"] = 5;
		monthNamesMap[L"kes\xe4"] = 6;
		monthNamesMap[L"hein\xe4"] = 7;
		monthNamesMap[L"elo"] = 8;
		monthNamesMap[L"syys"] = 9;
		monthNamesMap[L"loka"] = 10;
		monthNamesMap[L"marras"] = 11;
		monthNamesMap[L"joulu"] = 12;

		//Slovenian month names
		monthNamesMap[L"avg"] = 8;

		//Icelandic
		monthNamesMap[L"ma\x00ed"] = 5;
		monthNamesMap[L"j\x00fan"] = 6;
		monthNamesMap[L"j\x00fal"] = 7;
		monthNamesMap[L"\x00e1g"] = 8;
		monthNamesMap[L"n\x00f3v"] = 11;
		monthNamesMap[L"des"] = 12;

		//Lithuanian
		monthNamesMap[L"sau"] = 1;
		monthNamesMap[L"vas"] = 2;
		monthNamesMap[L"kov"] = 3;
		monthNamesMap[L"bal"] = 4;
		monthNamesMap[L"geg"] = 5;
		monthNamesMap[L"bir"] = 6;
		monthNamesMap[L"lie"] = 7;
		monthNamesMap[L"rgp"] = 8;
		monthNamesMap[L"rgs"] = 9;
		monthNamesMap[L"spa"] = 10;
		monthNamesMap[L"lap"] = 11;
		monthNamesMap[L"grd"] = 12;

		// Hungarian
		monthNamesMap[L"szept"] = 9;

		//There are more languages and thus month
		//names, but as long as nobody reports a
		//problem, I won't add them, there are way
		//too many languages

		// Some servers send a combination of month name and number,
		// Add corresponding numbers to the month names.
		std::map<std::wstring, int> combo;
		for (auto iter = monthNamesMap.begin(); iter != monthNamesMap.end(); ++iter) {
			// January could be 1 or 0, depends how the server counts
			combo[fz::sprintf(L"%s%02d", iter->first, iter->second)] = iter->second;
			combo[fz::sprintf(L"%s%02d", iter->first, iter->second - 1)] = iter->second;
			if (iter->second < 10) {
				combo[fz::sprintf(L"%s%d", iter->first, iter->second)] = iter->second;
			}
			else {
				combo[fz::sprintf(L"%s%d", iter->first, iter->second % 10)] = iter->second;
			}
			if (iter->second <= 10) {
				combo[fz::sprintf(L"%s%d", iter->first, iter->second - 1)] = iter->second;
			}
			else {
				combo[fz::sprintf(L"%s%d", iter->first, (iter->second - 1) % 10)] = iter->second;
			}
		}
		monthNamesMap.insert(combo.begin(), combo.end());

		monthNamesMap[L"1"] = 1;
		monthNamesMap[L"2"] = 2;
		monthNamesMap[L"3"] = 3;
		monthNamesMap[L"4"] = 4;
		monthNamesMap[L"5"] = 5;
		monthNamesMap[L"6"] = 6;
		monthNamesMap[L"7"] = 7;
		monthNamesMap[L"8"] = 8;
		monthNamesMap[L"9"] = 9;
		monthNamesMap[L"10"] = 10;
		monthNamesMap[L"11"] = 11;
		monthNamesMap[L"12"] = 12;

		return monthNamesMap;
	}();

	auto iter = monthNamesMap.find(lower);
	if (iter == monthNamesMap.end()) {
		return false;
	}

	month = iter->second;

	return true;
}

unsigned char const ebcdic_table[256] = {
	' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  // 0
	' ',  ' ',  ' ',  ' ',  ' ',  '\n', ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  '\n', // 1
	' ',  ' ',  ' ',  ' ',  ' ',  '\n', ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  // 2
	' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  // 3
	' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  '.',  '<',  '(',  '+',  '|',  // 4
	'&',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  '!',  '$',  '*',  ')',  ';',  ' ',  // 5
	'-',  '/',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  '|',  ',',  '%',  '_',  '>',  '?',  // 6
	' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  '`',  ':',  '#',  '@',  '\'', '=',  '"',  // 7
	' ',  'a',  'b',  'c',  'd',  'e',  'f',  'g',  'h',  'i',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  // 8
	' ',  'j',  'k',  'l',  'm',  'n',  'o',  'p',  'q',  'r',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  // 9
	' ',  '~',  's',  't',  'u',  'v',  'w',  'x',  'y',  'z',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  // a
	'^',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  '[',  ']',  ' ',  ' ',  ' ',  ' ',  // b
	'{',  'A',  'B',  'C',  'D',  'E',  'F',  'G',  'H',  'I',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  // c
	'}',  'J',  'K',  'L',  'M',  'N',  'O',  'P',  'Q',  'R',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  // d
	'\\', ' ',  'S',  'T',  'U',  'V',  'W',  'X',  'Y',  'Z',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  // e
	'0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',  ' ',  ' ',  ' ',  ' ',  ' ',  ' '   // f
};

void CDirectoryListingParser::ConvertEncoding()
{
	if (m_listingEncoding == listingEncoding::unknown) {
		DeduceEncoding();
	}
	if (m_listingEncoding != listingEncoding::ebcdic) {
		return;
	}

	for (size_t i = converted_; i < inbuf_.size(); ++i) {
		inbuf_[i] = ebcdic_table[inbuf_[i]];
	}
	converted_ = inbuf_.size();
}

void CDirectoryListingParser::DeduceEncoding()
{
	if (m_listingEncoding != listingEncoding::unknown) {
		return;
	}

	std::array<size_t, 256> count{};
	for (auto const& c : inbuf_.to_view().substr(0, 50000)) {
		++count[static_cast<unsigned char>(c)];
	}

	int count_normal = 0;
	int count_ebcdic = 0;
	for (int i = '0'; i <= '9'; ++i) {
		count_normal += count[i];
	}
	for (int i = 'a'; i <= 'z'; ++i) {
		count_normal += count[i];
	}
	for (int i = 'A'; i <= 'Z'; ++i) {
		count_normal += count[i];
	}

	for (int i = 0x81; i <= 0x89; ++i) {
		count_ebcdic += count[i];
	}
	for (int i = 0x91; i <= 0x99; ++i) {
		count_ebcdic += count[i];
	}
	for (int i = 0xa2; i <= 0xa9; ++i) {
		count_ebcdic += count[i];
	}
	for (int i = 0xc1; i <= 0xc9; ++i) {
		count_ebcdic += count[i];
	}
	for (int i = 0xd1; i <= 0xd9; ++i) {
		count_ebcdic += count[i];
	}
	for (int i = 0xe2; i <= 0xe9; ++i) {
		count_ebcdic += count[i];
	}
	for (int i = 0xf0; i <= 0xf9; ++i) {
		count_ebcdic += count[i];
	}


	if ((count[0x1f] || count[0x15] || count[0x25]) && !count[0x0a] && count[static_cast<unsigned char>('@')] && count[static_cast<unsigned char>('@')] > count[static_cast<unsigned char>(' ')] && count_ebcdic > count_normal) {
		if (m_pControlSocket) {
			m_pControlSocket->log(logmsg::status, _("Received a directory listing which appears to be encoded in EBCDIC."));
		}
		m_listingEncoding = listingEncoding::ebcdic;
	}
	else {
		m_listingEncoding = listingEncoding::normal;
	}
}
