#ifndef FILEZILLA_INTERFACE_SETTINGS_SETTINGSDIALOG_HEADER
#define FILEZILLA_INTERFACE_SETTINGS_SETTINGSDIALOG_HEADER

#include "../dialogex.h"
#include "../Options.h"

class COptions;
class COptionsPage;
class CMainFrame;
class wxTreeCtrlEx;
class login_manager;
class CSettingsDialog final : public wxDialogEx
{
public:
	CSettingsDialog(COptions& options, login_manager & lim, CFileZillaEngineContext & engine_context);
	virtual ~CSettingsDialog();

	bool Create(CMainFrame* pMainFrame);
	bool LoadSettings();

	CMainFrame* m_pMainFrame{};

	CFileZillaEngineContext& GetEngineContext() { return m_engine_context; }

	void RememberOldValue(interfaceOptions option);

protected:
	bool LoadPages();

	COptions & options_;
	login_manager & login_manager_;

	wxPanel* pagePanel_{};

	COptionsPage* m_activePanel{};

	wxTreeCtrlEx* tree_{};

	void AddPage(wxString const& name, COptionsPage* page, int nest);

	struct t_page
	{
		wxTreeItemId id;
		COptionsPage* page{};
	};
	std::vector<t_page> m_pages;

	std::map<interfaceOptions, std::wstring> m_oldValues;

	void OnPageChanging(wxTreeEvent& event);
	void OnPageChanged(wxTreeEvent& event);
	void OnOK(wxCommandEvent& event);
	void OnCancel(wxCommandEvent& event);

	CFileZillaEngineContext& m_engine_context;
};

#endif
