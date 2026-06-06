#include "filezilla.h"
#include "sitemanager_dialog.h"

#include "asksavepassworddialog.h"
#include "buildinfo.h"
#include "conditionaldialog.h"
#include "drop_target_ex.h"
#include "filezillaapp.h"
#include "inputdialog.h"
#include "loginmanager.h"
#include "Options.h"
#include "sitemanager_site.h"
#include "themeprovider.h"
#include "textctrlex.h"
#include "treectrlex.h"
#include "window_state_manager.h"
#include "wrapengine.h"
#include "xmlfunctions.h"
#include "xrc_helper.h"

#include "../commonui/fz_paths.h"
#include "../commonui/ipcmutex.h"

#include <wx/dirdlg.h>
#include <wx/dnd.h>
#include <wx/filedlg.h>
#include <wx/menu.h>
#include <wx/statline.h>
#include <wx/wupdlock.h>

#include <algorithm>
#include <array>
#include <list>

BEGIN_EVENT_TABLE(CSiteManagerDialog, wxDialogEx)
EVT_BUTTON(XRCID("wxID_OK"), CSiteManagerDialog::OnOK)
EVT_BUTTON(XRCID("wxID_CANCEL"), CSiteManagerDialog::OnCancel)
EVT_BUTTON(XRCID("ID_CONNECT"), CSiteManagerDialog::OnConnect)
EVT_BUTTON(XRCID("ID_NEWSITE"), CSiteManagerDialog::OnNewSite)
EVT_BUTTON(XRCID("ID_NEWFOLDER"), CSiteManagerDialog::OnNewFolder)
EVT_BUTTON(XRCID("ID_RENAME"), CSiteManagerDialog::OnRename)
EVT_BUTTON(XRCID("ID_DELETE"), CSiteManagerDialog::OnDelete)
EVT_TREE_BEGIN_LABEL_EDIT(XRCID("ID_SITETREE"), CSiteManagerDialog::OnBeginLabelEdit)
EVT_TREE_END_LABEL_EDIT(XRCID("ID_SITETREE"), CSiteManagerDialog::OnEndLabelEdit)
EVT_TREE_SEL_CHANGING(XRCID("ID_SITETREE"), CSiteManagerDialog::OnSelChanging)
EVT_TREE_SEL_CHANGED(XRCID("ID_SITETREE"), CSiteManagerDialog::OnSelChanged)
EVT_TREE_ITEM_ACTIVATED(XRCID("ID_SITETREE"), CSiteManagerDialog::OnItemActivated)
EVT_BUTTON(XRCID("ID_COPY"), CSiteManagerDialog::OnCopySite)
EVT_TREE_BEGIN_DRAG(XRCID("ID_SITETREE"), CSiteManagerDialog::OnBeginDrag)
EVT_CHAR(CSiteManagerDialog::OnChar)
EVT_TREE_ITEM_MENU(XRCID("ID_SITETREE"), CSiteManagerDialog::OnContextMenu)
EVT_MENU(XRCID("ID_EXPORT"), CSiteManagerDialog::OnExportSelected)
EVT_BUTTON(XRCID("ID_NEWBOOKMARK"), CSiteManagerDialog::OnNewBookmark)
EVT_BUTTON(XRCID("ID_BOOKMARK_BROWSE"), CSiteManagerDialog::OnBookmarkBrowse)
EVT_MENU(XRCID("ID_SEARCH"), CSiteManagerDialog::OnSearch)
END_EVENT_TABLE()

class CSiteManagerItemData : public wxTreeItemData
{
public:
	CSiteManagerItemData() = default;

	CSiteManagerItemData(std::unique_ptr<Site> && site)
		: m_site(std::move(site))
	{}

	// While inside tree, the contents of the path and bookmarks members are inconsistent
	std::unique_ptr<Site> m_site;

	// While inside tree, the nam member is inconsistent
	std::unique_ptr<Bookmark> m_bookmark;

	// Needed to keep track of currently connected sites so that
	// bookmarks and bookmark path can be updated in response to
	// changes done here
	int connected_item{-1};
};

class CSiteManagerDialogDataObject : public wxDataObjectSimple
{
public:
	CSiteManagerDialogDataObject()
		: wxDataObjectSimple(wxDataFormat(_T("FileZilla3SiteManagerObject")))
	{
	}

	// GTK doesn't like data size of 0
	virtual size_t GetDataSize() const { return 1; }

	virtual bool GetDataHere(void *buf) const { memset(buf, 0, 1); return true; }

	virtual bool SetData(size_t, const void *) { return true; }
};

class CSiteManagerDropTarget final : public CScrollableDropTarget<wxTreeCtrlEx>
{
public:
	CSiteManagerDropTarget(CSiteManagerDialog* pSiteManager)
		: CScrollableDropTarget<wxTreeCtrlEx>(XRCCTRL(*pSiteManager, "ID_SITETREE", wxTreeCtrlEx))
	{
		SetDataObject(new CSiteManagerDialogDataObject());
		m_pSiteManager = pSiteManager;
	}

	virtual wxTreeItemId DisplayDropHighlight(wxPoint const& p) override
	{
		return m_pSiteManager->tree_->DisplayDropHighlight(p);
	}

	bool IsValidDropLocation(wxTreeItemId const& hit, wxDragResult const& def)
	{
		if (!hit) {
			return false;
		}

		const bool predefined = m_pSiteManager->IsPredefinedItem(hit);
		if (predefined) {
			return false;
		}

		auto * tree = m_pSiteManager->tree_;

		CSiteManagerItemData *pData = (CSiteManagerItemData *)tree->GetItemData(hit);
		// Cannot drop on bookmark
		if (pData && !pData->m_site) {
			return false;
		}

		for (auto const& item : m_pSiteManager->draggedItems_) {
			if (hit == item) {
				return false;
			}

			CSiteManagerItemData* pSourceData = (CSiteManagerItemData*)tree->GetItemData(item);
			if (pData) {
				// If dropping on a site, source needs to be a bookmark
				if (!pSourceData || pSourceData->m_site) {
					return false;
				}
			}
			// Target is a directory, so source must not be a bookmark
			else if (pSourceData && !pSourceData->m_site) {
				return false;
			}

			// Disallow dragging into own child
			wxTreeItemId cur = hit;
			while (cur && cur != tree->GetRootItem()) {
				if (cur == item) {
					tree->ClearDropHighlight();
					return wxDragNone;
				}
				cur = tree->GetItemParent(cur);
			}

			// If moving, disallow moving to direct parent
			if (def == wxDragMove && tree->GetItemParent(item) == hit) {
				return false;
			}
		}

		return true;
	}

	virtual wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def)
	{
		auto tree = m_pSiteManager->tree_;
		tree->ClearDropHighlight();
		if (def == wxDragError ||
			def == wxDragNone ||
			def == wxDragCancel)
		{
			return def;
		}

		wxTreeItemId hit = tree->GetHit(wxPoint(x, y));
		if (!IsValidDropLocation(hit, def)) {
			return wxDragNone;
		}

		m_pSiteManager->m_is_deleting = true;
		for (auto const& item : m_pSiteManager->draggedItems_) {
			if (!m_pSiteManager->MoveItems(item, hit, def == wxDragCopy, true)) {
				def = wxDragNone;
				break;
			}
		}
		m_pSiteManager->m_is_deleting = false;
		m_pSiteManager->SetCtrlState();

		return def;
	}

	virtual bool OnDrop(wxCoord x, wxCoord y)
	{
		CScrollableDropTarget<wxTreeCtrlEx>::OnDrop(x, y);

		auto tree = m_pSiteManager->tree_;
		tree->ClearDropHighlight();

		wxTreeItemId hit = tree->GetHit(wxPoint(x, y));
		if (!IsValidDropLocation(hit, wxDragCopy)) {
			return wxDragNone;
		}

		return true;
	}

	virtual void OnLeave()
	{
		CScrollableDropTarget<wxTreeCtrlEx>::OnLeave();
		m_pSiteManager->tree_->ClearDropHighlight();
	}

	virtual wxDragResult OnEnter(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = CScrollableDropTarget<wxTreeCtrlEx>::OnEnter(x, y, def);
		return OnDragOver(x, y, def);
	}

	virtual wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = CScrollableDropTarget<wxTreeCtrlEx>::OnDragOver(x, y, def);

		auto tree = m_pSiteManager->tree_;
		if (def == wxDragError ||
			def == wxDragNone ||
			def == wxDragCancel)
		{
			tree->ClearDropHighlight();
			return def;
		}

		wxTreeItemId hit = tree->GetHit(wxPoint(x, y));
		if (!IsValidDropLocation(hit, def)) {
			m_pSiteManager->tree_->ClearDropHighlight();
		}
		else {
			tree->DisplayDropHighlight(hit);
		}

		return def;
	}

