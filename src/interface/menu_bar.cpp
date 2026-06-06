#include "filezilla.h"
#include "context_control.h"
#include "bookmarks_dialog.h"
#include "listingcomparison.h"
#include "Mainfrm.h"
#include "menu_bar.h"
#include "Options.h"
#include "QueueView.h"
#include "sitemanager.h"
#include "state.h"

#if USE_MAC_SANDBOX
#include "osx_sandbox_userdirs.h"
#endif

BEGIN_EVENT_TABLE(CMenuBar, wxMenuBar)
EVT_MENU(wxID_ANY, CMenuBar::OnMenuEvent)
END_EVENT_TABLE()

CMenuBar::CMenuBar(CMainFrame& mainFrame, COptions& options)
	: COptionChangeEventHandler(this)
	, mainFrame_(mainFrame)
	, options_(options)
{
	wxMenu* file = new wxMenu;
	Append(file, _("&File"));

	wxAcceleratorEntry accel;

	accel.FromString(L"CTRL+S");
	file->Append(XRCID("ID_MENU_FILE_SITEMANAGER"), _("&Site Manager..."), _("Opens the Site Manager"))->SetAccel(&accel);
	file->Append(XRCID("ID_MENU_FILE_COPYSITEMANAGER"), _("&Copy current connection to Site Manager..."));
	file->AppendSeparator();
	accel.FromString(L"CTRL+T");
	file->Append(XRCID("ID_MENU_FILE_NEWTAB"), _("New &tab"), _("Opens a new tab"))->SetAccel(&accel);
	accel.FromString(L"CTRL+W");
	file->Append(XRCID("ID_MENU_FILE_CLOSETAB"), _("Cl&ose tab"), _("Closes current tab"))->SetAccel(&accel);
	file->AppendSeparator();
	file->Append(XRCID("ID_EXPORT"), _("&Export..."));
	file->Append(XRCID("ID_IMPORT"), _("&Import..."));
	file->AppendSeparator();
	accel.FromString(L"CTRL+E");
	file->Append(XRCID("ID_MENU_FILE_EDITED"), _("S&how files currently being edited..."))->SetAccel(&accel);
	file->AppendSeparator();
	accel.FromString(L"CTRL+Q");
	file->Append(XRCID("wxID_EXIT"), _("E&xit"), _("Close FileZilla"))->SetAccel(&accel);


	wxMenu* edit = new wxMenu;
	Append(edit, _("&Edit"));
	edit->Append(XRCID("ID_MENU_EDIT_NETCONFWIZARD"), _("&Network configuration wizard..."));
	edit->Append(XRCID("ID_MENU_EDIT_CLEARPRIVATEDATA"), _("&Clear private data..."));
	edit->AppendSeparator();
	edit->Append(XRCID("wxID_PREFERENCES"), _("&Settings..."), _("Open the settings dialog of FileZilla"));
#ifdef FZ_MAC
	edit->Append(XRCID("ID_MENU_EDIT_SANDBOX_DIRECTORIES"), _("&Directory access permissions..."), _("Open the directory access permissions dialog to configure the local directories FileZilla has access to."));
#endif

	wxMenu* view = new wxMenu;
	Append(view, _("&View"));
	accel.FromString(L"F5");
	view->Append(XRCID("ID_REFRESH"), _("&Refresh"))->SetAccel(&accel);
	view->AppendSeparator();
	accel.FromString(L"Ctrl+I");
	view->Append(XRCID("ID_MENU_VIEW_FILTERS"), _("Directory listing &filters..."))->SetAccel(&accel);

	wxMenu* comparison = new wxMenu;
	view->AppendSubMenu(comparison, _("&Directory comparison"));

	accel.FromString(L"CTRL+O");
	comparison->Append(XRCID("ID_TOOLBAR_COMPARISON"), _("&Enable"), L"", wxITEM_CHECK)->SetAccel(&accel);
	comparison->AppendSeparator();
	comparison->Append(XRCID("ID_COMPARE_SIZE"), _("Compare file&size"), L"", wxITEM_RADIO);
	comparison->Append(XRCID("ID_COMPARE_DATE"), _("Compare &modification time"), L"", wxITEM_RADIO);
	comparison->AppendSeparator();
	comparison->Append(XRCID("ID_COMPARE_HIDEIDENTICAL"), _("&Hide identical files"), L"", wxITEM_CHECK);

	accel.FromString(L"CTRL+Y");
	view->Append(XRCID("ID_TOOLBAR_SYNCHRONIZED_BROWSING"), _("S&ynchronized browsing"), L"", wxITEM_CHECK)->SetAccel(&accel);
	view->Append(XRCID("ID_MENU_VIEW_FILELISTSTATUSBAR"), _("Filelist status &bars"), L"", wxITEM_CHECK);
	view->AppendSeparator();
	view->Append(XRCID("ID_VIEW_TOOLBAR"), _("T&oolbar"), L"", wxITEM_CHECK);
	view->Append(XRCID("ID_VIEW_QUICKCONNECT"), _("&Quickconnect bar"), L"", wxITEM_CHECK);
	view->Append(XRCID("ID_VIEW_MESSAGELOG"), _("&Message log"), L"", wxITEM_CHECK);
	view->Append(XRCID("ID_VIEW_LOCALTREE"), _("&Local directory tree"), L"", wxITEM_CHECK);
	view->Append(XRCID("ID_VIEW_REMOTETREE"), _("R&emote directory tree"), L"", wxITEM_CHECK);
	view->Append(XRCID("ID_VIEW_QUEUE"), _("&Transfer queue"), L"", wxITEM_CHECK);

	wxMenu* transfer = new wxMenu;
	Append(transfer, _("&Transfer"));

	accel.FromString(L"CTRL+P");
	transfer->Append(XRCID("ID_MENU_TRANSFER_PROCESSQUEUE"), _("Process &Queue"), L"", wxITEM_CHECK)->SetAccel(&accel);
	transfer->AppendSeparator();
	transfer->Append(XRCID("ID_MENU_TRANSFER_FILEEXISTS"), _("&Default file exists action..."));

	wxMenu* type = new wxMenu;
	transfer->Append(XRCID("ID_MENU_TRANSFER_TYPE"), _("Transfer &type"), type);

	type->Append(XRCID("ID_MENU_TRANSFER_TYPE_AUTO"), _("&Auto"), L"", wxITEM_RADIO);
	type->Append(XRCID("ID_MENU_TRANSFER_TYPE_ASCII"), _("A&SCII"), L"", wxITEM_RADIO);
	type->Append(XRCID("ID_MENU_TRANSFER_TYPE_BINARY"), _("&Binary"), L"", wxITEM_RADIO);
	accel.FromString(L"CTRL+U");

	transfer->Append(XRCID("ID_MENU_TRANSFER_PRESERVETIMES"), _("&Preserve timestamps of transferred files"), L"", wxITEM_CHECK)->SetAccel(&accel);

	wxMenu* speed = new wxMenu;
	transfer->AppendSubMenu(speed, _("&Speed limits"));

	speed->Append(XRCID("ID_MENU_TRANSFER_SPEEDLIMITS_ENABLE"), _("&Enable"), L"", wxITEM_CHECK);
	speed->Append(XRCID("ID_MENU_TRANSFER_SPEEDLIMITS_CONFIGURE"), _("&Configure..."));

	transfer->AppendSeparator();
	accel.FromString(L"CTRL+M");
	transfer->Append(XRCID("ID_MENU_TRANSFER_MANUAL"), _("&Manual transfer..."))->SetAccel(&accel);

	wxMenu* server = new wxMenu;
	Append(server, _("&Server"));
	accel.FromString(L"CTRL+.");
	server->Append(XRCID("ID_CANCEL"), _("C&ancel current operation"))->SetAccel(&accel);
	server->AppendSeparator();
	accel.FromString(L"CTRL+R");
	server->Append(XRCID("ID_MENU_SERVER_RECONNECT"), _("&Reconnect"))->SetAccel(&accel);
	accel.FromString(L"CTRL+D");
	server->Append(XRCID("ID_MENU_SERVER_DISCONNECT"), _("&Disconnect"))->SetAccel(&accel);
	server->AppendSeparator();
	accel.FromString(L"F3");
	server->Append(XRCID("ID_MENU_SERVER_SEARCH"), _("&Search remote files..."), _("Search server for files"))->SetAccel(&accel);
	server->Append(XRCID("ID_MENU_SERVER_CMD"), _("Enter &custom command..."), _("Send custom command to the server otherwise not available"));
	server->Append(XRCID("ID_MENU_SERVER_VIEWHIDDEN"), _("Force showing &hidden files"), L"", wxITEM_CHECK);

	wxMenu* bookmarks = new wxMenu;
	Append(bookmarks, _("&Bookmarks"));

	accel.FromString(L"CTRL+B");
	bookmarks->Append(XRCID("ID_BOOKMARK_ADD"), _("&Add bookmark..."))->SetAccel(&accel);
	accel.FromString(L"CTRL+SHIFT+B");
	bookmarks->Append(XRCID("ID_BOOKMARK_MANAGE"), _("&Manage bookmarks..."))->SetAccel(&accel);

	wxMenu* help = new wxMenu;
	Append(help, _("&Help"));

#if FZ_MANUALUPDATECHECK
	if (options_.get_int(OPTION_DEFAULT_DISABLEUPDATECHECK) == 0) {
		help->Append(XRCID("ID_CHECKFORUPDATES"), _("Check for &updates..."), _("Check for newer versions of FileZilla"));
		help->AppendSeparator();
	}
#endif
	help->Append(XRCID("ID_MENU_HELP_WELCOME"), _("Show &welcome dialog..."));
	help->Append(XRCID("ID_MENU_HELP_GETTINGHELP"), _("&Getting help..."));
	help->Append(XRCID("ID_MENU_HELP_BUGREPORT"), _("&Report a bug..."));

#ifndef FZ_MAC
	help->AppendSeparator();
#endif
	help->Append(XRCID("wxID_ABOUT"), _("&About..."), _("Display about dialog"));

	if (options_.get_int(OPTION_DEBUG_MENU)) {
		wxMenu* debug = new wxMenu;
		debug->Append(XRCID("ID_CLEARCACHE_LAYOUT"), _("Clear &layout cache"));
		debug->Append(XRCID("ID_CIPHERS"), _("&TLS Ciphers"), _("Shows available TLS ciphers"));
		debug->Append(XRCID("ID_CLEAR_UPDATER"), _("Clear auto&update data"));
		Append(debug, _("&Debug"));
	}

	UpdateBookmarkMenu();

	Check(XRCID("ID_MENU_SERVER_VIEWHIDDEN"), options_.get_int(OPTION_VIEW_HIDDEN_FILES) ? true : false);

	int mode = options_.get_int(OPTION_COMPARISONMODE);
	if (mode != 1) {
		Check(XRCID("ID_COMPARE_SIZE"), true);
	}
	else {
		Check(XRCID("ID_COMPARE_DATE"), true);
	}

	Check(XRCID("ID_COMPARE_HIDEIDENTICAL"), options_.get_int(OPTION_COMPARE_HIDEIDENTICAL) != 0);
	Check(XRCID("ID_VIEW_QUICKCONNECT"), options_.get_int(OPTION_SHOW_QUICKCONNECT) != 0);
	Check(XRCID("ID_VIEW_TOOLBAR"), options_.get_int(OPTION_TOOLBAR_HIDDEN) == 0);
	Check(XRCID("ID_VIEW_MESSAGELOG"), options_.get_int(OPTION_SHOW_MESSAGELOG) != 0);
	Check(XRCID("ID_VIEW_QUEUE"), options_.get_int(OPTION_SHOW_QUEUE) != 0);
	Check(XRCID("ID_VIEW_LOCALTREE"), options_.get_int(OPTION_SHOW_TREE_LOCAL) != 0);
	Check(XRCID("ID_VIEW_REMOTETREE"), options_.get_int(OPTION_SHOW_TREE_REMOTE) != 0);
	Check(XRCID("ID_MENU_VIEW_FILELISTSTATUSBAR"), options_.get_int(OPTION_FILELIST_STATUSBAR) != 0);
	Check(XRCID("ID_MENU_TRANSFER_PRESERVETIMES"), options_.get_int(OPTION_PRESERVE_TIMESTAMPS) != 0);

	switch (options_.get_int(OPTION_ASCIIBINARY))
	{
	case 1:
		Check(XRCID("ID_MENU_TRANSFER_TYPE_ASCII"), true);
		break;
	case 2:
		Check(XRCID("ID_MENU_TRANSFER_TYPE_BINARY"), true);
		break;
	default:
		Check(XRCID("ID_MENU_TRANSFER_TYPE_AUTO"), true);
		break;
	}

	UpdateSpeedLimitMenuItem();

	if (options_.get_int(OPTION_MESSAGELOG_POSITION) == 2) {
		HideItem(XRCID("ID_VIEW_MESSAGELOG"));
	}

	UpdateMenubarState();

	mainFrame_.Connect(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(CMenuBar::OnMenuEvent), 0, this);

	CContextManager::Get()->RegisterHandler(this, STATECHANGE_REMOTE_IDLE, true);
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_SERVER, true);
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_SYNC_BROWSE, true);
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_COMPARISON, true);

	CContextManager::Get()->RegisterHandler(this, STATECHANGE_QUEUEPROCESSING, false);
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_CHANGEDCONTEXT, false);

	CContextManager::Get()->RegisterHandler(this, STATECHANGE_GLOBALBOOKMARKS, false);

	options_.watch(OPTION_ASCIIBINARY, this);
	options_.watch(OPTION_PRESERVE_TIMESTAMPS, this);
	options_.watch(OPTION_TOOLBAR_HIDDEN, this);
	options_.watch(OPTION_SHOW_MESSAGELOG, this);
	options_.watch(OPTION_SHOW_QUEUE, this);
	options_.watch(OPTION_SHOW_TREE_LOCAL, this);
	options_.watch(OPTION_SHOW_TREE_REMOTE, this);
	options_.watch(OPTION_MESSAGELOG_POSITION, this);
	options_.watch(OPTION_COMPARISONMODE, this);
	options_.watch(OPTION_COMPARE_HIDEIDENTICAL, this);
	options_.watch(OPTION_SPEEDLIMIT_ENABLE, this);
	options_.watch(OPTION_SPEEDLIMIT_INBOUND, this);
	options_.watch(OPTION_SPEEDLIMIT_OUTBOUND, this);

