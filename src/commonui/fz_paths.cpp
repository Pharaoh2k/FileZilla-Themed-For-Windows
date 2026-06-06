#include "fz_paths.h"

#include "xml_file.h"
#include <libfilezilla/buffer.hpp>
#include <libfilezilla/local_filesys.hpp>

#include <cstdlib>
#include <cstring>

#ifdef FZ_MAC
	#include <CoreFoundation/CFBundle.h>
	#include <CoreFoundation/CFURL.h>
#elif defined(FZ_WINDOWS)
	#include <shlobj.h>
	#include <objbase.h>
#else
	#include <unistd.h>
	#include <wordexp.h>
#endif

using namespace std::literals;

#ifndef FZ_WINDOWS
namespace {
static std::wstring TryDirectory(std::wstring path, std::wstring const& suffix, bool check_exists)
{
	if (!path.empty() && path[0] == '/') {
		if (path[path.size() - 1] != '/') {
			path += '/';
		}

		path += suffix;

		if (check_exists) {
			if (!CLocalPath(path).Exists(nullptr)) {
				path.clear();
			}
		}
	}
	else {
		path.clear();
	}
	return path;
}
}
#endif

CLocalPath GetHomeDir()
{
	CLocalPath ret;

#ifdef FZ_WINDOWS
	wchar_t* out{};
	if (SHGetKnownFolderPath(FOLDERID_Profile, 0, 0, &out) == S_OK) {
		ret.SetPath(out);
		CoTaskMemFree(out);
	}
#else
	ret.SetPath(GetEnv("HOME"));
#endif
	return ret;
}

#if FZ_MAC
std::string const& GetTempDirImpl();
#endif

CLocalPath GetTempDir()
{
	CLocalPath ret;

#ifdef FZ_WINDOWS
	DWORD len = GetTempPathW(0, nullptr);
	if (len) {
		std::wstring buf;
		buf.resize(len);
		DWORD len2 = GetTempPathW(len, buf.data());
		if (len2 == len - 1) {
			buf.pop_back();
			ret.SetPath(buf);
		}
	}
#elif FZ_MAC
	ret.SetPath(fz::to_wstring_from_utf8(GetTempDirImpl()));
#else
	if (!ret.SetPath(GetEnv("TMPDIR"))) {
		if (!ret.SetPath(GetEnv("TMP"))) {
			if (!ret.SetPath(GetEnv("TEMP"))) {
				ret.SetPath(L"/tmp");
			}
		}
	}
#endif
	return ret;
}

CLocalPath GetUnadjustedSettingsDir()
{
	CLocalPath ret;

#ifdef FZ_WINDOWS
	wchar_t* out{};
	if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, 0, &out) == S_OK) {
		ret.SetPath(out);
		CoTaskMemFree(out);
	}
	if (!ret.empty()) {
		ret.AddSegment(L"FileZilla");
	}

	if (ret.empty()) {
		// Fall back to directory where the executable is
		std::wstring const& dir = GetOwnExecutableDir();
		ret.SetPath(dir);
	}
#else
	std::wstring cfg = TryDirectory(GetEnv("XDG_CONFIG_HOME"), L"filezilla/", true);
	if (cfg.empty()) {
		cfg = TryDirectory(GetEnv("HOME"), L".config/filezilla/", true);
	}
	if (cfg.empty()) {
		cfg = TryDirectory(GetEnv("HOME"), L".filezilla/", true);
	}
	if (cfg.empty()) {
		cfg = TryDirectory(GetEnv("XDG_CONFIG_HOME"), L"filezilla/", false);
	}
	if (cfg.empty()) {
		cfg = TryDirectory(GetEnv("HOME"), L".config/filezilla/", false);
	}
	if (cfg.empty()) {
		cfg = TryDirectory(GetEnv("HOME"), L".filezilla/", false);
	}
	ret.SetPath(cfg);
#endif
	return ret;
}

#ifdef FZ_MAC
namespace {
std::wstring fromCFURLRef(CFURLRef url)
{
	std::wstring ret;
	if (url) {
		CFURLRef absolute = CFURLCopyAbsoluteURL(url);
		if (absolute) {
			CFStringRef path = CFURLCopyFileSystemPath(absolute, kCFURLPOSIXPathStyle);
			if (path) {
				CFIndex inlen = CFStringGetLength(path);
				CFIndex outlen = 0;
				CFStringGetBytes(path, CFRangeMake(0, inlen), kCFStringEncodingUTF32, 0, false, nullptr, 0, &outlen);
				if (outlen) {
					ret.resize((outlen + 3) / 4);
					CFStringGetBytes(path, CFRangeMake(0, inlen), kCFStringEncodingUTF32, 0, false, reinterpret_cast<UInt8*>(ret.data()), outlen, &outlen);
				}
				CFRelease(path);
			}
			CFRelease(absolute);
		}
		CFRelease(url);
	}

	return ret;
}
}
#endif