protected:
	CSiteManagerDialog* m_pSiteManager;
};

CSiteManagerDialog::CSiteManagerDialog(COptionsBase & options, login_manager & lim)
	: options_(options)
	, login_manager_(lim)
{
}

CSiteManagerDialog::~CSiteManagerDialog()
{
	delete m_pSiteManagerMutex;

	if (m_pWindowStateManager) {
		m_pWindowStateManager->Remember(OPTION_SITEMANAGER_POSITION);
		delete m_pWindowStateManager;
	}
}

wxPanel * CreateBookmarkPanel(wxWindow* parent, DialogLayout const& lay)
{
	wxPanel* panel = new wxPanel(parent);

	auto* main = lay.createMain(panel, 1);
	main->AddGrowableCol(0);

	main->Add(new wxStaticText(panel, -1, _("&Local directory:")));

	auto row = lay.createFlex(0, 1);
	main->Add(row, lay.grow);
	row->AddGrowableCol(0);
	row->Add(new wxTextCtrlEx(panel, XRCID("ID_BOOKMARK_LOCALDIR")), lay.valigng);
	row->Add(new wxButton(panel, XRCID("ID_BOOKMARK_BROWSE"), _("&Browse...")), lay.valign);
	main->AddSpacer(0);
	main->Add(new wxStaticText(panel, -1, _("&Remote directory:")));
	main->Add(new wxTextCtrlEx(panel, XRCID("ID_BOOKMARK_REMOTEDIR")), lay.grow);
	main->AddSpacer(0);
	main->Add(new wxCheckBox(panel, XRCID("ID_BOOKMARK_SYNC"), _("Use &synchronized browsing")));
	main->Add(new wxCheckBox(panel, XRCID("ID_BOOKMARK_COMPARISON"), _("Directory comparison")));

	return panel;
}