#ifdef FZ_MAC
	wxMenu* editMenu = nullptr;
	wxMenuItem* dirsItem = FindItem(XRCID("ID_MENU_EDIT_SANDBOX_DIRECTORIES"), &editMenu);
	if (editMenu && dirsItem) {
#if USE_MAC_SANDBOX
		editMenu->Bind(wxEVT_COMMAND_MENU_SELECTED, [pMainFrame](wxCommandEvent&)
			{
				OSXSandboxUserdirsDialog dlg;
				dlg.Run(pMainFrame);
			}, XRCID("ID_MENU_EDIT_SANDBOX_DIRECTORIES"));
#else		
		editMenu->Delete(dirsItem);
#endif
	}
#endif
}

CMenuBar::~CMenuBar()
{
	options_.unwatch_all(this);

	for (auto hidden_menu : m_hidden_items) {
		for (auto hidden_item : hidden_menu.second) {
			delete hidden_item.second;
		}
	}

	mainFrame_.Disconnect(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(CMenuBar::OnMenuEvent), 0, this);
}

void CMenuBar::UpdateBookmarkMenu()
{
	wxMenu* pMenu;
	if (!FindItem(XRCID("ID_BOOKMARK_ADD"), &pMenu)) {
		return;
	}

	// Delete old bookmarks
	for (std::map<int, wxString>::const_iterator iter = m_bookmark_menu_id_map_global.begin(); iter != m_bookmark_menu_id_map_global.end(); ++iter) {
		pMenu->Delete(iter->first);
	}
	m_bookmark_menu_id_map_global.clear();

	for (std::map<int, wxString>::const_iterator iter = m_bookmark_menu_id_map_site.begin(); iter != m_bookmark_menu_id_map_site.end(); ++iter) {
		pMenu->Delete(iter->first);
	}
	m_bookmark_menu_id_map_site.clear();

	// Delete the separators
	while (pMenu->GetMenuItemCount() > 2) {
		wxMenuItem* pSeparator = pMenu->FindItemByPosition(2);
		if (pSeparator) {
			pMenu->Delete(pSeparator);
		}
	}

	auto ids = m_bookmark_menu_ids.begin();

	// Insert global bookmarks
	std::vector<std::wstring> global_bookmarks;
	if (CBookmarksDialog::GetGlobalBookmarks(global_bookmarks) && !global_bookmarks.empty()) {
		pMenu->AppendSeparator();

		for (auto const& bookmark : global_bookmarks) {
			int id;
			if (ids == m_bookmark_menu_ids.end()) {
				id = wxNewId();
				m_bookmark_menu_ids.push_back(id);
				ids = m_bookmark_menu_ids.end();
			}
			else {
				id = *ids;
				++ids;
			}
			std::wstring name(LabelEscape(bookmark));
			pMenu->Append(id, name);

			m_bookmark_menu_id_map_global[id] = bookmark;
		}
	}

	// Insert site-specific bookmarks
	CContextControl* pContextControl = mainFrame_.GetContextControl();
	CContextControl::_context_controls* controls = pContextControl ? pContextControl->GetCurrentControls() : 0;
	CState* pState = controls ? controls->pState : 0;
	if (!pState) {
		return;
	}

	Site site = pState->GetSite();
	if (!site) {
		site = pState->GetLastSite();
	}
	if (site.m_bookmarks.empty()) {
		return;
	}

	pMenu->AppendSeparator();

	for (auto const& bookmark : site.m_bookmarks) {
		int id;
		if (ids == m_bookmark_menu_ids.end()) {
			id = wxNewId();
			m_bookmark_menu_ids.push_back(id);
			ids = m_bookmark_menu_ids.end();
		}
		else {
			id = *ids;
			++ids;
		}
		wxString name(LabelEscape(bookmark.m_name));
		pMenu->Append(id, name);

		m_bookmark_menu_id_map_site[id] = bookmark.m_name;
	}
}

