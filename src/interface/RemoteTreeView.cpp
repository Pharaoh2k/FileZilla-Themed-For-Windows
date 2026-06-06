#include "filezilla.h"

#include "chmoddialog.h"
#include "commandqueue.h"
#include "dndobjects.h"
#include "dragdropmanager.h"
#include "drop_target_ex.h"
#include "graphics.h"
#include "inputdialog.h"
#include "Options.h"
#include "queue.h"
#include "QueueView.h"
#include "RemoteTreeView.h"
#include "remote_recursive_operation.h"
#include "themeprovider.h"
#include "../commonui/misc.h"

#include <libfilezilla/uri.hpp>

#include <wx/clipbrd.h>
#include <wx/menu.h>

#include <algorithm>

class CItemData : public wxTreeItemData
{
public:
	CItemData(CServerPath path) : m_path(path) {}
	CServerPath m_path;
};

class CRemoteTreeViewDropTarget final : public CFileDropTarget<wxTreeCtrlEx>
{
public:
	CRemoteTreeViewDropTarget(CRemoteTreeView* pRemoteTreeView)
		: CFileDropTarget<wxTreeCtrlEx>(pRemoteTreeView, pRemoteTreeView->options_, pRemoteTreeView->login_manager_)
		, m_pRemoteTreeView(pRemoteTreeView)
	{
	}

	wxTreeItemId GetHit(const wxPoint& point)
	{
		int flags{};
		wxTreeItemId hit = m_pRemoteTreeView->HitTest(point, flags);

		if (flags & (wxTREE_HITTEST_ABOVE | wxTREE_HITTEST_BELOW | wxTREE_HITTEST_NOWHERE | wxTREE_HITTEST_TOLEFT | wxTREE_HITTEST_TORIGHT)) {
			return wxTreeItemId();
		}

		return hit;
	}

	virtual wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def)
	{
		if (def == wxDragError ||
			def == wxDragNone ||
			def == wxDragCancel)
		{
			return def;
		}

		wxTreeItemId hit = GetHit(wxPoint(x, y));
		if (!hit) {
			return wxDragNone;
		}

		CServerPath path = m_pRemoteTreeView->GetPathFromItem(hit);
		if (path.empty()) {
			return wxDragNone;
		}

		if (!GetData()) {
			return wxDragError;
		}

		CDragDropManager* pDragDropManager = CDragDropManager::Get();
		if (pDragDropManager) {
			pDragDropManager->pDropTarget = m_pRemoteTreeView;
		}

		auto const format = GetReceivedFormat();
		if (format == m_pFileDataObject->GetFormat()) {
			m_pRemoteTreeView->m_state.UploadDroppedFiles(m_pFileDataObject, path, false);
		}
		else if (format == LocalDataObjectFormat()) {
			m_pRemoteTreeView->m_state.UploadDroppedFiles(GetLocalDataObject(), path, false);
		}
		else {
			auto * obj = GetRemoteDataObject();
			if (obj->GetProcessId() != (int)wxGetProcessId()) {
				wxMessageBoxEx(_("Drag&drop between different instances of FileZilla has not been implemented yet."));
				return wxDragNone;
			}

			if (!m_pRemoteTreeView->m_state.GetSite() || obj->GetSite().server != m_pRemoteTreeView->m_state.GetSite().server) {
				wxMessageBoxEx(_("Drag&drop between different servers has not been implemented yet."));
				return wxDragNone;
			}

			// Make sure path path is valid
			if (path == obj->GetServerPath()) {
				wxMessageBoxEx(_("Source and path of the drop operation are identical"));
				return wxDragNone;
			}

			std::vector<CRemoteDataObject::t_fileInfo> const& files = obj->GetFiles();
			for (auto const& info : files) {
				if (info.dir) {
					CServerPath dir = obj->GetServerPath();
					dir.AddSegment(info.name);
					if (dir == path) {
						return wxDragNone;
					}
					else if (dir.IsParentOf(path, false)) {
						wxMessageBoxEx(_("A directory cannot be dragged into one of its subdirectories."));
						return wxDragNone;
					}
				}
			}

			for (auto const& info : files) {
				m_pRemoteTreeView->m_state.m_pCommandQueue->ProcessCommand(
					new CRenameCommand(obj->GetServerPath(), info.name, path, info.name)
					);
			}

			return wxDragNone;
		}

		return def;
	}

	virtual bool OnDrop(wxCoord x, wxCoord y)
	{
		if (!CScrollableDropTarget::OnDrop(x, y)) {
			return false;
		}
		m_pRemoteTreeView->ClearDropHighlight();

		wxTreeItemId hit = GetHit(wxPoint(x, y));
		if (!hit) {
			return false;
		}

		const CServerPath& path = m_pRemoteTreeView->GetPathFromItem(hit);
		if (path.empty()) {
			return false;
		}

		return true;
	}

	wxTreeItemId DisplayDropHighlight(wxPoint const& point) override
	{
		wxTreeItemId hit = GetHit(point);
		if (!hit) {
			m_pRemoteTreeView->ClearDropHighlight();
			return wxTreeItemId();
		}

		CServerPath const& path = m_pRemoteTreeView->GetPathFromItem(hit);

		if (path.empty()) {
			m_pRemoteTreeView->ClearDropHighlight();
			return wxTreeItemId();
		}

		m_pRemoteTreeView->DisplayDropHighlight(hit);

		return hit;
	}

	virtual wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = CScrollableDropTarget::OnDragOver(x, y, def);

		if (def == wxDragError ||
			def == wxDragNone ||
			def == wxDragCancel)
		{
			m_pRemoteTreeView->ClearDropHighlight();
			return def;
		}

		wxTreeItemId hit = DisplayDropHighlight(wxPoint(x, y));
		if (!hit.IsOk()) {
			return wxDragNone;
		}

		if (def == wxDragLink) {
			def = wxDragCopy;
		}

		return def;
	}

	virtual void OnLeave()
	{
		CScrollableDropTarget::OnLeave();
		m_pRemoteTreeView->ClearDropHighlight();
	}

	virtual wxDragResult OnEnter(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = CScrollableDropTarget::OnEnter(x, y, def);
		return OnDragOver(x, y, def);
	}