bool CSiteManagerDialog::Create(wxWindow* parent, std::vector<_connected_site>* connected_sites, Site const* site)
{
	m_pSiteManagerMutex = new CInterProcessMutex(MUTEX_SITEMANAGERGLOBAL, false);
	if (m_pSiteManagerMutex->TryLock() == 0) {
		int answer = wxMessageBoxEx(_("The Site Manager is opened in another instance of FileZilla 3.\nDo you want to continue? Any changes made in the Site Manager won't be saved then."),
			_("Site Manager already open"), wxYES_NO | wxICON_QUESTION);
		if (answer != wxYES) {
			return false;
		}

		delete m_pSiteManagerMutex;
		m_pSiteManagerMutex = 0;
	}

	SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
	if (!wxDialogEx::Create(parent, -1, _("Site Manager"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxSYSTEM_MENU | wxRESIZE_BORDER | wxCLOSE_BOX)) {
		return false;
	}

	auto const& lay = layout();

	auto main = lay.createMain(this, 1);
	main->AddGrowableCol(0);
	main->AddGrowableRow(0);

	auto sides = new wxBoxSizer(wxHORIZONTAL);
	main->Add(sides, lay.grow)->SetProportion(1);

	auto left = lay.createFlex(1);
	left->AddGrowableCol(0);
	left->AddGrowableRow(1);
	sides->Add(left, lay.grow)->SetProportion(1);

	left->Add(new wxStaticText(this, wxID_ANY, _("&Select entry:")));

	tree_ = new wxTreeCtrlEx(this, XRCID("ID_SITETREE"), wxDefaultPosition, wxDefaultSize, DEFAULT_TREE_STYLE | wxBORDER_SUNKEN | wxTR_EDIT_LABELS | wxTR_MULTIPLE);
	tree_->SetFocus();
	left->Add(tree_, lay.grow)->SetProportion(1);

	auto entrybuttons = new wxGridSizer(2, wxSize(lay.gap, lay.gap));
	left->Add(entrybuttons, lay.halign);

	entrybuttons->Add(new wxButton(this, XRCID("ID_NEWSITE"), _("&New site")), lay.grow);
	entrybuttons->Add(new wxButton(this, XRCID("ID_NEWFOLDER"), _("New &folder")), lay.grow);
	entrybuttons->Add(new wxButton(this, XRCID("ID_NEWBOOKMARK"), _("New Book&mark")), lay.grow);
	entrybuttons->Add(new wxButton(this, XRCID("ID_RENAME"), _("&Rename")), lay.grow);
	entrybuttons->Add(new wxButton(this, XRCID("ID_DELETE"), _("&Delete")), lay.grow);
	entrybuttons->Add(new wxButton(this, XRCID("ID_COPY"), _("Dupl&icate")), lay.grow);

	main->Add(new wxStaticLine(this), lay.grow);

	auto buttons = new wxGridSizer(1, 0, wxSize(lay.gap, lay.gap));
	main->Add(buttons, 0, wxALIGN_RIGHT);

	auto connect = new wxButton(this, XRCID("ID_CONNECT"), _("&Connect"));
	connect->SetDefault();
	buttons->Add(connect);

	auto ok = new wxButton(this, wxID_OK, _("&OK"));
	buttons->Add(ok);

	auto cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
	buttons->Add(cancel);

	// Now create the imagelist for the site tree
	wxSize s = CThemeProvider::GetIconSize(iconSizeSmall);
	wxImageList* pImageList = new wxImageList(s.x, s.y);

	pImageList->Add(CThemeProvider::Get()->CreateBitmap(_T("ART_FOLDERCLOSED"), wxART_OTHER, s, true));
	pImageList->Add(CThemeProvider::Get()->CreateBitmap(_T("ART_FOLDER"), wxART_OTHER, s, true));
	pImageList->Add(CThemeProvider::Get()->CreateBitmap(_T("ART_SERVER"), wxART_OTHER, s, true));
	pImageList->Add(CThemeProvider::Get()->CreateBitmap(_T("ART_BOOKMARK"), wxART_OTHER, s, true));

	tree_->AssignImageList(pImageList);

	auto right = new wxBoxSizer(wxVERTICAL);
	sides->Add(right, 1, wxLEFT|wxGROW, lay.gap);

	m_pNotebook_Site = new CSiteManagerSite(options_);
	if (!m_pNotebook_Site->Load(this)) {
		return false;
	}

	right->Add(m_pNotebook_Site, 1, wxGROW);

	Layout();

	wxSize minSize = GetSizer()->GetMinSize();

	wxSize size = GetSize();
	wxSize clientSize = GetClientSize();
	SetMinSize(GetSizer()->GetMinSize() + size - clientSize);
	SetClientSize(minSize);

	{
		// Load bookmark notebook
		m_pNotebook_Bookmark = new wxNotebook(this, nullID);

		auto * bookmarkPanel = CreateBookmarkPanel(m_pNotebook_Bookmark, lay);

		m_pNotebook_Bookmark->AddPage(bookmarkPanel, _("Bookmark"));
		m_pNotebook_Bookmark->Hide();
		right->Add(m_pNotebook_Bookmark, 2, wxGROW);
		right->SetItemMinSize(1, right->GetItem((size_t)0)->GetMinSize().GetWidth(), -1);
	}

	// Set min size of tree to actual size of tree.
	// Otherwise some platforms automatically calculate a min size fitting all items,
	// resulting in a huge dialog if there are many sites.
	wxSize const treeSize = tree_->GetSize();
	if (treeSize.IsFullySpecified()) {
		tree_->SetMinSize(treeSize);
	}

	if (!Load()) {
		return false;
	}

	wxTreeItemId item = tree_->GetSelection();
	if (!item.IsOk()) {
		tree_->SafeSelectItem(m_ownSites);
	}
	SetCtrlState();

	m_pWindowStateManager = new CWindowStateManager(this, options_);
	m_pWindowStateManager->Restore(OPTION_SITEMANAGER_POSITION);

	tree_->SetDropTarget(new CSiteManagerDropTarget(this));

#ifdef __WXGTK__
	{
		CSiteManagerItemData* data = 0;
		wxTreeItemId selected = tree_->GetSelection();
		if (selected.IsOk()) {
			data = static_cast<CSiteManagerItemData* >(tree_->GetItemData(selected));
		}
		if (!data) {
			XRCCTRL(*this, "wxID_OK", wxButton)->SetFocus();
		}
	}
#endif

	acceleratorTable_.emplace_back(0, WXK_F3, XRCID("ID_SEARCH"));

	m_connected_sites = connected_sites;
	MarkConnectedSites();

	if (site && *site) {
		CopyAddServer(*site);
	}

	return true;
}

void CSiteManagerDialog::MarkConnectedSites()
{
	for (int i = 0; i < (int)m_connected_sites->size(); ++i) {
		MarkConnectedSite(i);
	}
}

void CSiteManagerDialog::MarkConnectedSite(int connected_site)
{
	auto & site = (*m_connected_sites)[connected_site];
	std::wstring const& connected_site_path = site.old_path;
	if (connected_site_path.empty()) {
		return;
	}

	if (connected_site_path[0] == '1') {
		// Default sites never change
		return;
	}

	if (connected_site_path[0] != '0') {
		// Unknown type, from a future version perhaps?
		return;
	}

	std::vector<std::wstring> segments;
	if (!CSiteManager::UnescapeSitePath(connected_site_path.substr(1), segments)) {
		return;
	}

	wxTreeItemId current = m_ownSites;
	for (auto const& segment : segments) {
		wxTreeItemIdValue c;
		wxTreeItemId child = tree_->GetFirstChild(current, c);
		while (child) {
			if (tree_->GetItemText(child) == segment) {
				break;
			}

			child = tree_->GetNextChild(current, c);
		}
		if (!child) {
			return;
		}

		current = child;
	}

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(tree_->GetItemData(current));
	if (!data || !data->m_site) {
		return;
	}

	wxASSERT(data->connected_item == -1);
	data->connected_item = connected_site;
}

void CSiteManagerDialog::OnOK(wxCommandEvent&)
{
	if (!Verify()) {
		return;
	}

	UpdateItem();

	if (!CAskSavePasswordDialog::Run(this, options_)) {
		return;
	}

	Save();

	RememberLastSelected();

	EndModal(wxID_OK);
}

void CSiteManagerDialog::OnCancel(wxCommandEvent&)
{
	EndModal(wxID_CANCEL);
}

void CSiteManagerDialog::OnConnect(wxCommandEvent&)
{
	wxTreeItemId item = tree_->GetSelection();
	if (!item.IsOk()) {
		return;
	}

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(tree_->GetItemData(item));
	if (!data) {
		wxBell();
		return;
	}

	if (!Verify()) {
		wxBell();
		return;
	}

	UpdateItem();

	if (!CAskSavePasswordDialog::Run(this, options_)) {
		return;
	}

	Save();

	RememberLastSelected();

	EndModal(wxID_YES);
}

class CSiteManagerXmlHandler_Tree : public CSiteManagerXmlHandler
{
public:
	CSiteManagerXmlHandler_Tree(wxTreeCtrlEx* tree_, wxTreeItemId root, std::wstring const& lastSelection, bool predefined, int kiosk)
		: m_tree_(tree_), m_item(root), m_predefined(predefined), m_kiosk(kiosk)
	{
		if (!CSiteManager::UnescapeSitePath(lastSelection, m_lastSelection)) {
			m_lastSelection.clear();
		}
		m_lastSelectionIt = m_lastSelection.cbegin();
	}

	virtual ~CSiteManagerXmlHandler_Tree()
	{
		m_tree_->SortChildren(m_item);
		m_tree_->Expand(m_item);
	}

	virtual bool AddFolder(std::wstring const& name, bool expanded)
	{
		wxTreeItemId newItem = m_tree_->AppendItem(m_item, name, 0, 0);
		m_tree_->SetItemImage(newItem, 1, wxTreeItemIcon_Expanded);
		m_tree_->SetItemImage(newItem, 1, wxTreeItemIcon_SelectedExpanded);

		m_item = newItem;
		m_expand.push_back(expanded);

		if (!m_wrong_sel_depth && m_lastSelectionIt != m_lastSelection.cend()) {
			if (*m_lastSelectionIt == name) {
				++m_lastSelectionIt;
				if (m_lastSelectionIt == m_lastSelection.cend()) {
					m_tree_->SafeSelectItem(newItem);
				}
			}
			else {
				++m_wrong_sel_depth;
			}
		}
		else {
			++m_wrong_sel_depth;
		}

		return true;
	}

	virtual bool AddSite(std::unique_ptr<Site> data)
	{
		if (m_kiosk && !m_predefined &&
			data->credentials.logonType_ == LogonType::normal)
		{
			// Clear saved password
			data->SetLogonType(LogonType::ask);
			data->credentials.SetPass(std::wstring());
			data->credentials.encrypted_ = fz::public_key();
		}

		std::wstring const name = data->GetName();

		CSiteManagerItemData* pData = new CSiteManagerItemData(std::move(data));
		wxTreeItemId newItem = m_tree_->AppendItem(m_item, name, 2, 2, pData);

		bool can_select = false;
		if (!m_wrong_sel_depth && m_lastSelectionIt != m_lastSelection.cend()) {
			if (*m_lastSelectionIt == name) {
				++m_lastSelectionIt;
				can_select = true;
				if (m_lastSelectionIt == m_lastSelection.cend()) {
					m_tree_->SafeSelectItem(newItem);
				}
			}
		}

		for (auto const& bookmark : pData->m_site->m_bookmarks) {
			AddBookmark(newItem, bookmark, can_select);
		}

		m_tree_->SortChildren(newItem);
		m_tree_->Expand(newItem);

		return true;
	}

	bool AddBookmark(wxTreeItemId const& parent, Bookmark const& bookmark, bool can_select)
	{
		CSiteManagerItemData* pData = new CSiteManagerItemData;
		pData->m_bookmark = std::make_unique<Bookmark>(bookmark);
		wxTreeItemId newItem = m_tree_->AppendItem(parent, bookmark.m_name, 3, 3, pData);

		if (can_select && m_lastSelectionIt != m_lastSelection.cend()) {
			if (*m_lastSelectionIt == bookmark.m_name) {
				++m_lastSelectionIt;
				if (m_lastSelectionIt == m_lastSelection.cend()) {
					m_tree_->SafeSelectItem(newItem);
				}
			}
		}

		return true;
	}

	virtual bool LevelUp()
	{
		if (m_wrong_sel_depth) {
			m_wrong_sel_depth--;
		}

		if (!m_expand.empty()) {
			const bool expand = m_expand.back();
			m_expand.pop_back();
			if (expand) {
				m_tree_->Expand(m_item);
			}
		}
		m_tree_->SortChildren(m_item);

		wxTreeItemId parent = m_tree_->GetItemParent(m_item);
		if (!parent) {
			return false;
		}

		m_item = parent;
		return true;
	}

protected:
	wxTreeCtrlEx * const m_tree_;
	wxTreeItemId m_item;

	std::vector<std::wstring> m_lastSelection;
	std::vector<std::wstring>::const_iterator m_lastSelectionIt;
	int m_wrong_sel_depth{};

	std::vector<bool> m_expand;

	bool const m_predefined{};
	int m_kiosk{};
};

bool CSiteManagerDialog::Load()
{
	tree_->DeleteAllItems();

	// We have to synchronize access to sitemanager.xml so that multiple processed don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	// Load default sites
	bool hasDefaultSites = LoadDefaultSites();
	if (hasDefaultSites) {
		m_ownSites = tree_->AppendItem(tree_->GetRootItem(), _("My Sites"), 0, 0);
	}
	else {
		m_ownSites = tree_->AddRoot(_("My Sites"), 0, 0);
	}

	wxTreeItemId treeId = m_ownSites;
	tree_->SetItemImage(treeId, 1, wxTreeItemIcon_Expanded);
	tree_->SetItemImage(treeId, 1, wxTreeItemIcon_SelectedExpanded);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("sitemanager")));
	auto document = file.Load();
	if (!document) {
		wxString msg = file.GetError() + _T("\n") + _("The Site Manager cannot be used unless the file gets repaired.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	if (file.IsFromFutureVersion()) {
		wxString msg = wxString::Format(_("The file '%s' has been created by a more recent version of FileZilla.\nLoading files created by newer versions can result in loss of data.\nDo you want to continue?"), file.GetFileName());
		if (wxMessageBoxEx(msg, _("Detected newer version of FileZilla"), wxICON_QUESTION | wxYES_NO) != wxYES) {
			return false;
		}
	}

	auto element = document.child("Servers");
	if (!element) {
		return true;
	}

	std::wstring lastSelection = options_.get_string(OPTION_SITEMANAGER_LASTSELECTED);
	if (!lastSelection.empty() && lastSelection[0] == '0') {
		if (lastSelection == _T("0")) {
			tree_->SafeSelectItem(treeId);
		}
		else {
			lastSelection = lastSelection.substr(1);
		}
	}
	else {
		lastSelection.clear();
	}
	CSiteManagerXmlHandler_Tree handler(tree_, treeId, lastSelection, false, options_.get_int(OPTION_DEFAULT_KIOSKMODE));

	bool res = CSiteManager::Load(element, handler, file.GetVersion());

	tree_->SortChildren(treeId);
	tree_->Expand(treeId);
	if (!tree_->GetSelection()) {
		tree_->SafeSelectItem(treeId);
	}

	tree_->EnsureVisible(tree_->GetSelection());

	return res;
}