void CMenuBar::OnMenuEvent(wxCommandEvent& event)
{
	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState) {
		return;
	}

	std::map<int, wxString>::const_iterator iter = m_bookmark_menu_id_map_site.find(event.GetId());
	if (iter != m_bookmark_menu_id_map_site.end()) {
		// We hit a site-specific bookmark
		CContextControl* pContextControl = mainFrame_.GetContextControl();
		CContextControl::_context_controls* controls = pContextControl ? pContextControl->GetCurrentControls() : 0;
		CState* pState = controls ? controls->pState : 0;
		if (!pState) {
			return;
		}

		Site site = pState->GetSite();
		if (!site) {
			site = pState->GetLastSite();
		}


		for (auto const& bookmark : site.m_bookmarks) {
			if (bookmark.m_name == iter->second) {

				pState->SetSyncBrowse(false);
				if (!bookmark.m_remoteDir.empty() && pState->IsRemoteIdle(true)) {
					Site const& activeSite = pState->GetSite();
					if (!activeSite || activeSite != site) {
						mainFrame_.ConnectToSite(site, bookmark);
						break;
					}
					else {
						pState->ChangeRemoteDir(bookmark.m_remoteDir, std::wstring(), 0, false, bookmark.m_comparison);
					}
				}
				if (!bookmark.m_localDir.empty()) {
					bool set = pState->SetLocalDir(bookmark.m_localDir);

					if (set && bookmark.m_sync) {
						wxASSERT(!bookmark.m_remoteDir.empty());
						pState->SetSyncBrowse(true, bookmark.m_remoteDir);
					}
				}

				if (bookmark.m_comparison && pState->GetComparisonManager()) {
					pState->GetComparisonManager()->CompareListings();
				}

				break;
			}
		}

		return;
	}

	std::map<int, wxString>::const_iterator iter2 = m_bookmark_menu_id_map_global.find(event.GetId());
	if (iter2 != m_bookmark_menu_id_map_global.end()) {
		// We hit a global bookmark
		wxString local_dir;
		CServerPath remote_dir;
		bool sync;
		bool comparison;
		if (!CBookmarksDialog::GetBookmark(iter2->second, local_dir, remote_dir, sync, comparison)) {
			return;
		}

		pState->SetSyncBrowse(false);
		if (!remote_dir.empty() && pState->IsRemoteIdle(true)) {
			Site const& activeSite = pState->GetSite();
			if (activeSite) {
				CServerPath current_remote_path = pState->GetRemotePath();
				if (!current_remote_path.empty() && current_remote_path.GetType() != remote_dir.GetType()) {
					wxMessageBoxEx(_("Selected global bookmark and current server use a different server type.\nUse site-specific bookmarks for this server."), _("Bookmark"), wxICON_EXCLAMATION, this);
					return;
				}
				pState->ChangeRemoteDir(remote_dir, std::wstring(), 0, false, comparison);
			}
		}
		if (!local_dir.empty()) {
			bool set = pState->SetLocalDir(local_dir.ToStdWstring());

			if (set && sync) {
				wxASSERT(!remote_dir.empty());
				pState->SetSyncBrowse(true, remote_dir);
			}
		}

		if (comparison && pState->GetComparisonManager()) {
			pState->GetComparisonManager()->CompareListings();
		}

		return;
	}

	event.Skip();
}

