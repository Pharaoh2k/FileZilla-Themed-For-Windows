#ifndef FILEZILLA_ENGINE_VERSION_HEADER
#define FILEZILLA_ENGINE_VERSION_HEADER

#include "visibility.h"

#include <string>

enum class lib_dependency
{
	gnutls,
	count
};

std::wstring FZC_PUBLIC_SYMBOL GetDependencyName(lib_dependency d);
std::wstring FZC_PUBLIC_SYMBOL GetDependencyVersion(lib_dependency d);

std::wstring FZC_PUBLIC_SYMBOL GetFileZillaEngineVersion();

int64_t FZC_PUBLIC_SYMBOL ConvertToVersionNumber(wchar_t const* version);

struct FZC_PUBLIC_SYMBOL SystemVersion
{
	unsigned int major{};
	unsigned int minor{};

	explicit operator bool() const { return major != 0; }
};
SystemVersion FZC_PUBLIC_SYMBOL GetSystemVersion();

#endif
