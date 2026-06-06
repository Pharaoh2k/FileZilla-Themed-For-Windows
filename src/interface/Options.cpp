#include "filezilla.h"
#include "Options.h"
#include "filezillaapp.h"
#include "file_utils.h"
#include "xmlfunctions.h"

#include "../commonui/fz_paths.h"

#ifdef FZ_WINDOWS
	#include <shlobj.h>
#endif

COptions* COptions::m_theOptions = 0;

#ifdef FZ_WINDOWS
//case insensitive
#define DEFAULT_FILENAME_SORT   0
#define DEFAULT_LOCAL_SYMLINK_RESOLVE true
#else
//case sensitive
#define DEFAULT_FILENAME_SORT   1
#define DEFAULT_LOCAL_SYMLINK_RESOLVE false
#endif

namespace {
#ifdef FZ_WINDOWS
	auto const platform_name = "win";
#elif defined(FZ_MAC)
	auto const platform_name = "mac";
#else
	auto const platform_name = "unix";
#endif

std::string const product_name{};

static unsigned int register_interface_options()
{
	// Note: A few options are versioned due to a changed
	// option syntax or past, unhealthy defaults
	static int const value = register_options({
		// Default/internal options
		{ "Cache directory", L"", option_flags::predefined_priority | option_flags::platform },

		// Normal UI options
		{ "Number of Transfers", 2, option_flags::numeric_clamp, 1, 10 },
		{ "Language Code", L"", option_flags::normal, 50 },
		{ "Concurrent download limit", 0, option_flags::numeric_clamp, 0, 10 },
		{ "Concurrent upload limit", 0, option_flags::numeric_clamp, 0, 10 },
		{ "Show debug menu", false, option_flags::normal },
		{ "File exists action download", 0, option_flags::normal, 0, 7 },
		{ "File exists action upload", 0, option_flags::normal, 0, 7 },
		{ "Allow ascii resume", false, option_flags::normal },
		{ "Greeting version", L"", option_flags::normal },
		{ "Greeting resources", L"", option_flags::normal },
		{ "Onetime Dialogs", L"", option_flags::normal },
		{ "Show Tree Local", true, option_flags::normal },
		{ "Show Tree Remote", true, option_flags::normal },
		{ "File Pane Layout", 0, option_flags::normal, 0, 3 },
		{ "File Pane Swap", false, option_flags::normal },
		{ "Filelist directory sort", 0, option_flags::normal, 0, 2 },
		{ "Filelist name sort", DEFAULT_FILENAME_SORT, option_flags::normal, 0, 2 },
		{ "Queue successful autoclear", false, option_flags::normal },
		{ "Queue column widths", L"", option_flags::normal },
		{ "Local filelist colwidths", L"", option_flags::normal },
		{ "Remote filelist colwidths", L"", option_flags::normal },
		{ "Window position and size", L"", option_flags::normal },
		{ "Splitter positions (v2)", L"", option_flags::normal },
		{ "Local filelist sortorder", L"", option_flags::normal },
		{ "Remote filelist sortorder", L"", option_flags::normal },
		{ "Time Format", L"", option_flags::normal },
		{ "Date Format", L"", option_flags::normal },
		{ "Show message log", true, option_flags::normal },
		{ "Show queue", true, option_flags::normal },
		{ "Default editor", L"", option_flags::platform },
		{ "Always use default editor", false, option_flags::normal },
		{ "File associations (v2)", L"", option_flags::platform },
		{ "Comparison mode", 1, option_flags::normal, 0, 1 },
		{ "Site Manager position", L"", option_flags::normal },
		{ "Icon theme", L"default", option_flags::normal },
		{ "Icon scale", 125, option_flags::numeric_clamp, 25, 400 },
		{ "Timestamp in message log", false, option_flags::normal },
		{ "Sitemanager last selected", L"", option_flags::normal },
		{ "Local filelist shown columns", L"", option_flags::normal },
		{ "Remote filelist shown columns", L"", option_flags::normal },
		{ "Local filelist column order", L"", option_flags::normal },
		{ "Remote filelist column order", L"", option_flags::normal },
		{ "Filelist status bar", true, option_flags::normal },
		{ "Filter toggle state", false, option_flags::normal },
		{ "Show quickconnect bar", true, option_flags::normal },
		{ "Messagelog position", 0, option_flags::normal, 0, 2 },
		{ "File doubleclick action", 0, option_flags::normal, 0, 3 },
		{ "Dir doubleclick action", 0, option_flags::normal, 0, 3 },
		{ "Minimize to tray", false, option_flags::normal },
		{ "Search column widths", L"", option_flags::normal },
		{ "Search column shown", L"", option_flags::normal },
		{ "Search column order", L"", option_flags::normal },
		{ "Search window size", L"", option_flags::normal },
		{ "Comparison hide identical", false, option_flags::normal },
		{ "Search sort order", L"", option_flags::normal },
		{ "Edit track local", true, option_flags::normal },
		{ "Prevent idle sleep", true, option_flags::normal },
		{ "Filteredit window size", L"", option_flags::normal },
		{ "Invalid char replace", L"_", option_flags::normal, option_type::string, 1, [](std::wstring& v) {
			return v.size() == 1 && !IsInvalidChar(v[0], true);
		}},
		{ "Already connected choice", 0, option_flags::normal },
		{ "Edit status dialog size", L"", option_flags::normal },
		{ "Display current speed", false, option_flags::normal },
		{ "Toolbar hidden", false, option_flags::normal },
		{ "Strip VMS revisions", false, option_flags::normal },
		{ "Startup action", 0, option_flags::normal },
		{ "Prompt password save", 0, option_flags::normal },
		{ "Persistent Choices", 0, option_flags::normal },
		{ "Queue completion action", 1, option_flags::normal },
		{ "Queue completion command", L"", option_flags::normal },
		{ "Drag and Drop disabled", false, option_flags::normal },
		{ "Disable update footer", false, option_flags::normal },
		{ "Tab data", L"", option_flags::normal | option_flags::sensitive_data, option_type::xml },
		{ "Highest shown overlay id", 0, option_flags::normal },
		{ "Resolve local symlinks", DEFAULT_LOCAL_SYMLINK_RESOLVE, option_flags::platform }
	});
	return value;
}

option_registrator r(&register_interface_options);
}

