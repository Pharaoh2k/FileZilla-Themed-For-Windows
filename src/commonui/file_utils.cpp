#include "file_utils.h"

#include <libfilezilla/format.hpp>
#include <libfilezilla/string.hpp>

std::wstring GetAsURL(std::wstring const& dir)
{
	// Cheap URL encode
	std::string utf8 = fz::to_utf8(dir);

	std::wstring encoded;
	encoded.reserve(utf8.size());

	char const* p = utf8.c_str();
	while (*p) {
		// List of characters that don't need to be escaped taken
		// from the BNF grammar in RFC 1738
		// Again attention seeking Windows wants special treatment...
		unsigned char const c = static_cast<unsigned char>(*p++);
		if ((c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') ||
			c == '$' ||
			c == '_' ||
			c == '-' ||
			c == '.' ||
			c == '+' ||
			c == '!' ||
			c == '*' ||
#ifndef FZ_WINDOWS
			c == '\'' ||
#endif
			c == '(' ||
			c == ')' ||
			c == ',' ||
			c == '?' ||
			c == ':' ||
			c == '@' ||
			c == '&' ||
			c == '=' ||
			c == '/')
		{
			encoded += c;
		}
#ifdef FZ_WINDOWS
		else if (c == '\\') {
			encoded += '/';
		}
#endif
		else {
			encoded += fz::sprintf(L"%%%x", c);
		}
	}
#ifdef FZ_WINDOWS
	if (fz::starts_with(encoded, std::wstring(L"//"))) {
		// UNC path
		encoded = encoded.substr(2);
	}
	else {
		encoded = L"/" + encoded;
	}
#endif
	return L"file://" + encoded;
}

std::wstring QuoteCommand(std::vector<std::wstring> const& cmd_with_args)
{
	std::wstring ret;

	for (auto const& arg : cmd_with_args) {
		if (!ret.empty()) {
			ret += ' ';
		}
		size_t pos = arg.find_first_of(L" \t\"'");
		if (pos != std::wstring::npos || arg.empty()) {
			ret += '"';
			ret += fz::replaced_substrings(arg, L"\"", L"\"\"");
			ret += '"';
		}
		else {
			ret += arg;
		}
	}

	return ret;
}

std::optional<std::wstring> UnquoteFirst(std::wstring_view & command)
{
	std::optional<std::wstring> ret;

	bool quoted{};
	size_t i = 0;
	for (; i < command.size(); ++i) {
		wchar_t const& c = command[i];

		if ((c == ' ' || c == '\t' || c == '\r' || c == '\n') && !quoted) {
			if (ret) {
				break;
			}
		}
		else {
			if (!ret) {
				ret = std::wstring();
			}
			if (c == '"') {
				if (!quoted) {
					quoted = true;
				}
				else if (i + 1 != command.size() && command[i + 1] == '"') {
					*ret += '"';
					++i;
				}
				else {
					quoted = false;
				}
			}
			else {
				ret->push_back(c);
			}
		}
	}
	if (quoted) {
		ret.reset();
	}

	if (ret) {
		for (; i < command.size(); ++i) {
			wchar_t const& c = command[i];
			if (!(c == ' ' || c == '\t' || c == '\r' || c == '\n')) {
				break;
			}
		}
		command = command.substr(i);
	}

	return ret;
}

std::vector<std::wstring> UnquoteCommand(std::wstring_view command)
{
	std::vector<std::wstring> ret;

	while (!command.empty()) {
		auto part = UnquoteFirst(command);
		if (!part) {
			break;
		}

		ret.emplace_back(std::move(*part));
	}

	if (!command.empty()) {
		ret.clear();
	}

	if (!ret.empty() && ret.front().empty()) {
		// Commands may have empty arguments, but themselves cannot be empty
		ret.clear();
	}

	return ret;
}

std::wstring GetExtension(std::wstring_view file)
{
	// Strip path if any
#ifdef FZ_WINDOWS
	size_t pos = file.find_last_of(L"/\\");
#else
	size_t pos = file.find_last_of(L"/");
#endif
	if (pos != std::wstring::npos) {
		file = file.substr(pos + 1);
	}

	// Find extension
	pos = file.find_last_of('.');
	if (!pos) {
		return std::wstring(L".");
	}
	else if (pos != std::wstring::npos) {
		return std::wstring(file.substr(pos + 1));
	}

	return std::wstring();
}

bool IsInvalidChar(wchar_t c, bool includeQuotesAndBreaks)
{
	switch (c)
	{
	case '/':
#ifdef FZ_WINDOWS
	case '\\':
	case ':':
	case '*':
	case '?':
	case '"':
	case '<':
	case '>':
	case '|':
#endif
		return true;


	case '\'':
#ifndef FZ_WINDOWS
	case '"':
	case '\\':
#endif
		return includeQuotesAndBreaks;

	default:
		if (c < 0x20) {
#ifdef FZ_WINDOWS
			return true;
#else
			return includeQuotesAndBreaks;
#endif
		}
		return false;
	}
}