protected:
	CRemoteTreeView *m_pRemoteTreeView{};
};

BEGIN_EVENT_TABLE(CRemoteTreeView, wxTreeCtrlEx)
EVT_TREE_ITEM_EXPANDING(wxID_ANY, CRemoteTreeView::OnItemExpanding)
EVT_TREE_SEL_CHANGED(wxID_ANY, CRemoteTreeView::OnSelectionChanged)
EVT_TREE_ITEM_ACTIVATED(wxID_ANY, CRemoteTreeView::OnItemActivated)
EVT_TREE_BEGIN_DRAG(wxID_ANY, CRemoteTreeView::OnBeginDrag)
EVT_TREE_ITEM_MENU(wxID_ANY, CRemoteTreeView::OnContextMenu)
EVT_MENU(XRCID("ID_CHMOD"), CRemoteTreeView::OnMenuChmod)
EVT_MENU(XRCID("ID_DOWNLOAD"), CRemoteTreeView::OnMenuDownload)
EVT_MENU(XRCID("ID_ADDTOQUEUE"), CRemoteTreeView::OnMenuDownload)
EVT_MENU(XRCID("ID_DELETE"), CRemoteTreeView::OnMenuDelete)
EVT_MENU(XRCID("ID_RENAME"), CRemoteTreeView::OnMenuRename)
EVT_TREE_BEGIN_LABEL_EDIT(wxID_ANY, CRemoteTreeView::OnBeginLabelEdit)
EVT_TREE_END_LABEL_EDIT(wxID_ANY, CRemoteTreeView::OnEndLabelEdit)
EVT_MENU(XRCID("ID_MKDIR"), CRemoteTreeView::OnMkdir)
EVT_MENU(XRCID("ID_MKDIR_CHGDIR"), CRemoteTreeView::OnMenuMkdirChgDir)
EVT_CHAR(CRemoteTreeView::OnChar)
EVT_MENU(XRCID("ID_GETURL"), CRemoteTreeView::OnMenuGeturl)
END_EVENT_TABLE()

CRemoteTreeView::CRemoteTreeView(wxWindow* parent, wxWindowID id, CState& state, CQueueView* pQueue, COptionsBase & options, login_manager & lim)
	: wxTreeCtrlEx(parent, id, wxDefaultPosition, wxDefaultSize, DEFAULT_TREE_STYLE | wxTAB_TRAVERSAL | wxTR_EDIT_LABELS | wxNO_BORDER | wxTR_HIDE_ROOT)
	, CSystemImageList(CThemeProvider::GetIconSize(iconSizeSmall).x)
	, CStateEventHandler(state)
	, COptionChangeEventHandler(this)
	, login_manager_(lim)
	, options_(options)
{
#ifdef __WXMAC__
	SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
#endif

	state.RegisterHandler(this, STATECHANGE_REMOTE_DIR);
	state.RegisterHandler(this, STATECHANGE_APPLYFILTER);
	state.RegisterHandler(this, STATECHANGE_SERVER);

	CreateImageList();

	UpdateSortMode();
	options_.watch(OPTION_FILELIST_NAMESORT, this);

	m_pQueue = pQueue;
	AddRoot(_T(""));
	m_ExpandAfterList = wxTreeItemId();

	SetDropTarget(new CRemoteTreeViewDropTarget(this));

	Enable(false);

	m_windowTinter = std::make_unique<CWindowTinter>(*this);
}

CRemoteTreeView::~CRemoteTreeView()
{
	options_.unwatch_all(this);
	SetImageList(0);
	delete m_pImageList;
}

void CRemoteTreeView::OnStateChange(t_statechange_notifications notification, std::wstring const&, const void* data2)
{
	if (notification == STATECHANGE_REMOTE_DIR) {
		SetDirectoryListing(m_state.GetRemoteDir(), data2 ? *reinterpret_cast<bool const*>(data2) : true);
	}
	else if (notification == STATECHANGE_APPLYFILTER) {
		ApplyFilters(false);
	}
	else if (notification == STATECHANGE_SERVER) {
		m_windowTinter->SetBackgroundTint(m_state.GetSite().m_colour);
	}
}

void CRemoteTreeView::SetDirectoryListing(std::shared_ptr<CDirectoryListing> const& pListing, bool primary)
{
	m_busy = true;

	if (!pListing) {
		m_ExpandAfterList = wxTreeItemId();
		DeleteAllItems();
		AddRoot(wxString());
		m_busy = false;
		if (FindFocus() == this) {
			wxNavigationKeyEvent *evt = new wxNavigationKeyEvent();
			evt->SetFromTab(true);
			evt->SetEventObject(this);
			evt->SetDirection(true);
			QueueEvent(evt);
		}
		Enable(false);
		m_contextMenuItem = wxTreeItemId();
		return;
	}
	Enable(true);
#ifdef __WXGTK__
	GetParent()->m_dirtyTabOrder = true;
#endif

	if (pListing->get_unsure_flags() && !(pListing->get_unsure_flags() & ~(CDirectoryListing::unsure_unknown | CDirectoryListing::unsure_file_mask))) {
		// Just files changed, does not affect directory tree
		m_busy = false;
		return;
	}

	bool const wasEmpty = GetChildrenCount(GetRootItem()) == 0;

#ifndef __WXMSW__
	Freeze();
#endif
	wxTreeItemId parent = MakeParent(pListing->path, primary);
	if (!parent) {
		m_busy = false;
#ifndef __WXMSW__
		Thaw();
#endif
		return;
	}

	if (wasEmpty && GetItemParent(parent) == GetRootItem() && !m_ExpandAfterList) {
		m_ExpandAfterList = parent;
	}

	if (!IsExpanded(parent) && parent != m_ExpandAfterList) {
		DeleteChildren(parent);
		CFilterManager filter;
		if (HasSubdirs(*pListing, filter)) {
			AppendItem(parent, wxString(), -1, -1);
		}
	}
	else {
		RefreshItem(parent, *pListing, primary);

		if (m_ExpandAfterList == parent) {
#ifndef __WXMSW__
			// Prevent CalculatePositions from being called
			wxGenericTreeItem *anchor = m_anchor;
			m_anchor = 0;
#endif
			Expand(parent);
#ifndef __WXMSW__
			m_anchor = anchor;
#endif
		}
	}
	m_ExpandAfterList = wxTreeItemId();

	SetItemImages(parent, false);

#ifndef __WXMSW__
	Thaw();
#endif
	if (primary) {
		SafeSelectItem(parent);
	}
#ifndef __WXMSW__
	else {
		Refresh();
	}
#endif

	m_busy = false;
}

