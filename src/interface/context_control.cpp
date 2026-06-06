#include "filezilla.h"
#include "cmdline.h"
#include "commandqueue.h"
#include "context_control.h"
#include "filelist_statusbar.h"
#include "filezillaapp.h"
#include "graphics.h"
#include "list_search_panel.h"
#include "local_recursive_operation.h"
#include "LocalListView.h"
#include "LocalTreeView.h"
#include "Mainfrm.h"
#include "Options.h"
#include "queue.h"
#include "remote_recursive_operation.h"
#include "recursive_operation_status.h"
#include "RemoteListView.h"
#include "RemoteTreeView.h"
#include "sitemanager.h"
#include "splitter.h"
#include "view.h"
#include "viewheader.h"
#include "xmlfunctions.h"

#ifdef USE_MAC_SANDBOX
#include "osx_sandbox_userdirs.h"
#endif

#include <wx/menu.h>
#include <wx/wupdlock.h>

#include <array>

using namespace std::literals;

wxDECLARE_EVENT(fzEVT_TAB_CLOSING_DEFERRED, wxCommandEvent);
wxDEFINE_EVENT(fzEVT_TAB_CLOSING_DEFERRED, wxCommandEvent);

BEGIN_EVENT_TABLE(CContextControl, wxSplitterWindow)
EVT_MENU(XRCID("ID_TABCONTEXT_REFRESH"), CContextControl::OnTabRefresh)
EVT_COMMAND(wxID_ANY, fzEVT_TAB_CLOSING_DEFERRED, CContextControl::OnTabClosing_Deferred)
EVT_MENU(XRCID("ID_TABCONTEXT_CLOSE"), CContextControl::OnTabContextClose)
EVT_MENU(XRCID("ID_TABCONTEXT_CLOSEOTHERS"), CContextControl::OnTabContextCloseOthers)
EVT_MENU(XRCID("ID_TABCONTEXT_NEW"), CContextControl::OnTabContextNew)
END_EVENT_TABLE()

CContextControl::CContextControl(CMainFrame& mainFrame)
	: m_mainFrame(mainFrame)
{
	wxASSERT(!CContextManager::Get()->HandlerCount(STATECHANGE_CHANGEDCONTEXT));
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_CHANGEDCONTEXT, false);
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_SERVER, false);
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_REWRITE_CREDENTIALS, false);
}

CContextControl::~CContextControl()
{
}

void CContextControl::Create(wxWindow *parent)
{
	wxSplitterWindow::Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_NOBORDER);
}

bool CContextControl::CreateTab()
{
	CLocalPath localPath;
	Site site;
	CServerPath remotePath;

	auto const* controls = GetCurrentControls();
	if (controls && controls->pState) {
		localPath = controls->pState->GetLocalDir();
		site = controls->pState->GetLastSite();
		remotePath = controls->pState->GetLastServerPath();
	}
	return CreateTab(localPath, site, remotePath);
}

bool CContextControl::CreateTab(CLocalPath const& localPath, Site const& site, CServerPath const& remotePath)
{
	wxGetApp().AddStartupProfileRecord("CContextControl::CreateTab"sv);

	if (GetTabCount() >= 200) {
		wxBell();
		return false;
	}

	{
	#ifdef __WXMSW__
		// Some reparenting is being done when creating tabs. Reparenting of frozen windows isn't working
		// on OS X.
		wxWindowUpdateLocker lock(this);
	#endif

		CState* pState = 0;

		// See if we can reuse an existing context
		for (size_t i = 0; i < m_context_controls.size(); i++) {
			if (m_context_controls[i].used() || !m_context_controls[i].pState) {
				continue;
			}

			if (m_context_controls[i].pState->IsRemoteConnected() ||
				!m_context_controls[i].pState->IsRemoteIdle())
			{
				continue;
			}

			pState = m_context_controls[i].pState;
			m_context_controls.erase(m_context_controls.begin() + i);
			if (m_current_context_controls > (int)i) {
				--m_current_context_controls;
			}
			break;
		}
		if (!pState) {
			pState = CContextManager::Get()->CreateState(m_mainFrame);
			if (!pState) {
				return false;
			}
		}

		pState->SetLastSite(site, remotePath);

		CreateContextControls(*pState);


		pState->GetLocalRecursiveOperation()->SetQueue(m_mainFrame.GetQueue());
		pState->GetRemoteRecursiveOperation()->SetQueue(m_mainFrame.GetQueue());

		if (localPath.empty() || !pState->SetLocalDir(localPath)) {
#ifdef USE_MAC_SANDBOX
			auto const dirs = OSXSandboxUserdirs::Get().GetDirs();
			if (dirs.empty() || !pState->SetLocalDir(dirs.front())) {
				pState->SetLocalDir(L"/");
			}
#else
			std::wstring const homeDir = wxGetHomeDir().ToStdWstring();
			if (!pState->SetLocalDir(homeDir)) {
				pState->SetLocalDir(L"/");
			}
#endif
		}

		CContextManager::Get()->SetCurrentContext(pState);
	}

	if (m_tabs) {
		m_tabs->SetSelection(m_tabs->GetPageCount() - 1);
	}

	return true;
}