void CMenuBar::OnStateChange(CState* pState, t_statechange_notifications notification, std::wstring const&, const void*)
{
	switch (notification)
	{
	case STATECHANGE_CHANGEDCONTEXT:
		UpdateMenubarState();
		UpdateBookmarkMenu();
		break;
	case STATECHANGE_GLOBALBOOKMARKS:
		UpdateBookmarkMenu();
		break;
	case STATECHANGE_SERVER:
	case STATECHANGE_REMOTE_IDLE:
		UpdateMenubarState();
		UpdateBookmarkMenu();
		break;
	case STATECHANGE_QUEUEPROCESSING:
		{
			const bool check = mainFrame_.GetQueue() && mainFrame_.GetQueue()->IsActive() != 0;
			Check(XRCID("ID_MENU_TRANSFER_PROCESSQUEUE"), check);
		}
		break;
	case STATECHANGE_SYNC_BROWSE:
		{
			bool is_sync_browse = pState && pState->GetSyncBrowse();
			Check(XRCID("ID_TOOLBAR_SYNCHRONIZED_BROWSING"), is_sync_browse);
		}
		break;
	case STATECHANGE_COMPARISON:
		{
			bool is_comparing = pState && pState->GetComparisonManager()->IsComparing();
			Check(XRCID("ID_TOOLBAR_COMPARISON"), is_comparing);
		}
		break;
	default:
		break;
	}
}