wxTreeItemId CRemoteTreeView::MakeParent(CServerPath path, bool select)
{
	std::vector<std::wstring> pieces;
	pieces.reserve(path.SegmentCount() + 1);
	while (path.HasParent()) {
		pieces.push_back(path.GetLastSegment());
		path.MakeParent();
	}
	wxASSERT(!path.GetPath().empty());
	pieces.push_back(path.GetPath());

	const wxTreeItemId root = GetRootItem();
	wxTreeItemId parent = root;

	for (auto iter = pieces.crbegin(); iter != pieces.crend(); ++iter) {
		if (iter != pieces.rbegin()) {
			path.AddSegment(*iter);
		}

		wxTreeItemIdValue cookie;
		wxTreeItemId child = GetFirstChild(parent, cookie);
		if (child && GetItemText(child).empty()) {
			Delete(child);
			child = wxTreeItemId();
			if (parent != root) {
				ListExpand(parent);
			}
		}
		for (child = GetFirstChild(parent, cookie); child; child = GetNextSibling(child)) {
			const wxString& text = GetItemText(child);
			if (text == *iter) {
				break;
			}
		}
		if (!child) {
			CDirectoryListing listing;

			if (m_state.engine_->CacheLookup(path, listing) == FZ_REPLY_OK) {
				child = AppendItem(parent, *iter, 0, 2, path.HasParent() ? 0 : new CItemData(path));
				SetItemImages(child, false);
			}
			else {
				child = AppendItem(parent, *iter, 1, 3, path.HasParent() ? 0 : new CItemData(path));
				SetItemImages(child, true);
			}
			SortChildren(parent);

			auto nextIter = iter;
			++nextIter;
			if (nextIter != pieces.rend()) {
				DisplayItem(child, listing);
			}
		}
		if (select && iter != pieces.rbegin()) {
#ifndef __WXMSW__
			// Prevent CalculatePositions from being called
			wxGenericTreeItem *anchor = m_anchor;
			m_anchor = 0;
#endif
			Expand(parent);
#ifndef __WXMSW__
			m_anchor = anchor;
#endif
		}

		parent = child;
	}

	return parent;
}

wxBitmap CRemoteTreeView::CreateIcon(int index, const wxString& overlay)
{
	auto * imageList = GetSystemImageList();
#ifdef __WXMSW__
	// Need to use wxImageList::GetIcon, wxImageList::GetBitmap kills the alpha channel on MSW...
	wxBitmap bmp = imageList ? imageList->GetIcon(index) : wxBitmap();
#else
	wxBitmap bmp = imageList ? imageList->GetBitmap(index) : wxBitmap();
#endif
	if (!overlay.empty()) {
		wxBitmap unknown = CThemeProvider::Get()->CreateBitmap(overlay, wxART_OTHER, bmp.GetScaledSize());
		Overlay(bmp, unknown);
	}

	if (!bmp.IsOk()) {
		bmp.Create(CThemeProvider::GetIconSize(iconSizeSmall));
	}

	return bmp;
}

void CRemoteTreeView::CreateImageList()
{
	if (!GetSystemImageList()) {
		return;
	}

	// Normal directory
	int index = GetIconIndex(iconType::dir, _T("{78013B9C-3532-4fe1-A418-5CD1955127CC}"), false);
	wxBitmap dirIcon = CreateIcon(index);

	wxSize s = dirIcon.GetScaledSize();

	// Must create image list only after we know actual icon size,
	// wxSystemSettings cannot return the correct size due to a bug in
	// Windows. On Vista and possibly 7, the documentation does not
	// match Windows' actual behavior.
	m_pImageList = new wxImageList(s.x, s.y, true, 4);

	m_pImageList->Add(dirIcon);
	m_pImageList->Add(CreateIcon(index, _T("ART_UNKNOWN")));

	// Opened directory
	index = GetIconIndex(iconType::opened_dir, _T("{78013B9C-3532-4fe1-A418-5CD1955127CC}"), false);
	m_pImageList->Add(CreateIcon(index));
	m_pImageList->Add(CreateIcon(index, _T("ART_UNKNOWN")));

	SetImageList(m_pImageList);
}

bool CRemoteTreeView::HasSubdirs(const CDirectoryListing& listing, const CFilterManager& filter)
{
	if (!listing.has_dirs()) {
		return false;
	}

	if (!filter.HasActiveFilters()) {
		return true;
	}

	std::wstring const path = listing.path.GetPath();
	for (size_t i = 0; i < listing.size(); ++i) {
		auto const& entry = listing[i];
		if (!entry.is_dir()) {
			continue;
		}

		if (filter.FilenameFiltered(entry.name, path, true, -1, false, 0, entry.time)) {
			continue;
		}

		return true;
	}

	return false;
}

void CRemoteTreeView::DisplayItem(wxTreeItemId parent, const CDirectoryListing& listing)
{
	DeleteChildren(parent);

	std::wstring const path = listing.path.GetPath();

	CFilterManager const& filter = m_state.GetStateFilterManager();

	wxTreeItemId last;
	for (size_t i = 0; i < listing.size(); ++i) {
		auto const& entry = listing[i];
		if (!entry.is_dir()) {
			continue;
		}

		if (filter.FilenameFiltered(entry.name, path, true, -1, false, 0, entry.time)) {
			continue;
		}

		std::wstring const& name = entry.name;
		CServerPath subdir = listing.path;
		subdir.AddSegment(name);

		CDirectoryListing subListing;

		if (m_state.engine_->CacheLookup(subdir, subListing) == FZ_REPLY_OK) {
			wxTreeItemId child = AppendItem(parent, name, 0, 2, 0);
			SetItemImages(child, false);

			if (HasSubdirs(subListing, filter)) {
				AppendItem(child, L"", -1, -1);
			}
		}
		else {
			wxTreeItemId child = InsertItem(parent, last, name, 1, 3, 0);
			SetItemImages(child, true);
		}
	}
	SortChildren(parent);
}