void CContextControl::CreateContextControls(CState& state)
{
	wxGetApp().AddStartupProfileRecord("CContextControl::CreateContextControls"sv);
	wxWindow* parent = this;

#ifdef __WXGTK__
	// This prevents some ugly flickering on tab creation.
	const wxPoint initial_position(1000000, 1000000);
#else
	const wxPoint initial_position(wxDefaultPosition);
#endif
	wxSize paneSizes[3] = { {-1, -1}, {-1, -1}, {-1, -1}};
	std::tuple<double, int, int> splitterPositions;

	if (!m_context_controls.empty()) {

		auto & currentControls = m_context_controls[m_current_context_controls != -1 ? m_current_context_controls : 0];

		if (currentControls.pViewSplitter) {
			paneSizes[0] = currentControls.pViewSplitter->GetSize();
		}
		if (currentControls.pLocalSplitter) {
			paneSizes[1] = currentControls.pLocalSplitter->GetSize();
		}
		if (currentControls.pRemoteSplitter) {
			paneSizes[2] = currentControls.pRemoteSplitter->GetSize();
		}

		splitterPositions = currentControls.GetSplitterPositions();
		if (currentControls.pLocalListView) {
			currentControls.pLocalListView->SaveColumnSettings(m_mainFrame.GetOptions(), OPTION_LOCALFILELIST_COLUMN_WIDTHS, OPTION_LOCALFILELIST_COLUMN_SHOWN, OPTION_LOCALFILELIST_COLUMN_ORDER);
		}
		if (currentControls.pRemoteListView) {
			currentControls.pRemoteListView->SaveColumnSettings(m_mainFrame.GetOptions(), OPTION_REMOTEFILELIST_COLUMN_WIDTHS, OPTION_REMOTEFILELIST_COLUMN_SHOWN, OPTION_REMOTEFILELIST_COLUMN_ORDER);
		}

		if (!m_tabs) {
			m_tabs = new wxAuiNotebookEx();

			m_tabs->Create(this, wxID_ANY, initial_position, paneSizes[0], wxNO_BORDER | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_WINDOWLIST_BUTTON | wxAUI_NB_CLOSE_ON_ALL_TABS | wxAUI_NB_TAB_MOVE);
			m_tabs->SetExArtProvider();
			m_tabs->SetSelectedFont(*wxNORMAL_FONT);
			m_tabs->SetMeasuringFont(*wxNORMAL_FONT);

			if (currentControls.pViewSplitter) {
				currentControls.pViewSplitter->Reparent(m_tabs);
			}

			m_tabs->AddPage(currentControls.pViewSplitter, currentControls.pState->GetTitle());
			m_tabs->SetTabColour(0, site_colour_to_wx(currentControls.pState->GetSite().m_colour));
			ReplaceWindow(currentControls.pViewSplitter, m_tabs);

			m_tabs->Connect(wxEVT_COMMAND_AUINOTEBOOK_PAGE_CHANGED, wxAuiNotebookEventHandler(CContextControl::OnTabChanged), 0, this);
			m_tabs->Connect(wxEVT_COMMAND_AUINOTEBOOK_PAGE_CLOSE, wxAuiNotebookEventHandler(CContextControl::OnTabClosing), 0, this);
			m_tabs->Connect(wxEVT_COMMAND_AUINOTEBOOK_BG_DCLICK, wxAuiNotebookEventHandler(CContextControl::OnTabBgDoubleclick), 0, this);
			m_tabs->Connect(wxEVT_COMMAND_AUINOTEBOOK_TAB_MIDDLE_UP, wxAuiNotebookEventHandler(CContextControl::OnTabClosing), 0, this);
			m_tabs->Connect(wxEVT_COMMAND_AUINOTEBOOK_TAB_RIGHT_UP, wxAuiNotebookEventHandler(CContextControl::OnTabRightclick), 0, this);

#ifdef __WXMAC__
			// We need to select the first page as the default selection is -1. Not doing so prevents selecting other pages later on.
			m_tabs->SetSelection(0);
#endif
		}

		parent = m_tabs;
	}

	CContextControl::_context_controls context_controls;
	context_controls.pState = &state;

	context_controls.pViewSplitter = new CSplitterWindowEx(parent, -1, initial_position, paneSizes[0], wxSP_NOBORDER  | wxSP_LIVE_UPDATE);
	context_controls.pViewSplitter->SetMinSize(wxDefaultSize);
	context_controls.pViewSplitter->SetMinimumPaneSize(50, 100);
	context_controls.pViewSplitter->SetSashGravity(0.5);

	context_controls.pLocalSplitter = new CSplitterWindowEx(context_controls.pViewSplitter, -1, wxDefaultPosition, paneSizes[1], wxSP_NOBORDER  | wxSP_LIVE_UPDATE);
	context_controls.pLocalSplitter->SetMinSize(wxDefaultSize);
	context_controls.pLocalSplitter->SetMinimumPaneSize(50, 100);

	context_controls.pRemoteSplitter = new CSplitterWindowEx(context_controls.pViewSplitter, -1, wxDefaultPosition, paneSizes[2], wxSP_NOBORDER  | wxSP_LIVE_UPDATE);
	context_controls.pRemoteSplitter->SetMinSize(wxDefaultSize);
	context_controls.pRemoteSplitter->SetMinimumPaneSize(50, 100);

	context_controls.pLocalTreeViewPanel = new CView(context_controls.pLocalSplitter);
	context_controls.pLocalListViewPanel = new CView(context_controls.pLocalSplitter);
	context_controls.pLocalTreeView = new CLocalTreeView(context_controls.pLocalTreeViewPanel, -1, state, m_mainFrame.GetQueue(), m_mainFrame.GetOptions(), m_mainFrame.GetLoginManager());
	context_controls.pLocalListView = new CLocalListView(context_controls.pLocalListViewPanel, state, m_mainFrame.GetQueue(), m_mainFrame.GetOptions(), m_mainFrame.GetTimeFormatter(), m_mainFrame.GetLoginManager(), m_mainFrame.GetEditHandler());
	context_controls.pLocalTreeViewPanel->SetWindow(context_controls.pLocalTreeView);
	context_controls.pLocalListViewPanel->SetWindow(context_controls.pLocalListView);

	context_controls.pRemoteTreeViewPanel = new CView(context_controls.pRemoteSplitter);
	context_controls.pRemoteListViewPanel = new CView(context_controls.pRemoteSplitter);
	context_controls.pRemoteTreeView = new CRemoteTreeView(context_controls.pRemoteTreeViewPanel, -1, state, m_mainFrame.GetQueue(), m_mainFrame.GetOptions(), m_mainFrame.GetLoginManager());
	context_controls.pRemoteListView = new CRemoteListView(context_controls.pRemoteListViewPanel, state, m_mainFrame.GetQueue(), m_mainFrame.GetOptions(), m_mainFrame.GetTimeFormatter(), m_mainFrame.GetLoginManager(), m_mainFrame.GetEditHandler());
	context_controls.pRemoteTreeViewPanel->SetWindow(context_controls.pRemoteTreeView);
	context_controls.pRemoteListViewPanel->SetWindow(context_controls.pRemoteListView);

	bool show_filelist_statusbars = m_mainFrame.GetOptions().get_int(OPTION_FILELIST_STATUSBAR) != 0;

	CFilelistStatusBar* pLocalFilelistStatusBar = new CFilelistStatusBar(context_controls.pLocalListViewPanel, m_mainFrame.GetOptions());
	if (!show_filelist_statusbars) {
		pLocalFilelistStatusBar->Hide();
	}
	context_controls.pLocalListViewPanel->SetStatusBar(pLocalFilelistStatusBar);
	if (context_controls.pLocalListView) {
		context_controls.pLocalListView->SetFilelistStatusBar(pLocalFilelistStatusBar);
	}
	pLocalFilelistStatusBar->SetConnected(true);

	CFilelistStatusBar* pRemoteFilelistStatusBar = new CFilelistStatusBar(context_controls.pRemoteListViewPanel, m_mainFrame.GetOptions());
	if (!show_filelist_statusbars) {
		pRemoteFilelistStatusBar->Hide();
	}
	context_controls.pRemoteListViewPanel->SetStatusBar(pRemoteFilelistStatusBar);
	if (context_controls.pRemoteListView) {
		context_controls.pRemoteListView->SetFilelistStatusBar(pRemoteFilelistStatusBar);
	}

	auto localRecursiveStatus = new CRecursiveOperationStatus(context_controls.pLocalListViewPanel, state, true);
	context_controls.pLocalListViewPanel->SetFooter(localRecursiveStatus);

	auto remoteRecursiveStatus = new CRecursiveOperationStatus(context_controls.pRemoteListViewPanel, state, false);
	context_controls.pRemoteListViewPanel->SetFooter(remoteRecursiveStatus);

	context_controls.pLocalListSearchPanel = new CListSearchPanel(context_controls.pLocalListViewPanel, context_controls.pLocalListView, context_controls.pState, true);
	context_controls.pLocalListViewPanel->SetSearchPanel(context_controls.pLocalListSearchPanel);

	context_controls.pRemoteListSearchPanel = new CListSearchPanel(context_controls.pRemoteListViewPanel, context_controls.pRemoteListView, context_controls.pState, false);
	context_controls.pRemoteListViewPanel->SetSearchPanel(context_controls.pRemoteListSearchPanel);

	const int layout = m_mainFrame.GetOptions().get_int(OPTION_FILEPANE_LAYOUT);
	const int swap = m_mainFrame.GetOptions().get_int(OPTION_FILEPANE_SWAP);

	if (layout == 1) {
		if (swap) {
			context_controls.pViewSplitter->SplitHorizontally(context_controls.pRemoteSplitter, context_controls.pLocalSplitter);
		}
		else {
			context_controls.pViewSplitter->SplitHorizontally(context_controls.pLocalSplitter, context_controls.pRemoteSplitter);
		}
	}
	else {
		if (swap) {
			context_controls.pViewSplitter->SplitVertically(context_controls.pRemoteSplitter, context_controls.pLocalSplitter);
		}
		else {
			context_controls.pViewSplitter->SplitVertically(context_controls.pLocalSplitter, context_controls.pRemoteSplitter);
		}
	}

	if (m_mainFrame.GetOptions().get_int(OPTION_SHOW_TREE_LOCAL)) {
		context_controls.pLocalViewHeader = new CLocalViewHeader(context_controls.pLocalTreeViewPanel, state);
		context_controls.pLocalTreeViewPanel->SetHeader(context_controls.pLocalViewHeader);
		if (layout == 3 && swap) {
			context_controls.pLocalSplitter->SplitVertically(context_controls.pLocalListViewPanel, context_controls.pLocalTreeViewPanel);
		}
		else if (layout) {
			context_controls.pLocalSplitter->SplitVertically(context_controls.pLocalTreeViewPanel, context_controls.pLocalListViewPanel);
		}
		else {
			context_controls.pLocalSplitter->SplitHorizontally(context_controls.pLocalTreeViewPanel, context_controls.pLocalListViewPanel);
		}
	}
	else {
		context_controls.pLocalTreeViewPanel->Hide();
		context_controls.pLocalViewHeader = new CLocalViewHeader(context_controls.pLocalListViewPanel, state);
		context_controls.pLocalListViewPanel->SetHeader(context_controls.pLocalViewHeader);
		context_controls.pLocalSplitter->Initialize(context_controls.pLocalListViewPanel);
	}

	if (m_mainFrame.GetOptions().get_int(OPTION_SHOW_TREE_REMOTE)) {
		context_controls.pRemoteViewHeader = new CRemoteViewHeader(context_controls.pRemoteTreeViewPanel, state);
		context_controls.pRemoteTreeViewPanel->SetHeader(context_controls.pRemoteViewHeader);
		if (layout == 3 && !swap) {
			context_controls.pRemoteSplitter->SplitVertically(context_controls.pRemoteListViewPanel, context_controls.pRemoteTreeViewPanel);
		}
		else if (layout) {
			context_controls.pRemoteSplitter->SplitVertically(context_controls.pRemoteTreeViewPanel, context_controls.pRemoteListViewPanel);
		}
		else {
			context_controls.pRemoteSplitter->SplitHorizontally(context_controls.pRemoteTreeViewPanel, context_controls.pRemoteListViewPanel);
		}
	}
	else {
		context_controls.pRemoteTreeViewPanel->Hide();
		context_controls.pRemoteViewHeader = new CRemoteViewHeader(context_controls.pRemoteListViewPanel, state);
		context_controls.pRemoteListViewPanel->SetHeader(context_controls.pRemoteViewHeader);
		context_controls.pRemoteSplitter->Initialize(context_controls.pRemoteListViewPanel);
	}

	if (layout == 3) {
		if (!swap) {
			context_controls.pRemoteSplitter->SetSashGravity(1.0);
		}
		else {
			context_controls.pLocalSplitter->SetSashGravity(1.0);
		}
	}

	if (!m_context_controls.empty()) {
		context_controls.SetSplitterPositions(splitterPositions);
	}
	else {
		context_controls.pViewSplitter->SetRelativeSashPosition(0.5);
		context_controls.pLocalSplitter->SetRelativeSashPosition(0.4);
		context_controls.pLocalSplitter->SetRelativeSashPosition(0.4);
	}

	m_mainFrame.ConnectNavigationHandler(context_controls.pLocalListView);
	m_mainFrame.ConnectNavigationHandler(context_controls.pRemoteListView);
	m_mainFrame.ConnectNavigationHandler(context_controls.pLocalTreeView);
	m_mainFrame.ConnectNavigationHandler(context_controls.pRemoteTreeView);
	m_mainFrame.ConnectNavigationHandler(context_controls.pLocalViewHeader);
	m_mainFrame.ConnectNavigationHandler(context_controls.pRemoteViewHeader);

	if (context_controls.pLocalListView && context_controls.pRemoteListView) {
		state.GetComparisonManager()->SetListings(context_controls.pLocalListView, context_controls.pRemoteListView);
	}

	if (m_tabs) {
		m_tabs->AddPage(context_controls.pViewSplitter, state.GetTitle());
	}
	else {
		Initialize(context_controls.pViewSplitter);
	}

	m_context_controls.push_back(context_controls);
}