optionsIndex mapOption(interfaceOptions opt)
{
	static unsigned int const offset = register_interface_options();

	auto ret = optionsIndex::invalid;
	if (opt < OPTIONS_NUM) {
		return static_cast<optionsIndex>(opt + offset);
	}
	return ret;
}

BEGIN_EVENT_TABLE(COptions, wxEvtHandler)
EVT_TIMER(wxID_ANY, COptions::OnTimer)
END_EVENT_TABLE()

COptions::COptions()
	: XmlOptions("")
{
	if (!m_theOptions) {
		m_theOptions = this;
	}

	m_save_timer.SetOwner(this);

	std::wstring error;
	if (!Load(error)) {
		wxString msg = error + L"\n\n" + _("For this session the default settings will be used. Any changes to the settings will not be saved.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);
	}
}

COptions::~COptions()
{
	if (m_theOptions == this) {
		m_theOptions = nullptr;
	}
}

COptions* COptions::Get()
{
	return m_theOptions;
}

void COptions::OnTimer(wxTimerEvent&)
{
	Save(false);
}

void COptions::Save(bool processChanged)
{
	m_save_timer.Stop();

	std::wstring error;
	if (!XmlOptions::Save(processChanged, error)) {
		wxString msg;
		if (xmlFile_) {
			msg = wxString::Format(_("Could not write \"%s\":"), xmlFile_->GetFileName());
		}
		if (error.empty()) {
			error = _("Unknown error");
		}
		wxMessageBoxEx(msg + L"\n" + error, _("Error writing xml file"), wxICON_ERROR);
	}
}

namespace {
#ifndef FZ_WINDOWS
std::wstring TryDirectory(wxString path, wxString const& suffix, bool check_exists)
{
	if (!path.empty() && path[0] == '/') {
		if (path[path.size() - 1] != '/') {
			path += '/';
		}

		path += suffix;

		if (check_exists) {
			if (!wxFileName::DirExists(path)) {
				path.clear();
			}
		}
	}
	else {
		path.clear();
	}
	return path.ToStdWstring();
}

#endif
}

CLocalPath COptions::GetCacheDirectory()
{
	CLocalPath ret;

	std::wstring dir(get_string(OPTION_DEFAULT_CACHE_DIR));
	if (!dir.empty()) {
		dir = ExpandPath(dir);
		ret.SetPath(GetDefaultsDir().GetPath());
		if (!ret.ChangePath(dir)) {
			ret.clear();
		}
	}
	else {
#ifdef FZ_WINDOWS
		wchar_t* out{};
		if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, 0, &out) == S_OK) {
			ret.SetPath(out);
			CoTaskMemFree(out);
		}
		if (!ret.empty()) {
			ret.AddSegment(L"FileZilla");
		}
#else
		std::wstring cfg = TryDirectory(GetEnv("XDG_CACHE_HOME"), L"filezilla/", false);
		if (cfg.empty()) {
			cfg = TryDirectory(wxGetHomeDir(), L".cache/filezilla/", false);
		}
		ret.SetPath(cfg);
#endif
	}

	return ret;
}

void COptions::notify_changed()
{
	CallAfter([this](){ continue_notify_changed(); });
}

void COptions::on_dirty()
{
	if (!m_save_timer.IsRunning()) {
		m_save_timer.Start(15000, true);
	}
}