void CRemoteTreeView::RefreshItem(wxTreeItemId parent, const CDirectoryListing& listing, bool will_select_parent)
{
	SetItemImages(parent, false);

	wxTreeItemIdValue cookie;
	wxTreeItemId child = GetFirstChild(parent, cookie);
	if (!child || GetItemText(child).empty()) {
		DisplayItem(parent, listing);
		return;
	}

	CFilterManager filter;

	std::wstring const path = listing.path.GetPath();

	std::vector<std::wstring> dirs;
	for (size_t i = 0; i < listing.size(); ++i) {
		auto const& entry = listing[i];
		if (!entry.is_dir()) {
			continue;
		}

		if (!filter.FilenameFiltered(entry.name, path, true, -1, false, 0, entry.time)) {
			dirs.push_back(entry.name);
		}
	}

	std::sort(dirs.begin(), dirs.end(), [&](auto const& lhs, auto const& rhs) { return sortFunction_(lhs, rhs) < 0; });

	std::vector<wxTreeItemId> toDelete;

	wxTreeItemIdValue unused;
	child = GetFirstChild(parent, unused);

	wxTreeItemId firstInserted;
	wxTreeItemId last = GetLastChild(parent);

	auto iter = dirs.begin();
	while (child || iter != dirs.end()) {
		int cmp;
		if (child && iter != dirs.end()) {
			wxString const& childName = GetItemText(child);
			cmp = sortFunction_(*iter, std::wstring_view(childName.data(), childName.size()));
		}
		else if (child) {
			cmp = 1;
		}
		else {
			cmp = -1;
		}

		if (!cmp) {
			CServerPath childPath = listing.path;
			childPath.AddSegment(*iter);

			CDirectoryListing subListing;
			if (m_state.engine_->CacheLookup(childPath, subListing) == FZ_REPLY_OK) {
				if (!GetLastChild(child) && HasSubdirs(subListing, filter)) {
					AppendItem(child, _T(""), -1, -1);
				}
				SetItemImages(child, false);
			}
			else {
				SetItemImages(child, true);
			}

			child = GetNextSibling(child);
			if (child == firstInserted) {
				child = wxTreeItemId{};
			}
			++iter;
		}
		else if (cmp > 0) {
			// Child no longer exists
			wxTreeItemId sel = GetSelection();
			while (sel && sel != child) {
				sel = GetItemParent(sel);
			}
			if (!sel || will_select_parent) {
				toDelete.push_back(child);
			}
			child = GetNextSibling(child);
			if (child == firstInserted) {
				child = wxTreeItemId{};
			}
		}
		else if (cmp < 0) {
			// New directory
			CServerPath childPath = listing.path;
			childPath.AddSegment(*iter);

			CDirectoryListing subListing;
			if (m_state.engine_->CacheLookup(childPath, subListing) == FZ_REPLY_OK) {
				last = InsertItem(parent, last, *iter, 0, 2, 0);
				SetItemImages(last, false);

				if (HasSubdirs(subListing, filter)) {
					AppendItem(last, _T(""), -1, -1);
				}
			}
			else {
				last = InsertItem(parent, last, *iter, 1, 3, 0);
				SetItemImages(last, true);
			}

			if (!firstInserted) {
				firstInserted = last;
			}
			++iter;
		}
	}

	for (auto it = toDelete.rbegin(); it != toDelete.rend(); ++it) {
		Delete(*it);
	}

	if (firstInserted) {
		SortChildren(parent);
	}
}

void CRemoteTreeView::OnItemExpanding(wxTreeEvent& event)
{
	if (m_busy) {
		return;
	}

	wxTreeItemId item = event.GetItem();
	if (!item) {
		return;
	}

	if (!ListExpand(item)) {
		event.Veto();
		return;
	}

	Refresh(false);
}

void CRemoteTreeView::SetItemImages(wxTreeItemId item, bool unknown)
{
	const int old_image = GetItemImage(item, wxTreeItemIcon_Normal);
	if (!unknown)
	{
		if (old_image == 0)
			return;
		SetItemImage(item, 0, wxTreeItemIcon_Normal);
		SetItemImage(item, 2, wxTreeItemIcon_Selected);
		SetItemImage(item, 0, wxTreeItemIcon_Expanded);
		SetItemImage(item, 2, wxTreeItemIcon_SelectedExpanded);
	}
	else
	{
		if (old_image == 1)
			return;
		SetItemImage(item, 1, wxTreeItemIcon_Normal);
		SetItemImage(item, 3, wxTreeItemIcon_Selected);
		SetItemImage(item, 1, wxTreeItemIcon_Expanded);
		SetItemImage(item, 3, wxTreeItemIcon_SelectedExpanded);
	}
}

void CRemoteTreeView::OnSelectionChanged(wxTreeEvent& event)
{
	if (event.GetItem() != m_ExpandAfterList) {
		m_ExpandAfterList = wxTreeItemId();
	}
	if (m_busy) {
		return;
	}

	if (!m_state.IsRemoteIdle(true)) {
		wxBell();
		return;
	}

	wxTreeItemId item = event.GetItem();
	if (!item) {
		return;
	}

	const CServerPath path = GetPathFromItem(item);
	wxASSERT(!path.empty());
	if (path.empty()) {
		return;
	}

	m_state.ChangeRemoteDir(path);
}

void CRemoteTreeView::OnItemActivated(wxTreeEvent& event)
{
	m_ExpandAfterList = GetSelection();
	event.Skip();
}