void CContextControl::OnTabRefresh(wxCommandEvent&)
{
	if (m_right_clicked_tab == -1) {
		return;
	}

	auto * controls = GetControlsFromTabIndex(m_right_clicked_tab);
	if (controls) {
		controls->pState->RefreshLocal();
		controls->pState->RefreshRemote();
	}
}

 CContextControl::_context_controls* CContextControl::GetCurrentControls()
{
	if (m_current_context_controls == -1) {
		return 0;
	}

	return &m_context_controls[m_current_context_controls];
}

CContextControl::_context_controls* CContextControl::GetControlsFromState(CState* pState)
{
	size_t i = 0;
	for (i = 0; i < m_context_controls.size(); ++i) {
		if (m_context_controls[i].pState == pState) {
			return &m_context_controls[i];
		}
	}
	return 0;
}

bool CContextControl::CloseTab(int tab)
{
	if (!m_tabs) {
		return false;
	}
	if (tab < 0 || static_cast<size_t>(tab) >= m_tabs->GetPageCount()) {
		return false;
	}


	auto *const removeControls = GetControlsFromTabIndex(tab);

	CState *const pState = removeControls->pState;

	if (!pState->m_pCommandQueue->Idle()) {
		if (wxMessageBoxEx(_("Cannot close tab while busy.\nCancel current operation and close tab?"), _T("FileZilla"), wxYES_NO | wxICON_QUESTION) != wxYES) {
			return false;
		}
	}

#ifndef __WXMAC__
	// Some reparenting is being done when closing tabs. Reparenting of frozen windows isn't working
	// on OS X.
	wxWindowUpdateLocker lock(this);
#endif

	pState->m_pCommandQueue->Cancel();
	pState->GetLocalRecursiveOperation()->StopRecursiveOperation();
	pState->GetRemoteRecursiveOperation()->StopRecursiveOperation();

	pState->GetComparisonManager()->SetListings(0, 0);

	if (m_tabs->GetPageCount() == 2) {
		// Get rid again of tab bar
		m_tabs->Disconnect(wxEVT_COMMAND_AUINOTEBOOK_PAGE_CHANGED, wxAuiNotebookEventHandler(CContextControl::OnTabChanged), 0, this);

		int keep = tab ? 0 : 1;

		auto * keptControls = GetControlsFromTabIndex(keep);
		m_tabs->RemovePage(keep);

		CContextManager::Get()->SetCurrentContext(keptControls->pState);

		keptControls->pViewSplitter->Reparent(this);
		ReplaceWindow(m_tabs, keptControls->pViewSplitter);
		keptControls->pViewSplitter->Show();

		wxAuiNotebookEx *tabs = m_tabs;
		m_tabs = 0;

		// We don't actually delete the controls outselves, that's done by wx as part of the RemovePage call.
		removeControls->pViewSplitter = 0;

		CContextManager::Get()->SetCurrentContext(keptControls->pState);

		tabs->Destroy();
	}
	else {
		if (pState == CContextManager::Get()->GetCurrentContext()) {
			int newsel = tab + 1;
			if (newsel >= (int)m_tabs->GetPageCount()) {
				newsel = m_tabs->GetPageCount() - 2;
			}

			m_tabs->SetSelection(newsel);
			CContextManager::Get()->SetCurrentContext(GetControlsFromTabIndex(newsel)->pState);
		}

		removeControls->pViewSplitter = 0;
		m_tabs->DeletePage(tab);
	}

	pState->Disconnect();

	return true;
}

