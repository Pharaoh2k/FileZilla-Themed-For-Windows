#ifndef FILEZILLA_COMMONUI_BUILDINFO_HEADER
#define FILEZILLA_COMMONUI_BUILDINFO_HEADER

#include <string>
#include <libfilezilla/time.hpp>

#include "visibility.h"

std::wstring FZCUI_PUBLIC_SYMBOL GetFileZillaVersion();

class FZCUI_PUBLIC_SYMBOL CBuildInfo final
{
public:
	CBuildInfo() = delete;

public:
	static std::wstring GetBuildDateString();
	static std::wstring GetBuildTimeString();
	static fz::datetime GetBuildDate();
	static std::wstring GetBuildType();
	static std::wstring GetCompiler();
	static std::wstring GetCompilerFlags();
	static std::wstring GetHostname();
	static std::wstring GetBuildSystem();
	static bool IsUnstable(); // Returns true on beta or rc releases.
	static std::wstring GetCPUCaps(char separator = ',');
};

#endif