CServerPath CRemoteTreeView::GetPathFromItem(const wxTreeItemId& item) const
{
	std::list<wxString> segments;

	wxTreeItemId i = item;
	while (i != GetRootItem()) {
		const CItemData* const pData = (const CItemData*)GetItemData(i);
		if (pData) {
			CServerPath path = pData->m_path;
			for (std::list<wxString>::const_iterator iter = segments.begin(); iter != segments.end(); ++iter) {
				if (!path.AddSegment(iter->ToStdWstring())) {
					return CServerPath();
				}
			}
			return path;
		}

		segments.push_front(GetItemText(i));
		i = GetItemParent(i);
	}

	return CServerPath();
}

void CRemoteTreeView::OnBeginDrag(wxTreeEvent& event)
{
	if (options_.get_int(OPTION_DND_DISABLED) != 0) {
		return;
	}

	// Drag could result in recursive operation, don't allow at this point
	if (!m_state.IsRemoteIdle()) {
		wxBell();
		return;
	}

	const wxTreeItemId& item = event.GetItem();
	if (!item) {
		return;
	}

	CServerPath path = GetPathFromItem(item);
	if (path.empty() || !path.HasParent()) {
		return;
	}

	const CServerPath& parent = path.GetParent();
	std::wstring const lastSegment = path.GetLastSegment();
	if (lastSegment.empty()) {
		return;
	}

	wxDataObjectComposite object;

	Site site = m_state.GetSite(); // Make a copy as DoDragDrop later runs the event loop
	if (!site) {
		return;
	}

	CRemoteDataObject *pRemoteDataObject = new CRemoteDataObject(login_manager_, options_, site, parent);
	pRemoteDataObject->AddFile(lastSegment, true, -1, false);

	pRemoteDataObject->Finalize();

	object.Add(pRemoteDataObject, true);

#if FZ3_USESHELLEXT
	std::unique_ptr<CShellExtensionInterface> ext = CShellExtensionInterface::CreateInitialized();
	if (ext) {
		const wxString& file = ext->GetDragDirectory();

		wxASSERT(!file.empty());

		wxFileDataObject *pFileDataObject = new wxFileDataObject;
		pFileDataObject->AddFile(file);

		object.Add(pFileDataObject);
	}
#endif

	CDragDropManager* pDragDropManager = CDragDropManager::Init();
	pDragDropManager->pDragSource = this;
	pDragDropManager->site = site;
	pDragDropManager->remoteParent = parent;
	pDragDropManager->dragDataObject = pRemoteDataObject;

	DropSource source(this);
	source.SetData(object);
	int res = source.DoFileDragDrop();

	pDragDropManager->Release();

	if (res != wxDragCopy) {
		return;
	}

#if FZ3_USESHELLEXT || defined(__WXMAC__)
#if FZ3_USESHELLEXT
	if (ext) {
		if (!pRemoteDataObject->DidSendData()) {
#else
	if (!source.m_OutDir.IsEmpty()) {
#endif
			Site newSite = m_state.GetSite();
			if (!newSite || !m_state.IsRemoteIdle() || site != newSite) {
				wxBell();
				return;
			}

#if FZ3_USESHELLEXT
			CLocalPath target(ext->GetTarget().ToStdWstring());
#else
			// wxMessageBoxEx(source.m_OutDir);
			CLocalPath target(source.m_OutDir.ToStdWstring());
#endif
			if (target.empty()) {
#if FZ3_USESHELLEXT
				ext.reset(); // Release extension before the modal message box
				wxMessageBoxEx(_("Could not determine the target of the Drag&Drop operation.\nEither the shell extension is not installed properly or you didn't drop the files into an Explorer window."));
#else
				wxMessageBoxEx(_("Could not determine the target of the Drag&Drop operation."));
#endif
				return;
			}

			m_state.DownloadDroppedFiles(pRemoteDataObject, target);
#if FZ3_USESHELLEXT
		}
#endif
	}
#endif
}

void CRemoteTreeView::OnContextMenu(wxTreeEvent& event)
{
	m_contextMenuItem = event.GetItem();

	wxMenu menu;
	auto item = new wxMenuItem(&menu, XRCID("ID_DOWNLOAD"), _("&Download"), _("Download selected directory"));
	item->SetBitmap(MakeBmpBundle(wxArtProvider::GetBitmap(_T("ART_DOWNLOAD"), wxART_MENU)));
	menu.Append(item);
	item = new wxMenuItem(&menu, XRCID("ID_ADDTOQUEUE"), _("&Add to queue"), _("Add selected directory to the transfer queue"));
	item->SetBitmap(MakeBmpBundle(wxArtProvider::GetBitmap(_T("ART_DOWNLOADADD"), wxART_MENU)));
	menu.Append(item);

	menu.AppendSeparator();
	menu.Append(XRCID("ID_MKDIR"), _("&Create directory"), _("Create a new subdirectory in the current directory"));
	menu.Append(XRCID("ID_MKDIR_CHGDIR"), _("Create director&y and enter it"), _("Create a new subdirectory in the current directory and change into it"));

	menu.AppendSeparator();
	menu.Append(XRCID("ID_DELETE"), _("D&elete"), _("Delete selected directory"));
	menu.Append(XRCID("ID_RENAME"), _("&Rename"), _("Rename selected directory"));
	menu.Append(XRCID("ID_GETURL"), _("C&opy URL(s) to clipboard"), _("Copy the URLs of the selected items to clipboard."));
	menu.Append(XRCID("ID_GETURL_PASSWORD"), _("C&opy URL(s) with password to clipboard"), _("Copy the URLs of the selected items to clipboard, including password."));

	auto const protocol = m_state.GetSite().server.GetProtocol();
	bool const hasChmod = protocol == FTP || protocol == FTPS || protocol == FTPES || protocol == INSECURE_FTP || protocol == SFTP;

	if (hasChmod) {
	    menu.Append(XRCID("ID_CHMOD"), _("&File Attributes..."), _("Change the file permissions."));
	}

	const CServerPath& path = m_contextMenuItem ? GetPathFromItem(m_contextMenuItem) : CServerPath();
	if (!m_state.IsRemoteIdle() || path.empty()) {
		menu.Enable(XRCID("ID_DOWNLOAD"), false);
		menu.Enable(XRCID("ID_ADDTOQUEUE"), false);
		menu.Enable(XRCID("ID_MKDIR"), false);
		menu.Enable(XRCID("ID_MKDIR_CHGDIR"), false);
		menu.Enable(XRCID("ID_DELETE"), false);
		if (hasChmod) {
			menu.Enable(XRCID("ID_CHMOD"), false);
		}
		menu.Enable(XRCID("ID_RENAME"), false);
		menu.Enable(XRCID("ID_GETURL"), false);
	}
	else if (!path.HasParent()) {
		menu.Enable(XRCID("ID_RENAME"), false);
	}

	if (!m_state.GetLocalDir().IsWriteable()) {
		menu.Enable(XRCID("ID_DOWNLOAD"), false);
		menu.Enable(XRCID("ID_ADDTOQUEUE"), false);
	}

	menu.Delete(XRCID(wxGetKeyState(WXK_SHIFT) ? "ID_GETURL" : "ID_GETURL_PASSWORD"));

	PopupMenu(&menu);
}

void CRemoteTreeView::OnMenuChmod(wxCommandEvent&)
{
	if (!m_state.IsRemoteIdle()) {
		return;
	}

	if (!m_contextMenuItem) {
		return;
	}

	const CServerPath& path = GetPathFromItem(m_contextMenuItem);
	if (path.empty()) {
		return;
	}

	const bool hasParent = path.HasParent();

	// Get current permissions of directory
	std::wstring const name = GetItemText(m_contextMenuItem).ToStdWstring();
	bool cached = false;
	posix_chmod chmod;

	// Obviously item needs to have a parent directory...
	if (hasParent) {
		const CServerPath& parentPath = path.GetParent();
		CDirectoryListing listing;

		// ... and it needs to be cached
		cached = m_state.engine_->CacheLookup(parentPath, listing) == FZ_REPLY_OK;
		if (cached) {
			for (size_t i = 0; i < listing.size(); ++i) {
				auto const& entry = listing[i];
				if (entry.name != name) {
					continue;
				}

				std::optional<posix_permissions> p = parse_permissions(fz::to_utf8(*entry.permissions));
				if (p) {
					if (!chmod) {
						chmod.mask_ = posix_permissions::mask;
						chmod.perms_ = *p;
					}
				}
			}
		}
	}

	CChmodDialog chmodDialog;
	if (!chmodDialog.Create(this, 0, 1, name, chmod)) {
		return;
	}

	if (chmodDialog.ShowModal() != wxID_OK) {
		return;
	}

	// State may have changed while chmod dialog was shown
	if (!m_contextMenuItem || !m_state.IsRemoteConnected() || !m_state.IsRemoteIdle()) {
		return;
	}

	ChmodData chmodData = chmodDialog.GetChmodData();

	recursion_root root(hasParent ? path.GetParent() : path, !cached);
	if (cached) { // Implies hasParent
		// Change directory permissions
		if (chmodData.apply_dirs_) {
			if (!chmod) {
				chmod.perms_ = posix_permissions::all_read | posix_permissions::all_execute | posix_permissions::user_write;
			}
			std::wstring const newPerms = fz::to_wstring_from_utf8(to_octal(chmodData.Apply(chmod.perms_), true));
			m_state.m_pCommandQueue->ProcessCommand(new CChmodCommand(path.GetParent(), name, newPerms));
		}

		if (chmodData.recurse_) {
			// Start recursion
			root.add_dir_to_visit(path, std::wstring(), CLocalPath());
		}
	}
	else {
		if (hasParent) {
			root.add_dir_to_visit_restricted(path.GetParent(), name, chmodData.recurse_);
		}
		else {
			root.add_dir_to_visit_restricted(path, std::wstring(), chmodData.recurse_);
		}
	}

	if (!cached || chmodData.recurse_) {
		CRemoteRecursiveOperation* pRecursiveOperation = m_state.GetRemoteRecursiveOperation();
		pRecursiveOperation->AddRecursionRoot(std::move(root));

		pRecursiveOperation->SetChmodData(chmodData);

		CFilterManager filter;
		pRecursiveOperation->StartRecursiveOperation(recursive_operation::recursive_chmod, filter.GetActiveFilters());
	}
}

void CRemoteTreeView::OnMenuDownload(wxCommandEvent& event)
{
	CLocalPath localDir = m_state.GetLocalDir();
	if (!localDir.IsWriteable()) {
		wxBell();
		return;
	}

	if (!m_state.IsRemoteIdle()) {
		return;
	}

	if (!m_contextMenuItem) {
		return;
	}

	const CServerPath& path = GetPathFromItem(m_contextMenuItem);
	if (path.empty()) {
		return;
	}

	std::wstring const name = GetItemText(m_contextMenuItem).ToStdWstring();

	localDir.AddSegment(CQueueView::ReplaceInvalidCharacters(options_, name));

	recursion_root root(path, true);
	root.add_dir_to_visit(path, std::wstring(), localDir);
	CRemoteRecursiveOperation* pRecursiveOperation = m_state.GetRemoteRecursiveOperation();
	pRecursiveOperation->AddRecursionRoot(std::move(root));

	const bool addOnly = event.GetId() == XRCID("ID_ADDTOQUEUE");
	CFilterManager filter;
	pRecursiveOperation->StartRecursiveOperation(recursive_operation::recursive_transfer, filter.GetActiveFilters(), !addOnly);
}

void CRemoteTreeView::OnMenuDelete(wxCommandEvent&)
{
	if (!m_state.IsRemoteIdle()) {
		return;
	}

	if (!m_contextMenuItem) {
		return;
	}

	CServerPath const& pathToDelete = GetPathFromItem(m_contextMenuItem);
	if (pathToDelete.empty()) {
		return;
	}

	if (wxMessageBoxEx(_("Really delete all selected files and/or directories from the server?"), _("Confirmation needed"), wxICON_QUESTION | wxYES_NO, this) != wxYES) {
		return;
	}

	bool const hasParent = pathToDelete.HasParent();

	CFilterManager filter;
	if (CServer::ProtocolHasFeature(m_state.GetSite().server.GetProtocol(), ProtocolFeature::RecursiveDelete) && !filter.HasActiveRemoteFilters()) {
		if (hasParent) {
			std::wstring const name = GetItemText(m_contextMenuItem).ToStdWstring();
			m_state.m_pCommandQueue->ProcessCommand(new CRemoveDirCommand(pathToDelete.GetParent(), name));
		}
		else {
			m_state.m_pCommandQueue->ProcessCommand(new CRemoveDirCommand(pathToDelete, std::wstring()));
		}
	}
	else {

		CRemoteRecursiveOperation* pRecursiveOperation = m_state.GetRemoteRecursiveOperation();

		recursion_root root;
		CServerPath startDir;
		if (hasParent) {
			std::wstring const name = GetItemText(m_contextMenuItem).ToStdWstring();
			startDir = pathToDelete.GetParent();
			root = recursion_root(startDir, !hasParent);
			root.add_dir_to_visit(startDir, name);
		}
		else {
			startDir = pathToDelete;
			root = recursion_root(startDir, !hasParent);
			root.add_dir_to_visit(startDir, std::wstring());
		}
		pRecursiveOperation->AddRecursionRoot(std::move(root));

		CServerPath currentPath;
		const wxTreeItemId selected = GetSelection();
		if (selected) {
			currentPath = GetPathFromItem(selected);
		}
		if (!currentPath.empty() && (pathToDelete == currentPath || pathToDelete.IsParentOf(currentPath, false))) {
			currentPath = startDir;
			m_state.ChangeRemoteDir(startDir);
		}

		pRecursiveOperation->StartRecursiveOperation(recursive_operation::recursive_delete, filter.GetActiveFilters());
	}
}

void CRemoteTreeView::OnMenuRename(wxCommandEvent&)
{
	if (!m_state.IsRemoteIdle())
		return;

	if (!m_contextMenuItem)
		return;

	const CServerPath& path = GetPathFromItem(m_contextMenuItem);
	if (path.empty())
		return;

	if (!path.HasParent())
		return;

	EditLabel(m_contextMenuItem);
}

void CRemoteTreeView::OnBeginLabelEdit(wxTreeEvent& event)
{
	if (!m_state.IsRemoteIdle()) {
		event.Veto();
		return;
	}

	const CServerPath& path = GetPathFromItem(event.GetItem());
	if (path.empty()) {
		event.Veto();
		return;
	}

	if (!path.HasParent()) {
		event.Veto();
		return;
	}
}

void CRemoteTreeView::OnEndLabelEdit(wxTreeEvent& event)
{
	if (event.IsEditCancelled()) {
		event.Veto();
		return;
	}

	if (!m_state.IsRemoteIdle()) {
		event.Veto();
		return;
	}

	CItemData* const pData = (CItemData*)GetItemData(event.GetItem());
	if (pData) {
		event.Veto();
		return;
	}

	CServerPath old_path = GetPathFromItem(event.GetItem());
	CServerPath parent = old_path.GetParent();

	std::wstring const oldName = GetItemText(event.GetItem()).ToStdWstring();
	std::wstring const newName = event.GetLabel().ToStdWstring();
	if (oldName == newName) {
		event.Veto();
		return;
	}

	m_state.m_pCommandQueue->ProcessCommand(new CRenameCommand(parent, oldName, parent, newName));
	m_state.ChangeRemoteDir(parent);

	CServerPath currentPath;
	const wxTreeItemId selected = GetSelection();
	if (selected) {
		currentPath = GetPathFromItem(selected);
	}
	if (currentPath.empty()) {
		return;
	}

	if (currentPath == old_path || currentPath.IsSubdirOf(old_path, false)) {
		// Previously selected path was below renamed one, list the new one

		std::list<wxString> subdirs;
		while (currentPath != old_path) {
			if (!currentPath.HasParent()) {
				// Abort just in case
				return;
			}
			subdirs.push_front(currentPath.GetLastSegment());
			currentPath = currentPath.GetParent();
		}
		currentPath = parent;
		currentPath.AddSegment(newName);
		for (std::list<wxString>::const_iterator iter = subdirs.begin(); iter != subdirs.end(); ++iter) {
			currentPath.AddSegment(iter->ToStdWstring());
		}
		m_state.ChangeRemoteDir(currentPath);
	}
	else if (currentPath != parent) {
		m_state.ChangeRemoteDir(currentPath);
	}
}


// Create a new Directory
void CRemoteTreeView::OnMkdir(wxCommandEvent&)
{
	CServerPath newpath = MenuMkdir();

	CServerPath listed;
	if (newpath.HasParent())
	{
		listed = newpath.GetParent();
		m_state.ChangeRemoteDir(listed);
	}

	CServerPath currentPath;
	const wxTreeItemId selected = GetSelection();
	if (selected)
		currentPath = GetPathFromItem(selected);
	if (!currentPath.empty() && currentPath != listed)
		m_state.ChangeRemoteDir(currentPath);

}

// Create a new Directory and enter the new Directory
void CRemoteTreeView::OnMenuMkdirChgDir(wxCommandEvent&)
{
	CServerPath newpath = MenuMkdir();
	if (!newpath.empty()) {
		m_state.ChangeRemoteDir(newpath);
	}
}

// Help-Function to create a new Directory
// Returns the name of the new directory
CServerPath CRemoteTreeView::MenuMkdir()
{
	if (!m_state.IsRemoteIdle()) {
		return CServerPath();
	}

	if (!m_contextMenuItem) {
		return CServerPath();
	}

	const CServerPath& path = GetPathFromItem(m_contextMenuItem);
	if (path.empty()) {
		return CServerPath();
	}

	CInputDialog dlg;
	if (!dlg.Create(this, _("Create directory"), _("Please enter the name of the directory which should be created:"))) {
		return CServerPath();
	}

	CServerPath newPath = path;

	// Append a long segment which does (most likely) not exist in the path and
	// replace it with "New directory" later. This way we get the exact position of
	// "New directory" and can preselect it in the dialog.
	std::wstring tmpName = _T("25CF809E56B343b5A12D1F0466E3B37A49A9087FDCF8412AA9AF8D1E849D01CF");
	if (newPath.AddSegment(tmpName)) {
		wxString pathName = newPath.GetPath();
		int pos = pathName.Find(tmpName);
		wxASSERT(pos != -1);
		wxString newName = _("New directory");
		pathName.Replace(tmpName, newName);
		dlg.SetValue(pathName);
		dlg.SelectText(pos, pos + newName.Length());
	}

	if (dlg.ShowModal() != wxID_OK) {
		return CServerPath();
	}

	newPath = path;
	if (!newPath.ChangePath(dlg.GetValue().ToStdWstring())) {
		wxBell();
		return CServerPath();
	}

	transfer_flags const flags = GetMkdirFlags(m_state.GetSite().server, options_, newPath);
	m_state.m_pCommandQueue->ProcessCommand(new CMkdirCommand(newPath, flags));

	return newPath;
}

bool CRemoteTreeView::ListExpand(wxTreeItemId item)
{
	const CServerPath path = GetPathFromItem(item);
	wxASSERT(!path.empty());
	if (path.empty()) {
		return false;
	}

	CDirectoryListing listing;
	if (m_state.engine_->CacheLookup(path, listing) == FZ_REPLY_OK) {
		RefreshItem(item, listing, false);
	}
	else {
		SetItemImages(item, true);

		wxTreeItemId child = GetLastChild(item);
		if (!child || GetItemText(child).empty()) {
			return false;
		}
	}

	return true;
}

void CRemoteTreeView::OnChar(wxKeyEvent& event)
{
	m_contextMenuItem = GetSelection();

	wxCommandEvent cmdEvt;
	if (event.GetKeyCode() == WXK_F2)
		OnMenuRename(cmdEvt);
	else if (event.GetKeyCode() == WXK_DELETE || event.GetKeyCode() == WXK_NUMPAD_DELETE)
		OnMenuDelete(cmdEvt);
	else
		event.Skip();
}

struct _parents
{
	wxTreeItemId item;
	CServerPath path;
};

void CRemoteTreeView::ApplyFilters(bool resort)
{
	std::list<_parents> parents;

	const wxTreeItemId root = GetRootItem();
	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = GetFirstChild(root, cookie); child; child = GetNextSibling(child)) {
		CServerPath path = GetPathFromItem(child);
		if (path.empty()) {
			continue;
		}

		_parents dir;
		dir.item = child;
		dir.path = path;
		parents.push_back(dir);
	}

	CFilterManager filter;
	while (!parents.empty()) {
		_parents parent = parents.back();
		parents.pop_back();

		if (resort) {
			SortChildren(parent.item);
		}

		CDirectoryListing listing;
		if (m_state.engine_->CacheLookup(parent.path, listing) == FZ_REPLY_OK) {
			RefreshItem(parent.item, listing, false);
		}
		else if (filter.HasActiveFilters()) {
			for (wxTreeItemId child = GetFirstChild(parent.item, cookie); child; child = GetNextSibling(child)) {
				CServerPath path = GetPathFromItem(child);
				if (path.empty()) {
					continue;
				}

				if (filter.FilenameFiltered(GetItemText(child).ToStdWstring(), path.GetPath(), true, -1, false, 0, fz::datetime())) {
					wxTreeItemId sel = GetSelection();
					while (sel && sel != child) {
						sel = GetItemParent(sel);
					}
					if (!sel) {
						Delete(child);
						continue;
					}
				}

				_parents dir;
				dir.item = child;
				dir.path = path;
				parents.push_back(dir);
			}

			// The stuff below has already been done above in this one case
			continue;
		}
		for (wxTreeItemId child = GetFirstChild(parent.item, cookie); child; child = GetNextSibling(child)) {
			CServerPath path = GetPathFromItem(child);
			if (path.empty()) {
				continue;
			}

			_parents dir;
			dir.item = child;
			dir.path = path;
			parents.push_back(dir);
		}
	}
}