void CMenuBar::OnOptionsChanged(watched_options const& options)
{
	if (options.test(OPTION_ASCIIBINARY)) {
		switch (options_.get_int(OPTION_ASCIIBINARY))
		{
		default:
			Check(XRCID("ID_MENU_TRANSFER_TYPE_AUTO"), true);
			break;
		case 1:
			Check(XRCID("ID_MENU_TRANSFER_TYPE_ASCII"), true);
			break;
		case 2:
			Check(XRCID("ID_MENU_TRANSFER_TYPE_BINARY"), true);
			break;
		}
	}
	if (options.test(OPTION_PRESERVE_TIMESTAMPS)) {
		Check(XRCID("ID_MENU_TRANSFER_PRESERVETIMES"), options_.get_int(OPTION_PRESERVE_TIMESTAMPS) != 0);
	}
	if (options.test(OPTION_SHOW_TREE_LOCAL)) {
		Check(XRCID("ID_VIEW_LOCALTREE"), options_.get_int(OPTION_SHOW_TREE_LOCAL) != 0);
	}
	if (options.test(OPTION_SHOW_TREE_REMOTE)) {
		Check(XRCID("ID_VIEW_REMOTETREE"), options_.get_int(OPTION_SHOW_TREE_REMOTE) != 0);
	}
	if (options.test(OPTION_SHOW_QUICKCONNECT)) {
		Check(XRCID("ID_VIEW_QUICKCONNECT"), options_.get_int(OPTION_SHOW_QUICKCONNECT) != 0);
	}
	if (options.test(OPTION_TOOLBAR_HIDDEN)) {
		Check(XRCID("ID_VIEW_TOOLBAR"), options_.get_int(OPTION_TOOLBAR_HIDDEN) == 0);
	}
	if (options.test(OPTION_SHOW_MESSAGELOG)) {
		Check(XRCID("ID_VIEW_MESSAGELOG"), options_.get_int(OPTION_SHOW_MESSAGELOG) != 0);
	}
	if (options.test(OPTION_SHOW_QUEUE)) {
		Check(XRCID("ID_VIEW_QUEUE"), options_.get_int(OPTION_SHOW_QUEUE) != 0);
	}
	if (options.test(OPTION_COMPARE_HIDEIDENTICAL)) {
		Check(XRCID("ID_COMPARE_HIDEIDENTICAL"), options_.get_int(OPTION_COMPARE_HIDEIDENTICAL) != 0);
	}
	if (options.test(OPTION_COMPARISONMODE)) {
		if (options_.get_int(OPTION_COMPARISONMODE) != 1) {
			Check(XRCID("ID_COMPARE_SIZE"), true);
		}
		else {
			Check(XRCID("ID_COMPARE_DATE"), true);
		}
	}
	if (options.test(OPTION_MESSAGELOG_POSITION)) {
		if (options_.get_int(OPTION_MESSAGELOG_POSITION) == 2) {
			HideItem(XRCID("ID_VIEW_MESSAGELOG"));
		}
		else {
			ShowItem(XRCID("ID_VIEW_MESSAGELOG"));
			Check(XRCID("ID_VIEW_MESSAGELOG"), options_.get_int(OPTION_SHOW_MESSAGELOG) != 0);
		}
	}
	if (options.test(OPTION_SPEEDLIMIT_ENABLE) || options.test(OPTION_SPEEDLIMIT_INBOUND) || options.test(OPTION_SPEEDLIMIT_OUTBOUND)) {
		UpdateSpeedLimitMenuItem();
	}
}

