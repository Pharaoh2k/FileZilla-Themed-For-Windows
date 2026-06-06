#include "filezilla.h"

#include "../include/version.h"

#include <libfilezilla/tls_layer.hpp>

#if FZ_WINDOWS
#include <libfilezilla/glue/windows.hpp>
#elif FZ_UNIX
#include <sys/utsname.h>
#else
#include <Carbon/Carbon.h>
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

std::wstring GetDependencyVersion(lib_dependency d)
{
	switch (d) {
	case lib_dependency::gnutls:
		return fz::to_wstring(fz::tls_layer::get_gnutls_version());
	default:
		return std::wstring();
	}
}

std::wstring GetDependencyName(lib_dependency d)
{
	switch (d) {
	case lib_dependency::gnutls:
		return L"GnuTLS";
	default:
		return std::wstring();
	}
}

std::wstring GetFileZillaEngineVersion()
{
#ifdef ENGINE_VERSION
	return fz::to_wstring(std::string(ENGINE_VERSION));
#else
	return {};
#endif
}

int64_t ConvertToVersionNumber(wchar_t const* version)
{
	// Crude conversion from version string into number for easy comparison
	// Supported version formats:
	// 1.2.4
	// 11.22.33.44
	// 1.2.3-rc3
	// 1.2.3.4-beta5
	// All numbers can be as large as 1024, with the exception of the release candidate.

	// Only either rc or beta can exist at the same time)
	//
	// The version string A.B.C.D-rcE-betaF expands to the following binary representation:
	// 0000aaaaaaaaaabbbbbbbbbbccccccccccddddddddddxeeeeeeeeeffffffffff
	//
	// x will be set to 1 if neither rc nor beta are set. 0 otherwise.
	//
	// Example:
	// 2.2.26-beta3 will be converted into
	// 0000000010 0000000010 0000011010 0000000000 0000000000 0000000011
	// which in turn corresponds to the simple 64-bit number 2254026754228227
	// And these can be compared easily

	if (!version || *version < '0' || *version > '9') {
		return -1;
	}

	int64_t v{};
	int segment{};
	int shifts{};

	for (; *version; ++version) {
		if (*version == '.' || *version == '-' || *version == 'b') {
			v += segment;
			segment = 0;
			v <<= 10;
			shifts++;
		}
		if (*version == '-' && shifts < 4) {
			v <<= (4 - shifts) * 10;
			shifts = 4;
		}
		else if (*version >= '0' && *version <= '9') {
			segment *= 10;
			segment += *version - '0';
		}
	}
	v += segment;
	v <<= (5 - shifts) * 10;

	// Make sure final releases have a higher version number than rc or beta releases
	if ((v & 0x0FFFFF) == 0) {
		v |= 0x080000;
	}

	return v;
}

#ifdef FZ_WINDOWS
namespace {
bool IsAtLeast(unsigned int major, unsigned int minor = 0)
{
	OSVERSIONINFOEX vi{};
	vi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
	vi.dwMajorVersion = major;
	vi.dwMinorVersion = minor;
	vi.dwPlatformId = VER_PLATFORM_WIN32_NT;

	DWORDLONG mask{};
	VER_SET_CONDITION(mask, VER_MAJORVERSION, VER_GREATER_EQUAL);
	VER_SET_CONDITION(mask, VER_MINORVERSION, VER_GREATER_EQUAL);
	VER_SET_CONDITION(mask, VER_PLATFORMID, VER_EQUAL);
	return VerifyVersionInfo(&vi, VER_MAJORVERSION | VER_MINORVERSION | VER_PLATFORMID, mask) != 0;
}
}

SystemVersion GetSystemVersion()
{
	// Microsoft, in its insane stupidity, has decided to make GetVersion(Ex) useless, starting with Windows 8.1,
	// this function no longer returns the operating system version but instead some arbitrary and random value depending
	// on the phase of the moon.
	// This function instead returns the actual Windows version. On non-Windows systems, it's equivalent to
	// wxGetOsVersion
	unsigned major = 4;
	unsigned minor = 0;
	while (IsAtLeast(++major, minor))
	{
	}
	--major;
	while (IsAtLeast(major, ++minor))
	{
	}
	--minor;

	return {major, minor};
}

#elif FZ_MAC
SystemVersion GetSystemVersion()
{
	SInt32 major{};
	Gestalt(gestaltSystemVersionMajor, &major);

	SInt32 minor{};
	Gestalt(gestaltSystemVersionMinor, &minor);

	return {static_cast<unsigned int>(major), static_cast<unsigned int>(minor)};
}

#else

SystemVersion GetSystemVersion()
{
	SystemVersion ret;
	utsname buf{};
	if (!uname(&buf)) {
		char const* p = buf.release;
		while (*p >= '0' && *p <= '9') {
			ret.major *= 10;
			ret.major += *p - '0';
			++p;
		}
		if (*p == '.') {
			++p;
			while (*p >= '0' && *p <= '9') {
				ret.minor *= 10;
				ret.minor += *p - '0';
				++p;
			}
		}
	}

	return ret;
}

#endif
