#include "filezilla.h"
#include "filezillaapp.h"
#include "filter_manager.h"
#include "listingcomparison.h"
#include "Mainfrm.h"
#include "Options.h"
#include "QueueView.h"
#include "themeprovider.h"
#include "toolbar.h"

namespace {
	constexpr int toolbarStyle = wxTB_FLAT | wxTB_HORIZONTAL | wxTB_NODIVIDER;
}

#ifdef __WXMAC__
void fix_toolbar_style(wxFrame& frame);
#endif

CToolBar::CToolBar(CMainFrame& mainFrame, COptions& options)
	: wxToolBar(&mainFrame, nullID, wxDefaultPosition, wxDefaultSize, toolbarStyle)
	, COptionChangeEventHandler(this)
	, mainFrame_(mainFrame)
	, options_(options)
{
#ifdef __WXMAC__
	fix_toolbar_style(mainFrame_);

	// These days, OS X only knows one hardcoded toolbar size
	iconSize_ = wxSize(32, 32);
	if (wxGetApp().GetTopWindow()) {
                double scale = wxGetApp().GetTopWindow()->GetContentScaleFactor();
                iconSize_.Scale(scale, scale);
	}
#else
	iconSize_ = CThemeProvider::GetIconSize(iconSizeSmall, true);
	SetToolBitmapSize(iconSize_);
#endif

	MakeTools();

	CContextManager::Get()->RegisterHandler(this, STATECHANGE_REMOTE_IDLE, true);
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_SERVER, true);
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_SYNC_BROWSE, true);
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_COMPARISON, true);
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_APPLYFILTER, true);

	CContextManager::Get()->RegisterHandler(this, STATECHANGE_QUEUEPROCESSING, false);
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_CHANGEDCONTEXT, false);

	options_.watch(OPTION_SHOW_MESSAGELOG, this);
	options_.watch(OPTION_SHOW_QUEUE, this);
	options_.watch(OPTION_SHOW_TREE_LOCAL, this);
	options_.watch(OPTION_SHOW_TREE_REMOTE, this);
	options_.watch(OPTION_MESSAGELOG_POSITION, this);

	ToggleTool(XRCID("ID_TOOLBAR_FILTER"), CFilterManager::HasActiveFilters());
	ToggleTool(XRCID("ID_TOOLBAR_LOGVIEW"), options_.get_int(OPTION_SHOW_MESSAGELOG) != 0);
	ToggleTool(XRCID("ID_TOOLBAR_QUEUEVIEW"), options_.get_int(OPTION_SHOW_QUEUE) != 0);
	ToggleTool(XRCID("ID_TOOLBAR_LOCALTREEVIEW"), options_.get_int(OPTION_SHOW_TREE_LOCAL) != 0);
	ToggleTool(XRCID("ID_TOOLBAR_REMOTETREEVIEW"), options_.get_int(OPTION_SHOW_TREE_REMOTE) != 0);

	mainFrame_.SetToolBar(this);
	Realize();

	if (options_.get_int(OPTION_MESSAGELOG_POSITION) == 2) {
		HideTool(XRCID("ID_TOOLBAR_LOGVIEW"));
	}
}

CToolBar::~CToolBar()
{
	options_.unwatch_all(this);
	for (auto iter = m_hidden_tools.begin(); iter != m_hidden_tools.end(); ++iter) {
		delete iter->second;
	}
}

void CToolBar::MakeTool(char const* id, std::wstring const& art, wxString const& tooltip, wxString const& help, wxItemKind type)
{
	if (help.empty() && !tooltip.empty()) {
		MakeTool(id, art, tooltip, tooltip, type);
		return;
	}

	wxBitmap bmp = CThemeProvider::Get()->CreateBitmap(art, wxART_TOOLBAR, iconSize_, true);
	wxToolBar::AddTool(XRCID(id), wxString(), bmp, wxBitmap(), type, tooltip, help);
}

