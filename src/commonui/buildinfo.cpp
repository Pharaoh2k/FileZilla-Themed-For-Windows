#include "buildinfo.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../include/version.h"

#include <libfilezilla/format.hpp>

#include <sqlite3.h>

#include <tuple>

std::wstring GetFileZillaVersion()
{
#ifdef PACKAGE_VERSION
    return fz::to_wstring(std::string(PACKAGE_VERSION));
#else
    return {};
#endif
}


std::wstring CBuildInfo::GetBuildDateString()
{
	// Get build date. Unfortunately it is in the ugly Mmm dd yyyy format.
	// Make a good yyyy-mm-dd out of it
	std::wstring date = fz::to_wstring(std::string(__DATE__));
	while (date.find(L"  ") != std::wstring::npos) {
		fz::replace_substrings(date, L"  ", L" ");
	}

	wchar_t const months[][4] = { L"Jan", L"Feb", L"Mar",
								L"Apr", L"May", L"Jun",
								L"Jul", L"Aug", L"Sep",
								L"Oct", L"Nov", L"Dec" };

	size_t pos = date.find(' ');
	if (pos == std::wstring::npos) {
		return date;
	}

	std::wstring month = date.substr(0, pos);
	size_t i = 0;
	for (i = 0; i < 12; ++i) {
		if (months[i] == month) {
			break;
		}
	}
	if (i == 12) {
		return date;
	}

	std::wstring tmp = date.substr(pos + 1);
	pos = tmp.find(' ');
	if (pos == std::wstring::npos) {
		return date;
	}

	auto day = fz::to_integral<unsigned int>(tmp.substr(0, pos));
	if (!day) {
		return date;
	}

	auto year = fz::to_integral<unsigned int>(tmp.substr(pos + 1));
	if (!year) {
		return date;
	}

	return fz::sprintf(L"%04d-%02d-%02d", year, i + 1, day);
}

std::wstring CBuildInfo::GetBuildTimeString()
{
	return fz::to_wstring(std::string(__TIME__));
}

fz::datetime CBuildInfo::GetBuildDate()
{
	fz::datetime date(GetBuildDateString(), fz::datetime::utc);
	return date;
}

std::wstring CBuildInfo::GetCompiler()
{
#ifdef USED_COMPILER
	return fz::to_wstring(std::string(USED_COMPILER));
#elif defined __VISUALC__
	int version = __VISUALC__;
	return fz::sprintf(L"Visual C++ %d", version);
#else
	return L"Unknown compiler";
#endif
}

std::wstring CBuildInfo::GetCompilerFlags()
{
#ifndef USED_CXXFLAGS
	return std::wstring();
#else
	return fz::to_wstring(std::string(USED_CXXFLAGS));
#endif
}

std::wstring CBuildInfo::GetBuildType()
{
#ifdef BUILDTYPE
	std::wstring buildtype = fz::to_wstring(std::string(BUILDTYPE));
	if (buildtype == L"official" || buildtype == L"nightly") {
		return buildtype;
	}
#endif
	return std::wstring();
}

std::wstring CBuildInfo::GetHostname()
{
#ifndef USED_HOST
	return std::wstring();
#else
	return fz::to_wstring(std::string(USED_HOST));
#endif
}

std::wstring CBuildInfo::GetBuildSystem()
{
#ifndef USED_BUILD
	return std::wstring();
#else
	return fz::to_wstring(std::string(USED_BUILD));
#endif
}

bool CBuildInfo::IsUnstable()
{
	if (GetFileZillaVersion().find(L"beta") != std::wstring::npos) {
		return true;
	}

	if (GetFileZillaVersion().find(L"rc") != std::wstring::npos) {
		return true;
	}

	return false;
}


#if defined(__i386__) || defined(__x86_64__) || defined(_M_X64) || defined(_M_IX86)
#define HAVE_CPUID 1
#endif

#if HAVE_CPUID

#ifdef _MSC_VER
namespace {
	void cpuid(int f, int sub, int reg[4])
	{
		__cpuidex(reg, f, sub);
	}
}
#else
#include <cpuid.h>
namespace {
	void cpuid(int f, int sub, int reg[4])
	{
		__cpuid_count(f, sub, reg[0], reg[1], reg[2], reg[3]);
	}
}
#endif
#endif

std::wstring CBuildInfo::GetCPUCaps(char separator)
{
	std::wstring ret;

#if HAVE_CPUID
	int reg[4];
	cpuid(0, 0, reg);

	int const max = reg[0];

	cpuid(0x80000000, 0, reg);
	int const extmax = reg[0];

	// function (aka leaf), subfunction (subleaf), register, bit, description
	std::tuple<int, int, int, int, std::wstring> const caps[] = {
		std::make_tuple(1, 0, 3, 25, L"sse"),
		std::make_tuple(1, 0, 3, 26, L"sse2"),
		std::make_tuple(1, 0, 2, 0,  L"sse3"),
		std::make_tuple(1, 0, 2, 9,  L"ssse3"),
		std::make_tuple(1, 0, 2, 19, L"sse4.1"),
		std::make_tuple(1, 0, 2, 20, L"sse4.2"),
		std::make_tuple(1, 0, 2, 28, L"avx"),
		std::make_tuple(7, 0, 1, 5,  L"avx2"),
		std::make_tuple(1, 0, 2, 25, L"aes"),
		std::make_tuple(1, 0, 2, 1,  L"pclmulqdq"),
		std::make_tuple(1, 0, 2, 30, L"rdrnd"),
		std::make_tuple(7, 0, 1, 3,  L"bmi"),
		std::make_tuple(7, 0, 1, 8,  L"bmi2"),
		std::make_tuple(7, 0, 1, 19, L"adx"),
		std::make_tuple(0x80000001, 0, 3, 29, L"lm")
	};

	for (auto const& cap : caps) {
		int const leaf = std::get<0>(cap);
		if (leaf > 0 && max < leaf) {
			continue;
		}
		if (leaf < 0 && leaf > extmax) {
			continue;
		}

		cpuid(leaf, std::get<1>(cap), reg);
		if (reg[std::get<2>(cap)] & (1 << std::get<3>(cap))) {
			if (!ret.empty()) {
				ret += separator;
			}
			ret += std::get<4>(cap);
		}
	}
#else
	(void)separator;
#endif

	return ret;
}