void CMenuBar::UpdateSpeedLimitMenuItem()
{
	bool enable = options_.get_int(OPTION_SPEEDLIMIT_ENABLE) != 0;

	int downloadLimit = options_.get_int(OPTION_SPEEDLIMIT_INBOUND);
	int uploadLimit = options_.get_int(OPTION_SPEEDLIMIT_OUTBOUND);

	if (!downloadLimit && !uploadLimit) {
		enable = false;
	}

	Check(XRCID("ID_MENU_TRANSFER_SPEEDLIMITS_ENABLE"), enable);
}

void CMenuBar::UpdateMenubarState()
{
	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState) {
		return;
	}

	Site const& site = pState->GetSite();
	const bool idle = pState->IsRemoteIdle();

	Enable(XRCID("ID_MENU_SERVER_DISCONNECT"), site && idle);
	Enable(XRCID("ID_CANCEL"), site && !idle);
	Enable(XRCID("ID_MENU_SERVER_CMD"), site && idle);
	Enable(XRCID("ID_MENU_FILE_COPYSITEMANAGER"), site.operator bool());
	Enable(XRCID("ID_TOOLBAR_SYNCHRONIZED_BROWSING"), site.operator bool());

	Check(XRCID("ID_TOOLBAR_COMPARISON"), pState->GetComparisonManager()->IsComparing());
	Check(XRCID("ID_TOOLBAR_SYNCHRONIZED_BROWSING"), pState->GetSyncBrowse());

	bool canReconnect;
	if (site || !idle) {
		canReconnect = false;
	}
	else {
		canReconnect = static_cast<bool>(pState->GetLastSite());
	}
	Enable(XRCID("ID_MENU_SERVER_RECONNECT"), canReconnect);

	Enable(XRCID("ID_MENU_TRANSFER_TYPE"), !site || CServer::ProtocolHasFeature(site.server.GetProtocol(), ProtocolFeature::DataTypeConcept));
	Enable(XRCID("ID_MENU_TRANSFER_PRESERVETIMES"), !site || CServer::ProtocolHasFeature(site.server.GetProtocol(), ProtocolFeature::PreserveTimestamp));
	Enable(XRCID("ID_MENU_SERVER_CMD"), !site || CServer::ProtocolHasFeature(site.server.GetProtocol(), ProtocolFeature::EnterCommand));
}