#if FZ_WINDOWS
std::wstring GetOwnExecutable()
{
	std::wstring path;
	path.resize(4095);
	DWORD res;
	while (true) {
		res = GetModuleFileNameW(0, &path[0], static_cast<DWORD>(path.size() - 1));
		if (!res) {
			// Failure
			return std::wstring();
		}

		if (res < path.size() - 1) {
			path.resize(res);
			break;
		}

		path.resize(path.size() * 2 + 1);
	}
	return path;
}
#endif

std::wstring GetOwnExecutableDir()
{
#ifdef FZ_WINDOWS
	std::wstring const path = GetOwnExecutable();
	size_t pos = path.find_last_of(L"\\/");
	if (pos != std::wstring::npos) {
		return path.substr(0, pos + 1);
	}
#elif defined(FZ_MAC)
	CFBundleRef bundle = CFBundleGetMainBundle();
	if (bundle) {
		std::wstring const executable = fromCFURLRef(CFBundleCopyExecutableURL(bundle));
		size_t pos = executable.rfind('/');
		if (pos != std::wstring::npos) {
			return executable.substr(0, pos + 1);
		}
	}
#else
	std::string path;
	path.resize(4095);
	while (true) {
		int res = readlink("/proc/self/exe", &path[0], path.size());
		if (res < 0) {
			return std::wstring();
		}
		if (static_cast<size_t>(res) < path.size()) {
			path.resize(res);
			break;
		}
		path.resize(path.size() * 2 + 1);
	}
	size_t pos = path.rfind('/');
	if (pos != std::wstring::npos) {
		return fz::to_wstring(path.substr(0, pos + 1));
	}
#endif
	return std::wstring();
}

#ifdef FZ_MAC
std::wstring mac_data_path();
#endif

namespace {

#if FZ_WINDOWS
std::wstring const PATH_SEP = L";";
#define L_DIR_SEP L"\\"
#else
std::wstring const PATH_SEP = L":";
#define L_DIR_SEP L"/"
#endif
}

CLocalPath GetFZDataDir(std::vector<std::wstring> const& fileToFind, std::wstring const& prefixSub, bool searchSelfDir)
{
	/*
	 * Finding the resources in all cases is a difficult task,
	 * due to the huge variety of different systems and their filesystem
	 * structure.
	 * Basically we just check a couple of paths for presence of the resources,
	 * and hope we find them. If not, the user can still specify on the cmdline
	 * and using environment variables where the resources are.
	 *
	 * At least on OS X it's simple: All inside application bundle.
	 */

	CLocalPath ret;

	auto testPath = [&](std::wstring const& path) {
		ret = CLocalPath(path);
		if (ret.empty()) {
			return false;
		}

		for (auto const& file : fileToFind) {
			if (FileExists(ret.GetPath() + file)) {
				return true;
			}
		}
		return false;
	};

#ifdef FZ_MAC
	(void)prefixSub;
	if (searchSelfDir) {
		CFBundleRef bundle = CFBundleGetMainBundle();
		if (bundle) {
			if (testPath(fromCFURLRef(CFBundleCopySharedSupportURL(bundle)))) {
				return ret;
			}
		}
	}

	return CLocalPath();
#else

	// First try the user specified data dir.
	if (searchSelfDir) {
		if (testPath(GetEnv("FZ_DATADIR"))) {
			return ret;
		}
	}

	std::wstring selfDir = GetOwnExecutableDir();
	if (!selfDir.empty()) {
		if (searchSelfDir && testPath(selfDir)) {
			return ret;
		}

		if (!prefixSub.empty() && selfDir.size() > 5 && fz::ends_with(selfDir, std::wstring(L_DIR_SEP L"bin" L_DIR_SEP))) {
			std::wstring path = selfDir.substr(0, selfDir.size() - 4) + prefixSub + L_DIR_SEP;
			if (testPath(path)) {
				return ret;
			}
		}

		// Development paths
		if (searchSelfDir && selfDir.size() > 7 && fz::ends_with(selfDir, std::wstring(L_DIR_SEP L".libs" L_DIR_SEP))) {
			std::wstring path = selfDir.substr(0, selfDir.size() - 6);
			if (FileExists(path + L"Makefile")) {
				if (testPath(path)) {
					return ret;
				}
			}
		}
	}

	// Now scan through the path
	if (!prefixSub.empty()) {
		std::wstring path = GetEnv("PATH");
		auto const segments = fz::strtok(path, PATH_SEP);

		for (auto const& segment : segments) {
			auto cur = CLocalPath(segment).GetPath();
			if (cur.size() > 5 && fz::ends_with(cur, std::wstring(L_DIR_SEP L"bin" L_DIR_SEP))) {
				std::wstring path = cur.substr(0, cur.size() - 4) + prefixSub + L_DIR_SEP;
				if (testPath(path)) {
					return ret;
				}
			}
		}
	}

	ret.clear();
	return ret;
#endif
}

