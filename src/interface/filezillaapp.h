#ifndef FILEZILLA_INTERFACE_FILEZILLAAPP_HEADER
#define FILEZILLA_INTERFACE_FILEZILLAAPP_HEADER

#include "../include/local_path.h"
#include "../include/engine_options.h"

#include <vector>

class CCommandLine;
class COptions;
class CThemeProvider;
class CWrapEngine;

class CFileZillaApp : public wxApp
{
public:
	CFileZillaApp();
	virtual ~CFileZillaApp();

	virtual bool OnInit();
	virtual int OnExit();

	// Always (back)slash-terminated
	CLocalPath GetResourceDir() const { return m_resourceDir; }
	CLocalPath GetLocalesDir() const { return m_localesDir; }

	std::wstring GetSettingsFile(std::wstring const& name) const;

#if ENABLE_STORJ
	void CheckExistsFzstorj();
#endif

	void InitLocale();
	bool SetLocale(int language);
	int GetCurrentLanguage() const;
	wxString GetCurrentLanguageCode() const;

#ifdef __WXMSW__
	// Applies the configured appearance (OPTION_APPEARANCE_MODE:
	// 0 = follow system, 1 = dark, 2 = light) by enabling wxWidgets'
	// native dark mode support. Windows-only (needs wxWidgets >= 3.3).
	void ApplyAppearanceMode();
#endif

	void DisplayEncodingWarning();

	CWrapEngine* GetWrapEngine();

	const CCommandLine* GetCommandLine() const { return m_pCommandLine.get(); }

	void ShowStartupProfile();
	void AddStartupProfileRecord(std::string_view const& msg);

protected:
#if ENABLE_STORJ
	void CheckExistsTool(std::wstring const& tool, std::wstring const& buildRelPath, char const* env, engineOptions setting, std::wstring const& description);
#endif

	bool LoadResourceFiles();
	bool LoadLocales();
	int ProcessCommandLine();

	std::unique_ptr<wxLocale> m_pLocale;

	CLocalPath m_resourceDir;
	CLocalPath m_localesDir;

#if wxUSE_DEBUGREPORT && wxUSE_ON_FATAL_EXCEPTION
	virtual void OnFatalException();
#endif

	std::unique_ptr<COptions> options_;
	std::unique_ptr<CWrapEngine> m_pWrapEngine;

	std::unique_ptr<CCommandLine> m_pCommandLine;

	fz::monotonic_clock m_profile_start;
	std::vector<std::pair<fz::monotonic_clock, std::string>> m_startupProfile;

	std::unique_ptr<CThemeProvider> themeProvider_;
};

DECLARE_APP(CFileZillaApp)

#endif