bool CSiteManagerDialog::Save(pugi::xml_node element, wxTreeItemId treeId)
{
	if (!m_pSiteManagerMutex) {
		return false;
	}

	if (!element || !treeId) {
		// We have to synchronize access to sitemanager.xml so that multiple processed don't write
		// to the same file or one is reading while the other one writes.
		CInterProcessMutex mutex(MUTEX_SITEMANAGER);

		CXmlFile xml(wxGetApp().GetSettingsFile(_T("sitemanager")));

		auto document = xml.Load();
		if (!document) {
			wxString msg = xml.GetError() + _T("\n") + _("Any changes made in the Site Manager could not be saved.");
			wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

			return false;
		}

		auto servers = document.child("Servers");
		while (servers) {
			document.remove_child(servers);
			servers = document.child("Servers");
		}
		element = document.append_child("Servers");

		if (!element) {
			return true;
		}

		bool res = Save(element, m_ownSites);

		if (!xml.Save()) {
			if (options_.get_int(OPTION_DEFAULT_KIOSKMODE) == 2) {
				return res;
			}
			wxString msg = wxString::Format(_("Could not write \"%s\", any changes to the Site Manager could not be saved: %s"), xml.GetFileName(), xml.GetError());
			wxMessageBoxEx(msg, _("Error writing xml file"), wxICON_ERROR);
		}

		return res;
	}

	wxTreeItemId child;
	wxTreeItemIdValue cookie;
	child = tree_->GetFirstChild(treeId, cookie);
	while (child.IsOk()) {
		SaveChild(element, child);

		child = tree_->GetNextChild(treeId, cookie);
	}

	return false;
}

bool CSiteManagerDialog::SaveChild(pugi::xml_node element, wxTreeItemId child)
{
	std::wstring const name = tree_->GetItemText(child).ToStdWstring();

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(tree_->GetItemData(child));
	if (!data) {
		auto node = element.append_child("Folder");
		const bool expanded = tree_->IsExpanded(child);
		SetTextAttribute(node, "expanded", expanded ? _T("1") : _T("0"));
		AddTextElement(node, name);
		Save(node, child);
	}
	else if (data->m_site) {
		auto node = element.append_child("Server");

		// Update bookmarks
		data->m_site->m_bookmarks.clear();

		wxTreeItemIdValue cookie;
		wxTreeItemId bookmarkChild = tree_->GetFirstChild(child, cookie);
		while (bookmarkChild.IsOk()) {
			CSiteManagerItemData* bookmarkData = static_cast<CSiteManagerItemData* >(tree_->GetItemData(bookmarkChild));
			wxASSERT(bookmarkData->m_bookmark);
			bookmarkData->m_bookmark->m_name = tree_->GetItemText(bookmarkChild).ToStdWstring();
			data->m_site->m_bookmarks.push_back(*bookmarkData->m_bookmark);
			bookmarkChild = tree_->GetNextChild(child, cookie);
		}

		if (data->m_site->server.GetProtocol() == ONEDRIVE) {
			CSiteManager::UpdateOneDrivePath(data->m_site->m_default_bookmark.m_remoteDir);
		}
		else if (data->m_site->server.GetProtocol() == GOOGLE_DRIVE) {
			CSiteManager::UpdateGoogleDrivePath(data->m_site->m_default_bookmark.m_remoteDir);
		}

		CSiteManager::Save(node, *data->m_site, login_manager_, options_);

		if (data->connected_item != -1) {
			(*m_connected_sites)[data->connected_item].site = *data->m_site;
			(*m_connected_sites)[data->connected_item].site.SetSitePath(GetSitePath(child));
		}
	}

	return true;
}

void CSiteManagerDialog::OnNewFolder(wxCommandEvent&)
{
	auto const selections = tree_->GetAllSelections();
	if (selections.empty()) {
		return;
	}

	wxTreeItemId item = selections.front();
	if (!item.IsOk()) {
		return;
	}

	while (tree_->GetItemData(item)) {
		item = tree_->GetItemParent(item);
	}

	if (!Verify()) {
		return;
	}
	UpdateItem();

	wxString name = FindFirstFreeName(item, _("New folder"));

	wxTreeItemId newItem = tree_->AppendItem(item, name, 0, 0);
	tree_->SetItemImage(newItem, 1, wxTreeItemIcon_Expanded);
	tree_->SetItemImage(newItem, 1, wxTreeItemIcon_SelectedExpanded);
	tree_->SortChildren(item);
	tree_->EnsureVisible(newItem);
	tree_->SafeSelectItem(newItem);
	tree_->EditLabel(newItem);
}