void CToolBar::MakeTools()
{
#ifdef __WXMSW__
	MakeTool("ID_TOOLBAR_SITEMANAGER", L"ART_SITEMANAGER", _("Open the Site Manager."), _("Open the Site Manager"), wxITEM_DROPDOWN);
#else
	MakeTool("ID_TOOLBAR_SITEMANAGER", L"ART_SITEMANAGER", _("Open the Site Manager. Right-click for a list of sites."), _("Open the Site Manager"), wxITEM_DROPDOWN);
#endif
	AddSeparator();

	MakeTool("ID_TOOLBAR_LOGVIEW", L"ART_LOGVIEW", _("Toggles the display of the message log"), wxString(), wxITEM_CHECK);
	MakeTool("ID_TOOLBAR_LOCALTREEVIEW", L"ART_LOCALTREEVIEW", _("Toggles the display of the local directory tree"), wxString(), wxITEM_CHECK);
	MakeTool("ID_TOOLBAR_REMOTETREEVIEW", L"ART_REMOTETREEVIEW", _("Toggles the display of the remote directory tree"), wxString(), wxITEM_CHECK);
	MakeTool("ID_TOOLBAR_QUEUEVIEW", L"ART_QUEUEVIEW", _("Toggles the display of the transfer queue"), wxString(), wxITEM_CHECK);
	AddSeparator();

	MakeTool("ID_TOOLBAR_REFRESH", L"ART_REFRESH", _("Refresh the file and folder lists"));
	MakeTool("ID_TOOLBAR_PROCESSQUEUE", L"ART_PROCESSQUEUE", _("Toggles processing of the transfer queue"), wxString(), wxITEM_CHECK);
	MakeTool("ID_TOOLBAR_CANCEL", L"ART_CANCEL", _("Cancels the current operation"), _("Cancel current operation"));
	MakeTool("ID_TOOLBAR_DISCONNECT", L"ART_DISCONNECT", _("Disconnects from the currently visible server"), _("Disconnect from server"));
	MakeTool("ID_TOOLBAR_RECONNECT", L"ART_RECONNECT", _("Reconnects to the last used server"));
	AddSeparator();

	MakeTool("ID_TOOLBAR_FILTER", L"ART_FILTER", _("Opens the directory listing filter dialog. Right-click to toggle filters.") + L"\n" + _("Files matching a filter rule are removed from directory listings."), _("Filter the directory listings"), wxITEM_CHECK);
	MakeTool("ID_TOOLBAR_COMPARISON", L"ART_COMPARE", _("Toggle directory comparison. Right-click to change comparison mode.\n\nColors:\nYellow: File only exists on one side\nGreen: File is newer than the unmarked file on other side\nRed: File sizes different"), _("Directory comparison"), wxITEM_CHECK);
	MakeTool("ID_TOOLBAR_SYNCHRONIZED_BROWSING", L"ART_SYNCHRONIZE", _("Toggle synchronized browsing.\nIf enabled, navigating the local directory hierarchy will also change the directory on the server accordingly and vice versa."), _("Synchronized browsing"), wxITEM_CHECK);
	MakeTool("ID_TOOLBAR_FIND", L"ART_FIND", _("Search for files recursively."), _("File search"));
}

#ifdef __WXMSW__
bool CToolBar::Realize()
{
	bool ret = wxToolBar::Realize();
	if (!ret) {
		return false;
	}

	wxASSERT(iconSize_.x > 0 && iconSize_.y > 0);
	auto toolImages = std::make_unique<wxImageList>(iconSize_.x, iconSize_.y, false, 0);
	auto disabledToolImages = std::make_unique<wxImageList>(iconSize_.x, iconSize_.y, false, 0);

	HWND hwnd = GetHandle();

	auto hImgList = reinterpret_cast<HIMAGELIST>(toolImages->GetHIMAGELIST());
	auto hDisabledImgList = reinterpret_cast<HIMAGELIST>(disabledToolImages->GetHIMAGELIST());
	::SendMessage(hwnd, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(hImgList));
	::SendMessage(hwnd, TB_SETDISABLEDIMAGELIST, 0, reinterpret_cast<LPARAM>(hDisabledImgList));

	toolImages_ = std::move(toolImages);
	disabledToolImages_ = std::move(disabledToolImages);

	for (size_t i = 0; i < GetToolsCount(); ++i) {
		auto tool = GetToolByPos(static_cast<int>(i));
		if (!tool || tool->GetStyle() != wxTOOL_STYLE_BUTTON) {
			continue;
		}

		auto bmp = tool->GetBitmap();
		if (!bmp.IsOk()) {
			continue;
		}

		int image = toolImages_->Add(bmp);
		auto disabled = tool->GetDisabledBitmap();
		if (!disabled.IsOk()) {
			disabled = wxBitmap(bmp.ConvertToImage().ConvertToGreyscale());
		}
		disabledToolImages_->Add(disabled);

		TBBUTTONINFO btn{};
		btn.cbSize = sizeof(TBBUTTONINFO);
		btn.dwMask = TBIF_BYINDEX;
		int index = ::SendMessage(hwnd, TB_GETBUTTONINFO, i, reinterpret_cast<LPARAM>(&btn));
		if (index != static_cast<int>(i)) {
			return false;
		}

		btn.dwMask = TBIF_BYINDEX | TBIF_IMAGE;
		btn.iImage = image;
		if (::SendMessage(hwnd, TB_SETBUTTONINFO, i, reinterpret_cast<LPARAM>(&btn)) == 0) {
			return false;
		}
	}

	::SendMessage(hwnd, TB_SETINDENT, ConvertDialogToPixels(wxPoint(1, 0)).x, 0);

	return true;
}

#endif