void CContextControl::OnTabBgDoubleclick(wxAuiNotebookEvent&)
{
	CreateTab();
}

void CContextControl::OnTabRightclick(wxAuiNotebookEvent& event)
{
	wxMenu menu;
	menu.Append(XRCID("ID_TABCONTEXT_NEW"), _("&Create new tab"));

	menu.AppendSeparator();
	menu.Append(XRCID("ID_TABCONTEXT_CLOSE"), _("Cl&ose tab"));
	menu.Append(XRCID("ID_TABCONTEXT_CLOSEOTHERS"), _("Close &all other tabs"));

	menu.AppendSeparator();
	menu.Append(XRCID("ID_TABCONTEXT_REFRESH"), _("&Refresh"));

	if (!m_tabs || m_tabs->GetPageCount() < 2) {
		menu.Enable(XRCID("ID_TABCONTEXT_CLOSE"), false);
		menu.Enable(XRCID("ID_TABCONTEXT_CLOSEOTHERS"), false);
	}

	m_right_clicked_tab = event.GetSelection();

	PopupMenu(&menu);
}

void CContextControl::OnTabContextClose(wxCommandEvent&)
{
	if (m_right_clicked_tab == -1) {
		return;
	}

	// Need to defer event, wxAUI would write to free'd memory
	// if we'd actually delete tab and potenially the notebook with it
	QueueEvent(new wxCommandEvent(fzEVT_TAB_CLOSING_DEFERRED, m_right_clicked_tab));
}