bool CSiteManagerDialog::Verify()
{
	wxTreeItemId item = tree_->GetSelection();
	if (!item.IsOk()) {
		return true;
	}

	CSiteManagerItemData* data = (CSiteManagerItemData *)tree_->GetItemData(item);
	if (!data) {
		return true;
	}

	if (data->m_site) {
		Site newSite = *data->m_site;
		if (m_pNotebook_Site && !m_pNotebook_Site->UpdateSite(newSite, false)) {
			return false;
		}
	}
	else {
		wxTreeItemId parent = tree_->GetItemParent(item);
		if (!parent) {
			return false;
		}
		CSiteManagerItemData* pServer = static_cast<CSiteManagerItemData*>(tree_->GetItemData(parent));
		if (!pServer || !pServer->m_site) {
			return false;
		}

		const wxString remotePathRaw = XRCCTRL(*this, "ID_BOOKMARK_REMOTEDIR", wxTextCtrl)->GetValue();
		if (!remotePathRaw.empty()) {
			CServerPath remotePath;
			remotePath.SetType(pServer->m_site->server.GetType());
			if (!remotePath.SetPath(remotePathRaw.ToStdWstring())) {
				XRCCTRL(*this, "ID_BOOKMARK_REMOTEDIR", wxTextCtrl)->SetFocus();
				wxString msg;
				if (pServer->m_site->server.GetType() != UNIX) {
					msg = wxString::Format(_("Remote path cannot be parsed. Make sure it is a valid absolute path and is supported by the servertype (%s) selected on the parent site."), CServer::GetNameFromServerType(pServer->m_site->server.GetType()));
				}
				else {
					msg = _("Remote path cannot be parsed. Make sure it is a valid absolute path.");
				}
				wxMessageBoxEx(msg, _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
				return false;
			}
		}

		const wxString localPath = XRCCTRL(*this, "ID_BOOKMARK_LOCALDIR", wxTextCtrl)->GetValue();

		if (remotePathRaw.empty() && localPath.empty()) {
			XRCCTRL(*this, "ID_BOOKMARK_LOCALDIR", wxTextCtrl)->SetFocus();
			wxMessageBoxEx(_("You need to enter at least one path, empty bookmarks are not supported."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
			return false;
		}

		if (XRCCTRL(*this, "ID_BOOKMARK_SYNC", wxCheckBox)->GetValue()) {
			if (remotePathRaw.empty() || localPath.empty()) {
				XRCCTRL(*this, "ID_BOOKMARK_SYNC", wxCheckBox)->SetFocus();
				wxMessageBoxEx(_("You need to enter both a local and a remote path to enable synchronized browsing for this bookmark."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
				return false;
			}
		}
	}

	return true;
}

void CSiteManagerDialog::OnBeginLabelEdit(wxTreeEvent& event)
{
	if (event.GetItem() != tree_->GetSelection()) {
		if (!Verify()) {
			event.Veto();
			return;
		}
	}

	wxTreeItemId item = event.GetItem();
	if (!item.IsOk() || item == tree_->GetRootItem() || item == m_ownSites || IsPredefinedItem(item)) {
		event.Veto();
		return;
	}
}

void CSiteManagerDialog::OnEndLabelEdit(wxTreeEvent& event)
{
	lastEditVetoed_ = false;
	if (event.IsEditCancelled()) {
		return;
	}

	wxTreeItemId item = event.GetItem();
	if (item != tree_->GetSelection()) {
		if (!Verify()) {
			lastEditVetoed_ = true;
			event.Veto();
			return;
		}
	}

	if (!item.IsOk() || item == tree_->GetRootItem() || item == m_ownSites || IsPredefinedItem(item)) {
		lastEditVetoed_ = true;
		event.Veto();
		return;
	}

	wxString name = event.GetLabel();
	name = name.substr(0, 255);
	if (name.empty()) {
		event.Veto();
		return;
	}

	wxTreeItemId parent = tree_->GetItemParent(item);

	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = tree_->GetFirstChild(parent, cookie); child.IsOk(); child = tree_->GetNextChild(parent, cookie)) {
		if (child == item) {
			continue;
		}
		if (!name.CmpNoCase(tree_->GetItemText(child))) {
			lastEditVetoed_ = true;
			wxMessageBoxEx(_("Name already exists"), _("Cannot rename entry"), wxICON_EXCLAMATION, this);
			event.Veto();
			return;
		}
	}

	// Always veto and manually change name so that the sorting works
	event.Veto();
	tree_->SetItemText(item, name);
	tree_->SortChildren(parent);
}

void CSiteManagerDialog::OnRename(wxCommandEvent&)
{
	wxTreeItemId item = tree_->GetSelection();
	if (!item.IsOk() || item == tree_->GetRootItem() || item == m_ownSites || IsPredefinedItem(item)) {
		return;
	}

	tree_->EditLabel(item);
}

void CSiteManagerDialog::OnDelete(wxCommandEvent&)
{
	auto selections = tree_->GetAllSelections();
	if (selections.empty()) {
		return;
	}

	std::vector<wxTreeItemId> items;
	wxTreeItemId ancestor;
	for (auto const& item : selections) {
		if (!item.IsOk() || item == tree_->GetRootItem() || item == m_ownSites || IsPredefinedItem(item)) {
			return;
		}

		// Only keep items that do not have an ancestor that is already being deleted
		auto parent = tree_->GetItemParent(item);
		while (parent && parent != ancestor) {
			parent = tree_->GetItemParent(parent);
		}
		if (!parent) {
			ancestor = item;
			items.push_back(item);
		}
	}
	if (items.empty()) {
		return;
	}

	CConditionalDialog dlg(this, CConditionalDialog::sitemanager_confirmdelete, CConditionalDialog::yesno, options_);
	dlg.SetTitle(_("Delete Site Manager entries"));

	dlg.AddText(_("Do you really want to delete the selected entries?"));

	if (!dlg.Run()) {
		return;
	}

	wxTreeItemId to_select = tree_->GetItemParent(items.front());

	m_is_deleting = true;

	for (auto const& item : items) {
		wxTreeItemId parent = tree_->GetItemParent(item);
		if (tree_->GetChildrenCount(parent) == 1) {
			tree_->Collapse(parent);
		}
#ifdef __WXMSW__
		// Delete tries to move selection to next visible, collapse so that it's never a child.
		tree_->Collapse(item);
#endif
		tree_->Delete(item);
	}

	tree_->SafeSelectItem(to_select);

	m_is_deleting = false;

	SetCtrlState();
}

void CSiteManagerDialog::OnSelChanging(wxTreeEvent& event)
{
	if (m_is_deleting || tree_->ShouldIgnoreChangeEvent()) {
		return;
	}

	if (tree_->GetEditControl()) {
#ifdef __WXMSW__
		bool ok = TreeView_EndEditLabelNow(tree_->GetHWND(), false);
		if (!ok) {
			event.Veto();
			return;
		}
#else
		tree_->EndEditLabel(wxTreeItemId(), false);
		if (lastEditVetoed_) {
			event.Veto();
			return;
		}
#endif
	}

	if (!Verify()) {
		event.Veto();
	}

	UpdateItem();
}

void CSiteManagerDialog::OnSelChanged(wxTreeEvent& evt)
{
	if (m_is_deleting || tree_->ShouldIgnoreChangeEvent()) {
		return;
	}

	if (tree_->InPrefixSearch()) {
		m_is_deleting = true;
		tree_->SafeSelectItem(evt.GetItem());
		m_is_deleting = false;
	}

#if !defined(__WXMSW__)
	auto focused = tree_->GetFocusedItem();
	if (focused) {
		tree_->EnsureVisible(focused);
	}
#endif

	wxWindowUpdateLocker l(this);
	SetCtrlState();
}

void CSiteManagerDialog::OnNewSite(wxCommandEvent&)
{
	auto const selections = tree_->GetAllSelections();
	if (selections.empty()) {
		return;
	}

	wxTreeItemId item = selections.front();
	if (!item.IsOk() || IsPredefinedItem(item)) {
		return;
	}

	while (tree_->GetItemData(item)) {
		item = tree_->GetItemParent(item);
	}

	if (!Verify()) {
		return;
	}
	UpdateItem();

	Site site;
	site.server.SetProtocol(ServerProtocol::FTP);
	AddNewSite(item, site);
}

bool CSiteManagerDialog::UpdateItem()
{
	wxTreeItemId item = tree_->GetSelection();
	if (!item.IsOk()) {
		return false;
	}

	if (IsPredefinedItem(item)) {
		return true;
	}

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(tree_->GetItemData(item));
	if (!data) {
		return false;
	}

	if (data->m_site) {
		return UpdateServer(*data->m_site, tree_->GetItemText(item));
	}
	else {
		wxASSERT(data->m_bookmark);
		wxTreeItemId parent = tree_->GetItemParent(item);
		CSiteManagerItemData const* pServer = static_cast<CSiteManagerItemData*>(tree_->GetItemData(parent));
		if (!pServer || !pServer->m_site) {
			return false;
		}
		data->m_bookmark->m_name = tree_->GetItemText(item).ToStdWstring();
		return UpdateBookmark(*data->m_bookmark, *pServer->m_site);
	}
}

bool CSiteManagerDialog::UpdateBookmark(Bookmark &bookmark, Site const& site)
{
	bookmark.m_localDir = xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxTextCtrl::GetValue).ToStdWstring();
	bookmark.m_remoteDir = CServerPath();
	bookmark.m_remoteDir.SetType(site.server.GetType());
	bookmark.m_remoteDir.SetPath(xrc_call(*this, "ID_BOOKMARK_REMOTEDIR", &wxTextCtrl::GetValue).ToStdWstring());
	bookmark.m_sync = xrc_call(*this, "ID_BOOKMARK_SYNC", &wxCheckBox::GetValue);
	bookmark.m_comparison = xrc_call(*this, "ID_BOOKMARK_COMPARISON", &wxCheckBox::GetValue);

	return true;
}

bool CSiteManagerDialog::UpdateServer(Site & site, const wxString &name)
{
	Site newSite = site;
	newSite.SetName(name.ToStdWstring());
	if (m_pNotebook_Site && !m_pNotebook_Site->UpdateSite(newSite, true)) {
		return false;
	}

	site = newSite;
	return true;
}

bool CSiteManagerDialog::GetServer(Site& data, Bookmark& bookmark)
{
	wxTreeItemId item = tree_->GetSelection();
	if (!item.IsOk()) {
		return false;
	}

	CSiteManagerItemData* pData = static_cast<CSiteManagerItemData* >(tree_->GetItemData(item));
	if (!pData) {
		return false;
	}

	if (pData->m_bookmark) {
		item = tree_->GetItemParent(item);
		CSiteManagerItemData* pSiteData = static_cast<CSiteManagerItemData*>(tree_->GetItemData(item));

		data = *pSiteData->m_site;
		bookmark = data.m_default_bookmark;

		if (!pData->m_bookmark->m_localDir.empty()) {
			bookmark.m_localDir = pData->m_bookmark->m_localDir;
		}

		if (!pData->m_bookmark->m_remoteDir.empty()) {
			bookmark.m_remoteDir = pData->m_bookmark->m_remoteDir;
		}

		if (bookmark.m_localDir.empty() || bookmark.m_remoteDir.empty()) {
			bookmark.m_sync = false;
			bookmark.m_comparison = false;
		}
		else {
			bookmark.m_sync = pData->m_bookmark->m_sync;
			bookmark.m_comparison = pData->m_bookmark->m_comparison;
		}
	}
	else {
		data = *pData->m_site;
		bookmark = data.m_default_bookmark;
	}

	data.SetSitePath(GetSitePath(item));

	return true;
}

void CSiteManagerDialog::OnItemActivated(wxTreeEvent&)
{
	wxTreeItemId item = tree_->GetSelection();
	if (!item.IsOk()) {
		return;
	}

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(tree_->GetItemData(item));
	if (!data) {
		if (item != tree_->GetRootItem() || !tree_->IsExpanded(item)) {
			tree_->Toggle(item);
		}
		return;
	}

	wxCommandEvent cmdEvent;
	OnConnect(cmdEvent);
}

void CSiteManagerDialog::SetCtrlState()
{
	auto const selections = tree_->GetAllSelections();

	wxTreeItemId item;
	if (selections.size() == 1) {
		item = selections.front();
	}
	bool const multiple = selections.size() > 0;
	bool const predefined = IsPredefinedItem(item);

#ifdef __WXGTK__
	wxWindow* pFocus = FindFocus();
#endif

	CSiteManagerItemData* data = 0;
	if (item.IsOk()) {
		data = static_cast<CSiteManagerItemData*>(tree_->GetItemData(item));
	}
	if (!data) {
		// Set the control states according if it's possible to use the control
		const bool root_or_predefined = (item == tree_->GetRootItem() || item == m_ownSites || predefined);

		if (m_pNotebook_Bookmark) {
			m_pNotebook_Bookmark->Hide();
		}
		if (m_pNotebook_Site) {
			m_pNotebook_Site->Show();
			m_pNotebook_Site->SetSite(Site(), root_or_predefined || multiple);
			m_pNotebook_Site->Enable(false);
			m_pNotebook_Site->GetContainingSizer()->Layout();
		}

		xrc_call(*this, "ID_RENAME", &wxWindow::Enable, !selections.empty() && !root_or_predefined && !multiple);
		xrc_call(*this, "ID_DELETE", &wxWindow::Enable, !selections.empty() && !root_or_predefined);
		xrc_call(*this, "ID_COPY", &wxWindow::Enable, !selections.empty());
		xrc_call(*this, "ID_NEWFOLDER", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_NEWSITE", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_NEWBOOKMARK", &wxWindow::Enable, false);
		xrc_call(*this, "ID_CONNECT", &wxButton::Enable, false);
#ifdef __WXGTK__
		xrc_call(*this, "wxID_OK", &wxButton::SetDefault);
#endif
	}
	else if (data->m_site) {
		if (m_pNotebook_Bookmark) {
			m_pNotebook_Bookmark->Hide();
		}
		if (m_pNotebook_Site) {
			m_pNotebook_Site->Show();
			m_pNotebook_Site->SetSite(*data->m_site, predefined);
			m_pNotebook_Site->Enable(true);
			m_pNotebook_Site->GetContainingSizer()->Layout();
		}

		// Set the control states according if it's possible to use the control
		xrc_call(*this, "ID_RENAME", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_DELETE", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_COPY", &wxWindow::Enable, true);
		xrc_call(*this, "ID_NEWFOLDER", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_NEWSITE", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_NEWBOOKMARK", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_CONNECT", &wxButton::Enable, true);

#ifdef __WXGTK__
		xrc_call(*this, "ID_CONNECT", &wxButton::SetDefault);
#endif
	}
	else {
		if (m_pNotebook_Site) {
			m_pNotebook_Site->Hide();
			m_pNotebook_Site->GetContainingSizer()->Layout();
		}
		if (m_pNotebook_Bookmark) {
			m_pNotebook_Bookmark->Show();
		}

		// Set the control states according if it's possible to use the control
		xrc_call(*this, "ID_RENAME", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_DELETE", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_COPY", &wxWindow::Enable, true);
		xrc_call(*this, "ID_NEWFOLDER", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_NEWSITE", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_NEWBOOKMARK", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_CONNECT", &wxButton::Enable, true);

		xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxTextCtrl::ChangeValue, data->m_bookmark->m_localDir);
		xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_BOOKMARK_REMOTEDIR", &wxTextCtrl::ChangeValue, data->m_bookmark->m_remoteDir.GetPath());
		xrc_call(*this, "ID_BOOKMARK_REMOTEDIR", &wxWindow::Enable, !predefined);

		xrc_call(*this, "ID_BOOKMARK_SYNC", &wxCheckBox::Enable, !predefined);
		xrc_call(*this, "ID_BOOKMARK_SYNC", &wxCheckBox::SetValue, data->m_bookmark->m_sync);
		xrc_call(*this, "ID_BOOKMARK_COMPARISON", &wxCheckBox::Enable, !predefined);
		xrc_call(*this, "ID_BOOKMARK_COMPARISON", &wxCheckBox::SetValue, data->m_bookmark->m_comparison);
	}
#ifdef __WXGTK__
	if (pFocus && !pFocus->IsEnabled()) {
		for (wxWindow* pParent = pFocus->GetParent(); pParent; pParent = pParent->GetParent()) {
			if (pParent == this) {
				xrc_call(*this, "wxID_OK", &wxButton::SetFocus);
				break;
			}
		}
	}
#endif
}

void CSiteManagerDialog::OnSearch(wxCommandEvent&)
{
	CInputDialog dlg;
	if (!dlg.Create(this, _("Search sites"), _("Search for entries containing the entered text."))) {
		return;
	}

	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	wxString search = dlg.GetValue().Lower();

	bool match{};

	{
		wxWindowUpdateLocker l(this);

		m_is_deleting = true;
		tree_->UnselectAll();

		wxTreeItemId item = tree_->GetRootItem();
		while (item) {
			bool select{};
			if (item != tree_->GetRootItem() && item != m_ownSites && item != m_predefinedSites) {
				auto name = tree_->GetItemText(item).Lower();
				if (name.find(search) != wxString::npos) {
					select = true;
				}
			}

			if (select) {
				tree_->SafeSelectItem(item, false);
				match = true;
			}

			item = tree_->GetNextItemSimple(item, true);
		}
		SetCtrlState();
		m_is_deleting = false;
	}

	if (match) {
		tree_->SetFocus();
	}
	else {
		wxMessageBoxEx(wxString::Format(_("No entries found matching '%s'."), dlg.GetValue()), _("Search result"), wxICON_INFORMATION);
	}
}


void CSiteManagerDialog::OnCopySite(wxCommandEvent&)
	{
	std::vector<wxTreeItemId> items;

	wxTreeItemId item = tree_->GetSelection();
	if (item) {
		CSiteManagerItemData* data = static_cast<CSiteManagerItemData*>(tree_->GetItemData(item));
		if (data) {
			if (!Verify()) {
				return;
			}

			if (!UpdateItem()) {
				return;
			}
		}

		items.push_back(item);
	}
	else {
		auto selections = tree_->GetAllSelections();

		wxTreeItemId ancestor;
		for (auto const& item : selections) {
			if (!item.IsOk() || item == tree_->GetRootItem()) {
				return;
			}

			// Only keep items that do not have an ancestor that is already being copied
			auto parent = tree_->GetItemParent(item);
			while (parent && parent != ancestor) {
				parent = tree_->GetItemParent(parent);
			}
			if (!parent) {
				ancestor = item;
				items.push_back(item);
			}
		}
	}

	if (items.empty()) {
		return;
	}

	for (auto const& item : items) {
		wxTreeItemId parent = tree_->GetItemParent(item);
		if (!parent || IsPredefinedItem(parent) || parent == tree_->GetRootItem()) {
			parent = m_ownSites;
		}

		auto newItem = MoveItems(item, parent, true, false);

		if (items.size() == 1) {
			tree_->EnsureVisible(newItem);
			tree_->SafeSelectItem(newItem);
			tree_->EditLabel(newItem);
		}
	}
}

bool CSiteManagerDialog::LoadDefaultSites()
{
	CLocalPath const defaultsDir = GetDefaultsDir();
	if (defaultsDir.empty()) {
		return false;
	}

	CXmlFile file(defaultsDir.GetPath() + _T("fzdefaults.xml"));

	auto document = file.Load();
	if (!document) {
		return false;
	}

	auto element = document.child("Servers");
	if (!element) {
		return false;
	}

	int style = tree_->GetWindowStyle();
	tree_->SetWindowStyle(style | wxTR_HIDE_ROOT);
	wxTreeItemId root = tree_->AddRoot(wxString(), 0, 0);

	m_predefinedSites = tree_->AppendItem(root, _("Predefined Sites"), 0, 0);
	tree_->SetItemImage(m_predefinedSites, 1, wxTreeItemIcon_Expanded);
	tree_->SetItemImage(m_predefinedSites, 1, wxTreeItemIcon_SelectedExpanded);

	std::wstring lastSelection = options_.get_string(OPTION_SITEMANAGER_LASTSELECTED);
	if (!lastSelection.empty() && lastSelection[0] == '1') {
		if (lastSelection == _T("1")) {
			tree_->SafeSelectItem(m_predefinedSites);
		}
		else {
			lastSelection = lastSelection.substr(1);
		}
	}
	else {
		lastSelection.clear();
	}
	CSiteManagerXmlHandler_Tree handler(tree_, m_predefinedSites, lastSelection, true, options_.get_int(OPTION_DEFAULT_KIOSKMODE));

	CSiteManager::Load(element, handler, file.GetVersion());

	return true;
}

bool CSiteManagerDialog::IsPredefinedItem(wxTreeItemId item)
{
	while (item) {
		if (item == m_predefinedSites) {
			return true;
		}
		item = tree_->GetItemParent(item);
	}

	return false;
}

void CSiteManagerDialog::OnBeginDrag(wxTreeEvent& event)
{
#ifdef __WXMSW__
	// On a multi-selection tree control, if vetoing a selection change,
	// wxWidgets doesn't reset the drag source. Next time mouse is moved
	// drag is started even if mouse if up again...
	wxMouseState mouseState = wxGetMouseState();
	if (!mouseState.LeftIsDown()) {
		event.Veto();
		return;
	}
#endif

	if (options_.get_int(OPTION_DND_DISABLED) != 0) {
		event.Veto();
		return;
	}

	if (!Verify()) {
		event.Veto();
		return;
	}
	UpdateItem();

	wxTreeItemId dragItem = event.GetItem();
	if (!dragItem.IsOk() || !tree_->IsSelected(dragItem)) {
		event.Veto();
		return;
	}

	std::vector<wxTreeItemId> items;
	auto selections = tree_->GetAllSelections();

	bool predefined{};

	wxTreeItemId ancestor;
	for (auto const& item : selections) {
		if (!item.IsOk() || item == tree_->GetRootItem()) {
			return;
		}

		// Only keep items that do not have an ancestor that is already being copied
		auto parent = tree_->GetItemParent(item);
		while (parent && parent != ancestor) {
			parent = tree_->GetItemParent(parent);
		}
		if (!parent) {
			ancestor = item;
			items.push_back(item);

			predefined |= IsPredefinedItem(item);
			if (item == tree_->GetRootItem() || item == m_ownSites) {
				event.Veto();
				return;
			}
		}
	}

	if (items.empty()) {
		event.Veto();
		return;
	}

	CSiteManagerDialogDataObject obj;

	wxDropSource source(this);
	source.SetData(obj);

	draggedItems_ = items;

	source.DoDragDrop(predefined ? wxDrag_CopyOnly : wxDrag_DefaultMove);

	draggedItems_.clear();

	SetCtrlState();
}

struct itempair
{
	wxTreeItemId source;
	wxTreeItemId target;
};

wxTreeItemId CSiteManagerDialog::MoveItems(wxTreeItemId source, wxTreeItemId target, bool copy, bool use_existing_name)
{
	if (source == target) {
		return wxTreeItemId();
	}

	if (IsPredefinedItem(target)) {
		return wxTreeItemId();
	}

	if (IsPredefinedItem(source) && !copy) {
		return wxTreeItemId();
	}

	CSiteManagerItemData *pTargetData = (CSiteManagerItemData *)tree_->GetItemData(target);
	CSiteManagerItemData *pSourceData = (CSiteManagerItemData *)tree_->GetItemData(source);
	if (pTargetData) {
		if (pTargetData->m_bookmark) {
			return wxTreeItemId();
		}
		if (!pSourceData || pSourceData->m_site) {
			return wxTreeItemId();
		}
	}
	else if (pSourceData && !pSourceData->m_site) {
		return wxTreeItemId();
	}

	wxTreeItemId item = target;
	while (item != tree_->GetRootItem()) {
		if (item == source) {
			return wxTreeItemId();
		}
		item = tree_->GetItemParent(item);
	}

	if (!copy && tree_->GetItemParent(source) == target) {
		return wxTreeItemId();
	}

	wxString newName;

	if (use_existing_name) {
		wxString oldName = tree_->GetItemText(source);
		wxTreeItemIdValue cookie;
		for (auto child = tree_->GetFirstChild(target, cookie); child.IsOk(); child = tree_->GetNextChild(target, cookie)) {
			wxString const childName = tree_->GetItemText(child);
			if (!oldName.CmpNoCase(childName)) {
				wxMessageBoxEx(_("An item with the same name as the dragged item already exists at the target location."), _("Failed to copy or move sites"), wxICON_INFORMATION);
				return wxTreeItemId();
			}
		}
	}
	else {
		newName = FindFirstFreeName(target, tree_->GetItemText(source));
	}

	std::list<itempair> work;
	itempair initial;
	initial.source = source;
	initial.target = target;
	work.push_back(initial);

	std::list<wxTreeItemId> expand;

	wxTreeItemId ret;

	while (!work.empty()) {
		itempair pair = work.front();
		work.pop_front();

		wxString name = tree_->GetItemText(pair.source);
		if (!newName.empty()) {
			name = newName;
			newName.clear();
		}

		CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(tree_->GetItemData(pair.source));

		wxTreeItemId newItem = tree_->AppendItem(pair.target, name, data ? 2 : 0);
		if (!ret) {
			ret = newItem;
		}

		if (!data) {
			tree_->SetItemImage(newItem, 1, wxTreeItemIcon_Expanded);
			tree_->SetItemImage(newItem, 1, wxTreeItemIcon_SelectedExpanded);

			if (tree_->IsExpanded(pair.source)) {
				expand.push_back(newItem);
			}
		}
		else if (data->m_site) {
			CSiteManagerItemData* newData = new CSiteManagerItemData;
			newData->m_site = std::make_unique<Site>(*data->m_site);
			newData->connected_item = copy ? -1 : data->connected_item;
			tree_->SetItemData(newItem, newData);

			if (tree_->IsExpanded(pair.source)) {
				expand.push_back(newItem);
			}
		}
		else {
			tree_->SetItemImage(newItem, 3, wxTreeItemIcon_Normal);
			tree_->SetItemImage(newItem, 3, wxTreeItemIcon_Selected);

			CSiteManagerItemData* newData = new CSiteManagerItemData;
			newData->m_bookmark = std::make_unique<Bookmark>(*data->m_bookmark);
			tree_->SetItemData(newItem, newData);
		}

		wxTreeItemIdValue cookie2;
		for (auto child = tree_->GetFirstChild(pair.source, cookie2); child.IsOk(); child = tree_->GetNextChild(pair.source, cookie2)) {
			itempair newPair;
			newPair.source = child;
			newPair.target = newItem;
			work.push_back(newPair);
		}

		tree_->SortChildren(pair.target);
	}

	if (!copy) {
		wxTreeItemId parent = tree_->GetItemParent(source);
		if (tree_->GetChildrenCount(parent) == 1) {
			tree_->Collapse(parent);
		}

#ifdef __WXMSW__
		// Delete tries to move selection to next visible, collapse so that it's never a child.
		tree_->Collapse(source);
#endif
		tree_->Delete(source);
	}

	for (auto iter = expand.begin(); iter != expand.end(); ++iter) {
		tree_->Expand(*iter);
	}

	tree_->Expand(target);

	return ret;
}

void CSiteManagerDialog::OnChar(wxKeyEvent& event)
{
	if (event.GetKeyCode() != WXK_F2) {
		event.Skip();
		return;
	}

	wxCommandEvent cmdEvent;
	OnRename(cmdEvent);
}

void CSiteManagerDialog::CopyAddServer(Site const& site)
{
	if (!Verify()) {
		return;
	}

	AddNewSite(m_ownSites, site, true);
}

wxString CSiteManagerDialog::FindFirstFreeName(wxTreeItemId const& parent, wxString const& name)
{
	wxString newName = name;
	wxString root = name;

	size_t pos = root.find_last_not_of(L"0123456789");
	int index = 1;
	if (pos != std::wstring::npos && pos + 1 < newName.size() && newName.size() - pos < 10) {
		root = newName.substr(0, pos + 1);
		index = fz::to_integral<int>(newName.substr(pos + 1).ToStdWstring());
	}
	else {
		root += L" ";
	}
	for (;;) {
		wxTreeItemId child;
		wxTreeItemIdValue cookie;
		child = tree_->GetFirstChild(parent, cookie);
		bool found = false;
		while (child.IsOk()) {
			wxString child_name = tree_->GetItemText(child);
			int cmp = child_name.CmpNoCase(newName);
			if (!cmp) {
				found = true;
				break;
			}

			child = tree_->GetNextChild(parent, cookie);
		}
		if (!found) {
			break;
		}

		newName = root + wxString::Format(_T("%d"), ++index);
	}

	return newName;
}

void CSiteManagerDialog::AddNewSite(wxTreeItemId parent, Site const& site, bool connected)
{
	wxString name = FindFirstFreeName(parent, _("New site"));

	CSiteManagerItemData* pData = new CSiteManagerItemData;
	pData->m_site = std::make_unique<Site>();
	*pData->m_site = site;

	// Erase updated server info
	pData->m_site->server = site.GetOriginalServer();
	pData->m_site->originalServer.reset();
	if (connected) {
		pData->connected_item = 0;
	}

	wxTreeItemId newItem = tree_->AppendItem(parent, name, 2, 2, pData);
	tree_->SortChildren(parent);
	tree_->EnsureVisible(newItem);
	tree_->SafeSelectItem(newItem);
#ifdef __WXMAC__
	// Need to trigger dirty processing of generic tree control.
	// Else edit control will be hidden behind item
	tree_->OnInternalIdle();
#endif
	tree_->EditLabel(newItem);
}

void CSiteManagerDialog::AddNewBookmark(wxTreeItemId parent)
{
	wxString name = FindFirstFreeName(parent, _("New bookmark"));

	CSiteManagerItemData* pData = new CSiteManagerItemData;
	pData->m_bookmark = std::make_unique<Bookmark>();
	wxTreeItemId newItem = tree_->AppendItem(parent, name, 3, 3, pData);
	tree_->SortChildren(parent);
	tree_->EnsureVisible(newItem);
	tree_->SafeSelectItem(newItem);
	tree_->EditLabel(newItem);
}

void CSiteManagerDialog::RememberLastSelected()
{
	std::wstring path;

	wxTreeItemId sel = tree_->GetSelection();
	if (sel) {
		path = GetSitePath(sel);
	}

	options_.set(OPTION_SITEMANAGER_LASTSELECTED, path);
}

void CSiteManagerDialog::OnContextMenu(wxTreeEvent&)
{
	if (!Verify()) {
		return;
	}
	UpdateItem();

	wxMenu menu;
	menu.Append(XRCID("ID_EXPORT"), _("&Export..."));

	PopupMenu(&menu);
}

void CSiteManagerDialog::OnExportSelected(wxCommandEvent&)
{
	if (!Verify()) {
		return;
	}
	UpdateItem();

	wxFileDialog dlg(this, _("Select file for exported sites"), wxString(),
					_T("sites.xml"), _T("XML files (*.xml)|*.xml"),
					wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	CXmlFile xml(dlg.GetPath().ToStdWstring());

	auto exportRoot = xml.CreateEmpty();

	auto servers = exportRoot.append_child("Servers");

	auto selections = tree_->GetAllSelections();

	wxTreeItemId ancestor;
	for (auto const& item : selections) {
		if (!item.IsOk()) {
			continue;
		}

		// Only keep items that do not have an ancestor that is already being copied
		auto parent = tree_->GetItemParent(item);
		while (parent && parent != ancestor) {
			parent = tree_->GetItemParent(parent);
		}
		if (!parent) {
			ancestor = item;
			if (item == m_ownSites || item == m_predefinedSites) {
				Save(servers, item);
			}
			else {
				SaveChild(servers, item);
			}
		}
	}

	if (!xml.Save()) {
		wxString msg = wxString::Format(_("Could not write \"%s\", the selected sites could not be exported: %s"), xml.GetFileName(), xml.GetError());
		wxMessageBoxEx(msg, _("Error writing xml file"), wxICON_ERROR);
	}
}

void CSiteManagerDialog::OnBookmarkBrowse(wxCommandEvent&)
{
	wxTreeItemId item = tree_->GetSelection();
	if (!item.IsOk()) {
		return;
	}

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(tree_->GetItemData(item));
	if (!data || !data->m_bookmark) {
		return;
	}

	wxDirDialog dlg(this, _("Choose the local directory"), XRCCTRL(*this, "ID_BOOKMARK_LOCALDIR", wxTextCtrl)->GetValue(), wxDD_NEW_DIR_BUTTON);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	XRCCTRL(*this, "ID_BOOKMARK_LOCALDIR", wxTextCtrl)->ChangeValue(dlg.GetPath());
}

void CSiteManagerDialog::OnNewBookmark(wxCommandEvent&)
{
	wxTreeItemId item = tree_->GetSelection();
	if (!item.IsOk() || IsPredefinedItem(item)) {
		return;
	}

	CSiteManagerItemData *pData = (CSiteManagerItemData *)tree_->GetItemData(item);
	if (!pData) {
		return;
	}
	if (pData->m_bookmark) {
		item = tree_->GetItemParent(item);
	}

	if (!Verify()) {
		return;
	}

	AddNewBookmark(item);
}

std::wstring CSiteManagerDialog::GetSitePath(wxTreeItemId item, bool stripBookmark)
{
	wxASSERT(item);

	CSiteManagerItemData* pData = static_cast<CSiteManagerItemData* >(tree_->GetItemData(item));
	if (!pData) {
		return std::wstring();
	}

	if (stripBookmark && pData->m_bookmark) {
		item = tree_->GetItemParent(item);
	}

	std::wstring path;
	while (item) {
		if (item == m_predefinedSites) {
			return _T("1") + path;
		}
		else if (item == m_ownSites) {
			return _T("0") + path;
		}
		path = _T("/") + CSiteManager::EscapeSegment(tree_->GetItemText(item).ToStdWstring()) + path;

		item = tree_->GetItemParent(item);
	}

	return _T("0") + path;
}