CLocalPath GetDefaultsDir()
{
	static CLocalPath path = [] {
		CLocalPath path;
#ifdef FZ_UNIX
		path = GetUnadjustedSettingsDir();
		if (path.empty() || !FileExists(path.GetPath() + L"fzdefaults.xml")) {
			if (FileExists(L"/etc/filezilla/fzdefaults.xml")) {
				path.SetPath(L"/etc/filezilla");
			}
			else {
				path.clear();
			}
		}

#endif
		if (path.empty()) {
			path = GetFZDataDir({ L"fzdefaults.xml" }, L"share/filezilla");
		}
		return path;
	}();

	return path;
}


std::wstring GetSettingFromFile(std::wstring const& xmlfile, std::string const& name)
{
	CXmlFile file(xmlfile);
	if (!file.Load()) {
		return L"";
	}

	auto element = file.GetElement();
	if (!element) {
		return L"";
	}

	auto settings = element.child("Settings");
	if (!settings) {
		return L"";
	}

	for (auto setting = settings.child("Setting"); setting; setting = setting.next_sibling("Setting")) {
		const char* nodeVal = setting.attribute("name").value();
		if (!nodeVal || strcmp(nodeVal, name.c_str())) {
			continue;
		}

		return fz::to_wstring_from_utf8(setting.child_value());
	}

	return L"";
}

std::wstring ReadSettingsFromDefaults(CLocalPath const& defaultsDir)
{
	if (defaultsDir.empty()) {
		return L"";
	}

	std::wstring dir = GetSettingFromFile(defaultsDir.GetPath() + L"fzdefaults.xml", "Config Location");
	auto result = ExpandPath(dir);

	if (!FileExists(result)) {
		return L"";
	}

	if (result[result.size() - 1] != '/') {
		result += '/';
	}

	return result;
}


CLocalPath GetSettingsDir()
{
	CLocalPath p;

	CLocalPath const defaults_dir = GetDefaultsDir();
	std::wstring dir = ReadSettingsFromDefaults(defaults_dir);
	if (!dir.empty()) {
		dir = ExpandPath(dir);
		p.SetPath(defaults_dir.GetPath());
		p.ChangePath(dir);
	}
	else {
		p = GetUnadjustedSettingsDir();
	}
	return p;
}

namespace {
template<typename String>
String ExpandPathImpl(String dir)
{
	if (dir.empty()) {
		return dir;
	}

	String result;
	while (!dir.empty()) {
		String token;
#ifdef FZ_WINDOWS
		size_t pos = dir.find_first_of(fzS(typename String::value_type, "\\/"));
#else
		size_t pos = dir.find('/');
#endif
		if (pos == String::npos) {
			token.swap(dir);
		}
		else {
			token = dir.substr(0, pos);
			dir = dir.substr(pos + 1);
		}

		if (token[0] == '$') {
			if (token[1] == '$') {
				result += token.substr(1);
			}
			else if (token.size() > 1) {
				char* env = getenv(fz::to_string(token.substr(1)).c_str());
				if (env) {
					result += fz::toString<String>(env);
				}
			}
		}
		else {
			result += token;
		}

#ifdef FZ_WINDOWS
		result += '\\';
#else
		result += '/';
#endif
	}

	return result;
}
}

std::string ExpandPath(std::string const& dir) {
	return ExpandPathImpl(dir);
}

std::wstring ExpandPath(std::wstring const& dir) {
	return ExpandPathImpl(dir);
}


#if defined FZ_MAC
char const* GetDownloadDirImpl();
#elif !defined(FZ_WINDOWS)