void CContextControl::OnTabContextCloseOthers(wxCommandEvent&)
{
	QueueEvent(new wxCommandEvent (fzEVT_TAB_CLOSING_DEFERRED, -m_right_clicked_tab - 1));
}

void CContextControl::OnTabClosing_Deferred(wxCommandEvent& event)
{
	int tab = event.GetId();
	if (tab < 0) {
		++tab;
		int count = GetTabCount();
		for (int i = count - 1; i >= 0; --i) {
			if (i != -tab) {
				CloseTab(i);
			}
		}
	}
	else {
		CloseTab(tab);
	}
}


void CContextControl::OnTabChanged(wxAuiNotebookEvent&)
{
	int i = m_tabs->GetSelection();
	auto * const controls = GetControlsFromTabIndex(i);
	if (!controls) {
		return;
	}

	CContextManager::Get()->SetCurrentContext(controls->pState);
}

void CContextControl::OnTabClosing(wxAuiNotebookEvent& event)
{
	// Need to defer event, wxAUI would write to free'd memory
	// if we'd actually delete tab and potenially the notebook with it
	QueueEvent(new wxCommandEvent(fzEVT_TAB_CLOSING_DEFERRED, event.GetSelection()));

	event.Veto();
}

int CContextControl::GetCurrentTab() const
{
	return m_tabs ? m_tabs->GetSelection() : (m_context_controls.empty() ? -1 : 0);
}