void CRemoteTreeView::OnMenuGeturl(wxCommandEvent& event)
{
	if (!m_contextMenuItem) {
		return;
	}

	CServerPath const& path = GetPathFromItem(m_contextMenuItem);
	if (path.empty()) {
		wxBell();
		return;
	}

	Site const& site = m_state.GetSite();
	if (!site) {
		wxBell();
		return;
	}

	if (!wxTheClipboard->Open()) {
		wxMessageBoxEx(_("Could not open clipboard"), _("Could not copy URLs"), wxICON_EXCLAMATION);
		return;
	}

	std::wstring url = site.Format((event.GetId() == XRCID("ID_GETURL_PASSWORD")) ? ServerFormat::url_with_password : ServerFormat::url);

	std::wstring const pathPart = fz::percent_encode_w(path.GetPath(), true);
	if (!pathPart.empty() && pathPart[0] != '/') {
		url += '/';
	}
	url += pathPart;

	wxTheClipboard->SetData(new wxURLDataObject(url));

	wxTheClipboard->Flush();
	wxTheClipboard->Close();
}

void CRemoteTreeView::UpdateSortMode()
{
	NameSortMode sortMode;
	switch (options_.get_int(OPTION_FILELIST_NAMESORT))
	{
	case 0:
	default:
		sortMode = NameSortMode::case_insensitive;
		break;
	case 1:
		sortMode = NameSortMode::case_sensitive;
		break;
	case 2:
		sortMode = NameSortMode::natural;
		break;
	}
	sortFunction_ = CFileListCtrlSortBase::GetCmpFunction(sortMode);
	Resort();
}

void CRemoteTreeView::OnOptionsChanged(watched_options const& options)
{
	if (options.test(OPTION_FILELIST_NAMESORT)) {
		UpdateSortMode();
		ApplyFilters(true);
	}
}