namespace {
std::string ShellUnescape(std::string const& path)
{
	std::string ret;

	wordexp_t p;
	int res = wordexp(path.c_str(), &p, WRDE_NOCMD);
	if (!res && p.we_wordc == 1 && p.we_wordv) {
		ret = p.we_wordv[0];
	}
	wordfree(&p);

	return ret;
}

size_t next_line(fz::file& f, fz::buffer& buf, size_t maxlen = 16 * 1024)
{
	while (!buf.empty() && buf[0] == '\n') {
		buf.consume(1);
	}
	for (size_t i = 0; i < buf.size(); ++i) {
		if (buf[i] == '\n') {
			return i;
		}
	}

	while (buf.size() < maxlen) {
		size_t const oldSize = buf.size();
		unsigned char* p = buf.get(maxlen - oldSize);
		fz::rwresult read = f.read2(p, maxlen - oldSize);
		if (!read) {
			return std::string::npos;
		}
		if (!read.value_) {
			return buf.size();
		}
		buf.add(read.value_);
		for (size_t i = 0; i < read.value_; ++i) {
			if (p[i] == '\n') {
				return i + oldSize;
			}
		}
	}

	return std::string::npos;
}

CLocalPath GetXdgUserDir(std::string_view type)
{
	CLocalPath confdir(GetEnv("XDG_CONFIG_HOME"));
	if (confdir.empty()) {
		confdir = GetHomeDir();
		if (!confdir.empty()) {
			confdir.AddSegment(L".config");
		}
	}
	if (confdir.empty()) {
		return {};
	}

	fz::file f(fz::to_native(confdir.GetPath()) + "/user-dirs.dirs", fz::file::reading, fz::file::existing);
	if (!f.opened()) {
		return {};
	}

	fz::buffer buf;
	while (true) {
		size_t const nl = next_line(f, buf);
		if (nl == std::string::npos) {
			break;
		}

		std::string_view line(reinterpret_cast<char const*>(buf.get()), nl);
		fz::trim(line);
		if (fz::starts_with(line, type)) {
			size_t pos = line.find('=');
			if (pos != std::string::npos) {
				CLocalPath path(fz::to_wstring(ShellUnescape(std::string(line.substr(pos + 1)))));
				if (!path.empty()) {
					return path;
				}
			}
		}
		buf.consume(nl);
	}

	return {};
}
}
#endif

CLocalPath GetDownloadDir()
{
#if FZ_WINDOWS
	PWSTR path;
	HRESULT result = SHGetKnownFolderPath(FOLDERID_Downloads, 0, 0, &path);
	if (result == S_OK) {
		std::wstring dir = path;
		CoTaskMemFree(path);
		return CLocalPath(dir);
	}
	return {};
#elif FZ_MAC
	CLocalPath ret;
	char const* url = GetDownloadDirImpl();
	ret.SetPath(fz::to_wstring_from_utf8(url));
	return ret;
#else
	CLocalPath dl = GetXdgUserDir("XDG_DOWNLOAD_DIR"sv);
	if (dl.empty() || !dl.Exists()) {
		dl = GetXdgUserDir("XDG_DOCUMENTS_DIR"sv);
	}

	return dl;
#endif
}

std::wstring FindTool(std::wstring const& tool, std::wstring const& buildRelPath, char const* env)
{
#if FZ_MAC
	(void)buildRelPath;
	(void)env;

	// On Mac we only look inside the bundle
	std::wstring path = GetOwnExecutableDir();
	if (!path.empty()) {
		std::wstring executable = path;
		executable += '/';
		executable += tool;
		if (FileExists(executable)) {
			return executable;
		}
	}
#else

	// First check the given environment variable
	std::wstring executable = GetEnv(env);
	if (!executable.empty()) {
		if (FileExists(executable)) {
			return executable;
		}
	}

	std::wstring program = tool;
#if FZ_WINDOWS
	program += L".exe";
#endif

	std::wstring path = GetOwnExecutableDir();
	if (!path.empty()) {
		// Now search in own executable dir
		executable = path + program;
		if (FileExists(executable)) {
			return executable;
		}

		// Check if running from build dir
		if (path.size() > 7 && fz::ends_with(path, std::wstring(L"/.libs/"))) {
			if (FileExists(path.substr(0, path.size() - 6) + L"Makefile")) {
				executable = path + L"../" + buildRelPath + program;
				if (FileExists(executable)) {
					return executable;
				}
			}
		}
		else if (FileExists(path + L"Makefile")) {
			executable = path + buildRelPath + program;
			if (FileExists(executable)) {
				return executable;
			}
		}
	}

	// Last but not least, PATH
	path = GetEnv("PATH");
	auto const segments = fz::strtok(path, PATH_SEP);
	for (auto const& segment : segments) {
		auto cur = CLocalPath(segment).GetPath();
		executable = cur + program;
		if (!cur.empty() && FileExists(executable)) {
			return executable;
		}
	}
#endif

	return {};
}