int CContextControl::GetTabCount() const
{
	return m_tabs ? m_tabs->GetPageCount() : (m_context_controls.empty() ? 0 : 1);
}

CContextControl::_context_controls* CContextControl::GetControlsFromTabIndex(int i)
{
	if (!m_tabs) {
		if (i == 0 && !m_context_controls.empty()) {
			for (auto & controls : m_context_controls) {
				if (controls.pViewSplitter != 0) {
					return &controls;
				}
			}
		}
		return 0;
	}

	wxWindow* page = m_tabs->GetPage(i);
	if (page) {
		for (auto & controls : m_context_controls) {
			if (controls.pViewSplitter == page) {
				return &controls;
			}
		}
	}

	return 0;
}

bool CContextControl::SelectTab(int i)
{
	if (i < 0) {
		return false;
	}

	if (!m_tabs) {
		if (i != 0) {
			return false;
		}

		return true;
	}

	if ((int)m_tabs->GetPageCount() <= i) {
		return false;
	}

	m_tabs->SetSelection(i);

	return true;
}

void CContextControl::AdvanceTab(bool forward)
{
	if (!m_tabs) {
		return;
	}

	m_tabs->AdvanceTab(forward);
}

void CContextControl::OnStateChange(CState* pState, t_statechange_notifications notification, std::wstring const&, const void*)
{
	if (notification == STATECHANGE_CHANGEDCONTEXT) {
		if (!pState) {
			m_current_context_controls = m_context_controls.empty() ? -1 : 0;
			return;
		}

		// Get current controls for new current context
		for (m_current_context_controls = 0; m_current_context_controls < static_cast<int>(m_context_controls.size()); ++m_current_context_controls) {
			if (m_context_controls[m_current_context_controls].pState == pState) {
				break;
			}
		}
		if (m_current_context_controls == static_cast<int>(m_context_controls.size())) {
			m_current_context_controls = -1;
		}
	}
	else if (notification == STATECHANGE_SERVER) {
		if (!m_tabs) {
			return;
		}

		CContextControl::_context_controls* controls = GetControlsFromState(pState);
		if (controls && controls->used()) {
			int i = m_tabs->GetPageIndex(controls->pViewSplitter);
			if (i != wxNOT_FOUND) {
				m_tabs->SetTabColour(i, site_colour_to_wx(controls->pState->GetSite().m_colour));
				m_tabs->SetPageText(i, controls->pState->GetTitle());
			}
		}
	}
	else if (notification == STATECHANGE_REWRITE_CREDENTIALS) {
		SaveTabs();
	}
}