void CToolBar::OnStateChange(CState* pState, t_statechange_notifications notification, std::wstring const&, const void*)
{
	switch (notification)
	{
	case STATECHANGE_CHANGEDCONTEXT:
	case STATECHANGE_SERVER:
	case STATECHANGE_REMOTE_IDLE:
		UpdateToolbarState();
		break;
	case STATECHANGE_QUEUEPROCESSING:
		{
			const bool check = mainFrame_.GetQueue() && mainFrame_.GetQueue()->IsActive() != 0;
			ToggleTool(XRCID("ID_TOOLBAR_PROCESSQUEUE"), check);
		}
		break;
	case STATECHANGE_SYNC_BROWSE:
		{
			bool is_sync_browse = pState && pState->GetSyncBrowse();
			ToggleTool(XRCID("ID_TOOLBAR_SYNCHRONIZED_BROWSING"), is_sync_browse);
		}
		break;
	case STATECHANGE_COMPARISON:
		{
			bool is_comparing = pState && pState->GetComparisonManager()->IsComparing();
			ToggleTool(XRCID("ID_TOOLBAR_COMPARISON"), is_comparing);
		}
		break;
	case STATECHANGE_APPLYFILTER:
		ToggleTool(XRCID("ID_TOOLBAR_FILTER"), CFilterManager::HasActiveFilters());
		break;
	default:
		break;
	}
}

void CToolBar::UpdateToolbarState()
{
	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState) {
		return;
	}

	bool const hasServer = static_cast<bool>(pState->GetSite());
	bool const idle = pState->IsRemoteIdle();

	EnableTool(XRCID("ID_TOOLBAR_DISCONNECT"), hasServer && idle);
	EnableTool(XRCID("ID_TOOLBAR_CANCEL"), hasServer && !idle);
	EnableTool(XRCID("ID_TOOLBAR_SYNCHRONIZED_BROWSING"), hasServer);

	ToggleTool(XRCID("ID_TOOLBAR_COMPARISON"), pState->GetComparisonManager()->IsComparing());
	ToggleTool(XRCID("ID_TOOLBAR_SYNCHRONIZED_BROWSING"), pState->GetSyncBrowse());

	bool canReconnect;
	if (hasServer || !idle) {
		canReconnect = false;
	}
	else {
		canReconnect = static_cast<bool>(pState->GetLastSite());
	}
	EnableTool(XRCID("ID_TOOLBAR_RECONNECT"), canReconnect);
}

void CToolBar::OnOptionsChanged(watched_options const& options)
{
	if (options.test(OPTION_SHOW_MESSAGELOG)) {
		ToggleTool(XRCID("ID_TOOLBAR_LOGVIEW"), options_.get_int(OPTION_SHOW_MESSAGELOG) != 0);
	}
	if (options.test(OPTION_SHOW_QUEUE)) {
		ToggleTool(XRCID("ID_TOOLBAR_QUEUEVIEW"), options_.get_int(OPTION_SHOW_QUEUE) != 0);
	}
	if (options.test(OPTION_SHOW_TREE_LOCAL)) {
		ToggleTool(XRCID("ID_TOOLBAR_LOCALTREEVIEW"), options_.get_int(OPTION_SHOW_TREE_LOCAL) != 0);
	}
	if (options.test(OPTION_SHOW_TREE_REMOTE)) {
		ToggleTool(XRCID("ID_TOOLBAR_REMOTETREEVIEW"), options_.get_int(OPTION_SHOW_TREE_REMOTE) != 0);
	}
	if (options.test(OPTION_MESSAGELOG_POSITION)) {
		if (options_.get_int(OPTION_MESSAGELOG_POSITION) == 2) {
			HideTool(XRCID("ID_TOOLBAR_LOGVIEW"));
		}
		else {
			ShowTool(XRCID("ID_TOOLBAR_LOGVIEW"));
			ToggleTool(XRCID("ID_TOOLBAR_LOGVIEW"), options_.get_int(OPTION_SHOW_MESSAGELOG) != 0);
		}
	}
}

bool CToolBar::ShowTool(int id)
{
	int offset = 0;

	for (auto iter = m_hidden_tools.begin(); iter != m_hidden_tools.end(); ++iter) {
		if (iter->second->GetId() != id) {
			offset++;
			continue;
		}

		InsertTool(iter->first - offset, iter->second);
		Realize();
		m_hidden_tools.erase(iter);

		return true;
	}

	return false;
}

bool CToolBar::HideTool(int id)
{
	int pos = GetToolPos(id);
	if (pos == -1) {
		return false;
	}

	wxToolBarToolBase* tool = RemoveTool(id);
	if (!tool) {
		return false;
	}

	for (auto const& iter : m_hidden_tools) {
		if (iter.first > pos) {
			break;
		}

		++pos;
	}

	m_hidden_tools[pos] = tool;

	return true;
}

#if defined(__WXMSW__)
void CToolBar::DoSetToolBitmapSize(wxSize const&)
{
	// wx internally taints the sizes. Ignore the passed size and pass along out own, proper size.
	wxToolBar::DoSetToolBitmapSize(iconSize_);
}
#endif
