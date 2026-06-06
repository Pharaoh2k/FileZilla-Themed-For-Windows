#ifndef FILEZILLA_INTERFACE_QUICKCONNECTBAR_HEADER
#define FILEZILLA_INTERFACE_QUICKCONNECTBAR_HEADER

#include "../commonui/site.h"

#include <wx/panel.h>

class CMainFrame;
class COptionsBase;
class wxTextCtrl;
class CQuickconnectBar final : public wxPanel
{
public:
	CQuickconnectBar(CMainFrame & parent);

	void ClearFields();

protected:
	// Only valid while menu is being displayed
	std::deque<Site> m_recentServers;

	DECLARE_EVENT_TABLE()
	void OnQuickconnect(wxCommandEvent& event);
	void OnQuickconnectDropdown(wxCommandEvent& event);
	void OnMenu(wxCommandEvent& event);

	COptionsBase & options_;

	wxTextCtrl* m_pHost{};
	wxTextCtrl* m_pUser{};
	wxTextCtrl* m_pPass{};
	wxTextCtrl* m_pPort{};

	CMainFrame & mainFrame_;
};


#endif