bool CMenuBar::ShowItem(int id)
{
	for (auto menu_iter = m_hidden_items.begin(); menu_iter != m_hidden_items.end(); ++menu_iter) {
		int offset = 0;

		for (auto iter = menu_iter->second.begin(); iter != menu_iter->second.end(); ++iter) {
			if (iter->second->GetId() != id) {
				offset++;
				continue;
			}

			menu_iter->first->Insert(iter->first - offset, iter->second);
			menu_iter->second.erase(iter);
			if (menu_iter->second.empty()) {
				m_hidden_items.erase(menu_iter);
			}

			return true;
		}
	}
	return false;
}

bool CMenuBar::HideItem(int id)
{
	wxMenu* pMenu = 0;
	wxMenuItem* pItem = FindItem(id, &pMenu);
	if (!pItem || !pMenu) {
		return false;
	}

	size_t pos;
	pItem = pMenu->FindChildItem(id, &pos);
	if (!pItem) {
		return false;
	}

	pMenu->Remove(pItem);

	auto menu_iter = m_hidden_items.insert(std::make_pair(pMenu, std::map<int, wxMenuItem*>())).first;

	for (auto iter = menu_iter->second.begin(); iter != menu_iter->second.end(); ++iter) {
		if (iter->first > static_cast<int>(pos)) {
			break;
		}

		pos++;
	}

	menu_iter->second[static_cast<int>(pos)] = pItem;

	return true;
}