void CContextControl::OnTabContextNew(wxCommandEvent&)
{
	CreateTab();
}

void CContextControl::SaveTabs()
{
	pugi::xml_document xml;
	auto tabs = xml.append_child("Tabs");

	int const currentTab = GetCurrentTab();

	for (int i = 0; i < GetTabCount(); ++i) {
		auto controls = GetControlsFromTabIndex(i);
		if (!controls || !controls->pState) {
			continue;
		}

		Site const site = controls->pState->GetLastSite();

		auto tab = tabs.append_child("Tab");
		SetServer(tab, site, m_mainFrame.GetLoginManager(), m_mainFrame.GetOptions());
		tab.append_child("Site").text().set(fz::to_utf8(site.SitePath()).c_str());
		tab.append_child("RemotePath").text().set(fz::to_utf8(controls->pState->GetLastServerPath().GetSafePath()).c_str());
		tab.append_child("LocalPath").text().set(fz::to_utf8(controls->pState->GetLocalDir().GetPath()).c_str());

		if (controls->pState->IsRemoteConnected()) {
			tab.append_attribute("connected").set_value(1);
		}
		if (i == currentTab) {
			tab.append_attribute("selected").set_value(1);
		}
	}

	m_mainFrame.GetOptions().set(OPTION_TAB_DATA, xml);
}

