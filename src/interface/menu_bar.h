#ifndef FILEZILLA_INTERFACE_MENU_BAR_HEADER
#define FILEZILLA_INTERFACE_MENU_BAR_HEADER

#include "state.h"

#include "option_change_event_handler.h"

#include <wx/menu.h>

class CMainFrame;
class CMenuBar final : public wxMenuBar, public CGlobalStateEventHandler, public COptionChangeEventHandler
{
public:
	CMenuBar(CMainFrame & mainFrame, COptions & options);
	virtual ~CMenuBar();

	bool ShowItem(int id);
	bool HideItem(int id);

	void UpdateBookmarkMenu();

	std::vector<int> m_bookmark_menu_ids;
	std::map<int, wxString> m_bookmark_menu_id_map_global;
	std::map<int, wxString> m_bookmark_menu_id_map_site;

	void UpdateMenubarState();
protected:
	CMainFrame & mainFrame_;
	COptions& options_;

	void UpdateSpeedLimitMenuItem();

	virtual void OnStateChange(CState* pState, t_statechange_notifications notification, std::wstring const& data, const void* data2) override;
	virtual void OnOptionsChanged(watched_options const& options);

	DECLARE_EVENT_TABLE()
	void OnMenuEvent(wxCommandEvent& event);

	std::map<wxMenu*, std::map<int, wxMenuItem*> > m_hidden_items;
};

#endif
