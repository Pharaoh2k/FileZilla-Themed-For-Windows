#ifndef FILEZILLA_COMMONUI_FILE_UTILS_HEADER
#define FILEZILLA_COMMONUI_FILE_UTILS_HEADER

#include "visibility.h"

#include <optional>
#include <string>
#include <vector>

// Quotation rules:
// - Args containing spaces double-quotes need to be quotes by enclosing in double-quotes.
// - If an arg is quoted, contained double-quotes are doubled
//
// - Example: "foo""bar" is the quoted representation of foo"bar
std::wstring FZCUI_PUBLIC_SYMBOL QuoteCommand(std::vector<std::wstring> const& cmd_with_args);
std::vector<std::wstring> FZCUI_PUBLIC_SYMBOL UnquoteCommand(std::wstring_view command);

// Extracts the first argument and returns it unquoted. Removes it from the passed view.
std::optional<std::wstring> FZCUI_PUBLIC_SYMBOL UnquoteFirst(std::wstring_view & command);

// Returns a file:// URL
std::wstring FZCUI_PUBLIC_SYMBOL GetAsURL(std::wstring const& dir);

std::wstring FZCUI_PUBLIC_SYMBOL GetExtension(std::wstring_view file);

bool FZCUI_PUBLIC_SYMBOL IsInvalidChar(wchar_t c, bool includeQuotesAndBreaks = false);

#endif