void CContextControl::RestoreTabs()
{
	if (!m_context_controls.empty()) {
		return;
	}

	int selected = 0;

	auto xml = m_mainFrame.GetOptions().get_xml(OPTION_TAB_DATA);

	bool selectedOnly = m_mainFrame.GetOptions().get_int(OPTION_STARTUP_ACTION) != 2;

	CCommandLine const* pCommandLine = wxGetApp().GetCommandLine();
	if (pCommandLine && pCommandLine->BlocksReconnectAtStartup()) {
		selectedOnly = true;
	}

	pugi::xml_node tabs = xml.child("Tabs");
	if (tabs) {
#ifdef __WXMSW__
		// Some reparenting is being done when creating tabs. Reparenting of frozen windows isn't working
		// on OS X.
		wxWindowUpdateLocker lock(this);
#endif
		for (auto tab = tabs.child("Tab"); tab; tab = tab.next_sibling("Tab")) {

			if (tab.attribute("selected").as_int()) {
				selected = m_context_controls.size();
			}
			else if (selectedOnly) {
				continue;
			}

			CLocalPath localPath(fz::to_wstring_from_utf8(tab.child("LocalPath").child_value()));

			Site site;
			CServerPath last_path;

			if (GetServer(tab, site) && last_path.SetSafePath(fz::to_wstring_from_utf8(tab.child("RemotePath").child_value()))) {
				std::wstring last_site_path = fz::to_wstring_from_utf8(tab.child("Site").child_value());

				std::unique_ptr<Site> ssite;
				if (!last_site_path.empty()) {
					auto ssite = CSiteManager::GetSiteByPath(m_mainFrame.GetOptions(), last_site_path, false).first;
					if (ssite && ssite->SameResource(site)) {
						site = *ssite;
					}
				}
			}

			CreateTab(localPath, site, last_path);
		}
	}

	if (m_context_controls.empty()) {
		CreateTab();
	}

	SelectTab(selected);
}

namespace {
bool SwitchFocus(wxWindow *focus, wxWindow *first, wxWindow *second)
{
	if (focus == first) {
		if (second && second->IsShownOnScreen() && second->IsEnabled()) {
			second->SetFocus();
		}
		return true;
	}
	return false;
}
}

void CContextControl::_context_controls::SwitchFocusedSide()
{
	std::array<std::pair<wxWindow*, wxWindow*>, 3> ctrls =
	{{
		{pLocalListView, pRemoteListView},
		{pLocalTreeView, pRemoteTreeView},
		{pLocalViewHeader, pRemoteViewHeader}
	}};
	auto *focus = wxWindow::FindFocus();
	while (focus) {
		for (auto & p : ctrls) {
			if (SwitchFocus(focus, p.first, p.second)) {
					return;
			}
			if (SwitchFocus(focus, p.second, p.first)) {
					return;
			}
		}
		focus = focus->GetParent();
	}
}

std::tuple<double, int, int> CContextControl::_context_controls::GetSplitterPositions()
{
	std::tuple<double, int, int> ret;

	std::get<0>(ret) = pViewSplitter ? pViewSplitter->GetRelativeSashPosition() : 0.5f;
	std::get<1>(ret) = pLocalSplitter ? pLocalSplitter->GetSashPosition() : 135;
	std::get<2>(ret) = pRemoteSplitter ? pRemoteSplitter->GetSashPosition() : 135;

	return ret;
}

void CContextControl::_context_controls::SetSplitterPositions(std::tuple<double, int, int> const& positions)
{
	if (pViewSplitter) {
		double pos = std::get<0>(positions);
		if (pos < 0 || pos > 1) {
			pos = 0.5;
		}
		pViewSplitter->SetRelativeSashPosition(pos);
	}
	if (pLocalSplitter) {
		pLocalSplitter->SetSashPosition(std::get<1>(positions));
	}
	if (pRemoteSplitter) {
		pRemoteSplitter->SetSashPosition(std::get<2>(positions));
	}
}
