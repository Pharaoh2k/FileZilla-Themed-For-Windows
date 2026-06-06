#include "filezilla.h"

#include "dndobjects.h"
#include "dragdropmanager.h"
#include "drop_target_ex.h"
#include "filezillaapp.h"
#include "filter_manager.h"
#include "file_utils.h"
#include "graphics.h"
#include "inputdialog.h"
#include "LocalTreeView.h"
#include "Options.h"
#include "queue.h"
#include "themeprovider.h"

#include <libfilezilla/local_filesys.hpp>

#include <wx/menu.h>

#ifdef __WXMSW__
#include <shlobj.h>
#include <dbt.h>
#include "volume_enumerator.h"
#endif

#ifdef USE_MAC_SANDBOX
#include "osx_sandbox_userdirs.h"
#endif

#include <algorithm>

using namespace std::literals;

class CTreeItemData : public wxTreeItemData
{
public:
	CTreeItemData(std::wstring const& known_subdir) : m_known_subdir(known_subdir) {}
	std::wstring m_known_subdir;
};

class CLocalTreeViewDropTarget final : public CFileDropTarget<wxTreeCtrlEx>
{
public:
	CLocalTreeViewDropTarget(CLocalTreeView* pLocalTreeView, login_manager& lim)
		: CFileDropTarget(pLocalTreeView, pLocalTreeView->options_, lim)
		, m_pLocalTreeView(pLocalTreeView)
	{
	}

	std::wstring GetDirFromItem(wxTreeItemId const& item)
	{
		std::wstring const dir = m_pLocalTreeView->GetDirFromItem(item);

#ifdef __WXMSW__
		if (dir == L"/") {
			return std::wstring();
		}
#endif

		return dir;
	}

	wxTreeItemId GetHit(const wxPoint& point)
	{
		int flags{};
		wxTreeItemId hit = m_pLocalTreeView->HitTest(point, flags);

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
		if (def == wxDragLink) {
			def = wxDragCopy;
		}

		wxTreeItemId hit = GetHit(wxPoint(x, y));
		if (!hit) {
			return wxDragNone;
		}

		const CLocalPath path(GetDirFromItem(hit));
		if (path.empty() || !path.IsWriteable()) {
			return wxDragNone;
		}

		if (!GetData()) {
			return wxDragError;
		}

		CDragDropManager* pDragDropManager = CDragDropManager::Get();
		if (pDragDropManager) {
			pDragDropManager->pDropTarget = m_pLocalTreeView;
		}

		auto const format = GetReceivedFormat();
		if (format == m_pFileDataObject->GetFormat()) {
			m_pLocalTreeView->m_state.HandleDroppedFiles(m_pFileDataObject, path, def == wxDragCopy);
		}
		else if (format == LocalDataObjectFormat()) {
			m_pLocalTreeView->m_state.HandleDroppedFiles(GetLocalDataObject(), path, def == wxDragCopy);
		}
		else {
			auto * obj = GetRemoteDataObject();
			if (obj->GetProcessId() != (int)wxGetProcessId()) {
				wxMessageBoxEx(_("Drag&drop between different instances of FileZilla has not been implemented yet."));
				return wxDragNone;
			}

			auto & state = m_pLocalTreeView->m_state;
			if (!state.GetSite() || obj->GetSite().server != state.GetSite().server) {
				wxMessageBoxEx(_("Drag&drop between different servers has not been implemented yet."));
				return wxDragNone;
			}

			if (!state.DownloadDroppedFiles(obj, path)) {
				return wxDragNone;
			}
		}

		return def;
	}

	virtual bool OnDrop(wxCoord x, wxCoord y)
	{
		if (!CFileDropTarget::OnDrop(x, y)) {
			return false;
		}

		m_pLocalTreeView->ClearDropHighlight();

		wxTreeItemId hit = GetHit(wxPoint(x, y));
		if (!hit) {
			return false;
		}

		std::wstring const dir = GetDirFromItem(hit);
		if (dir.empty() || !CLocalPath(dir).IsWriteable()) {
			return false;
		}

		return true;
	}

	wxTreeItemId DisplayDropHighlight(wxPoint const& point) override
	{
		wxTreeItemId hit = GetHit(point);
		if (!hit) {
			m_pLocalTreeView->ClearDropHighlight();
			return hit;
		}

		std::wstring dir = GetDirFromItem(hit);

		if (dir.empty()) {
			m_pLocalTreeView->ClearDropHighlight();
			return wxTreeItemId();
		}
		else {
			m_pLocalTreeView->DisplayDropHighlight(hit);
		}

		return hit;
	}

	virtual wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = CScrollableDropTarget::OnDragOver(x, y, def);

		if (def == wxDragError ||
			def == wxDragNone ||
			def == wxDragCancel)
		{
			m_pLocalTreeView->ClearDropHighlight();
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
		m_pLocalTreeView->ClearDropHighlight();
	}

	virtual wxDragResult OnEnter(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = CScrollableDropTarget::OnEnter(x, y, def);
		return OnDragOver(x, y, def);
	}

protected:
	CLocalTreeView *m_pLocalTreeView{};
};

BEGIN_EVENT_TABLE(CLocalTreeView, wxTreeCtrlEx)
EVT_TREE_ITEM_EXPANDING(wxID_ANY, CLocalTreeView::OnItemExpanding)
#ifdef __WXMSW__
EVT_TREE_SEL_CHANGING(wxID_ANY, CLocalTreeView::OnSelectionChanging)
#endif
EVT_TREE_SEL_CHANGED(wxID_ANY, CLocalTreeView::OnSelectionChanged)
EVT_TREE_BEGIN_DRAG(wxID_ANY, CLocalTreeView::OnBeginDrag)
#ifdef __WXMSW__
EVT_COMMAND(-1, fzEVT_VOLUMESENUMERATED, CLocalTreeView::OnVolumesEnumerated)
EVT_COMMAND(-1, fzEVT_VOLUMEENUMERATED, CLocalTreeView::OnVolumesEnumerated)
#endif //__WXMSW__
EVT_TREE_ITEM_MENU(wxID_ANY, CLocalTreeView::OnContextMenu)
EVT_MENU(XRCID("ID_UPLOAD"), CLocalTreeView::OnMenuUpload)
EVT_MENU(XRCID("ID_ADDTOQUEUE"), CLocalTreeView::OnMenuUpload)
EVT_MENU(XRCID("ID_DELETE"), CLocalTreeView::OnMenuDelete)
EVT_MENU(XRCID("ID_RENAME"), CLocalTreeView::OnMenuRename)
EVT_MENU(XRCID("ID_MKDIR"), CLocalTreeView::OnMenuMkdir)
EVT_MENU(XRCID("ID_MKDIR_CHGDIR"), CLocalTreeView::OnMenuMkdirChgDir)
EVT_TREE_BEGIN_LABEL_EDIT(wxID_ANY, CLocalTreeView::OnBeginLabelEdit)
EVT_TREE_END_LABEL_EDIT(wxID_ANY, CLocalTreeView::OnEndLabelEdit)
EVT_CHAR(CLocalTreeView::OnChar)
EVT_MENU(XRCID("ID_OPEN"), CLocalTreeView::OnMenuOpen)
END_EVENT_TABLE()

CLocalTreeView::CLocalTreeView(wxWindow* parent, wxWindowID id, CState& state, CQueueView *pQueueView, COptionsBase & options, login_manager & lim)
	: wxTreeCtrlEx(parent, id, wxDefaultPosition, wxDefaultSize, DEFAULT_TREE_STYLE | wxTAB_TRAVERSAL | wxTR_EDIT_LABELS | wxNO_BORDER)
	, CSystemImageList(CThemeProvider::GetIconSize(iconSizeSmall).x)
	, CStateEventHandler(state)
	, COptionChangeEventHandler(this)
	, m_pQueueView(pQueueView)
	, options_(options)
{
	wxGetApp().AddStartupProfileRecord("CLocalTreeView::CLocalTreeView"sv);
#ifdef __WXMAC__
	SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
#endif

	state.RegisterHandler(this, STATECHANGE_LOCAL_DIR);
	state.RegisterHandler(this, STATECHANGE_APPLYFILTER);
	state.RegisterHandler(this, STATECHANGE_SERVER);

	SetImageList(GetSystemImageList());

	UpdateSortMode();
	options_.watch(OPTION_FILELIST_NAMESORT, this);

#ifdef __WXMSW__
	m_pVolumeEnumeratorThread = 0;

	CreateRoot();
#else
	wxTreeItemId root = AddRoot(_T("/"));
	SetItemImage(root, GetIconIndex(iconType::dir), wxTreeItemIcon_Normal);
	SetItemImage(root, GetIconIndex(iconType::opened_dir), wxTreeItemIcon_Selected);
	SetItemImage(root, GetIconIndex(iconType::dir), wxTreeItemIcon_Expanded);
	SetItemImage(root, GetIconIndex(iconType::opened_dir), wxTreeItemIcon_SelectedExpanded);

#if USE_MAC_SANDBOX
	auto userDirs = OSXSandboxUserdirs::Get().GetDirs();
	for (auto const& dir : userDirs) {
		SetDir(dir);
	}
#endif

	SetDir(_T("/"));
#endif

	SetDropTarget(new CLocalTreeViewDropTarget(this, lim));

	m_windowTinter = std::make_unique<CWindowTinter>(*this);
}

CLocalTreeView::~CLocalTreeView()
{
	options_.unwatch_all(this);
#ifdef __WXMSW__
	delete m_pVolumeEnumeratorThread;
#endif
}

void CLocalTreeView::SetDir(wxString const& localDir)
{
	if (m_currentDir == localDir && !changing_dir_) {
		RefreshListing();
		return;
	}

	if (localDir.Left(2) == _T("\\\\")) {
		// TODO: UNC path, don't display it yet
		m_currentDir.clear();
		SafeSelectItem(wxTreeItemId());
		return;
	}
	m_currentDir = localDir;

#ifdef __WXMSW__
	if (localDir == _T("\\")) {
		SafeSelectItem(m_drives);
		return;
	}
#endif

	wxString subDirs = localDir;
	wxTreeItemId parent = GetNearestParent(subDirs);
	if (!parent) {
		SafeSelectItem(wxTreeItemId());
		return;
	}

	if (subDirs.empty()) {
		SafeSelectItem(parent);
		return;
	}
	wxTreeItemId item = MakeSubdirs(parent, localDir.Left(localDir.Length() - subDirs.Length()).ToStdWstring(), subDirs);
	if (!item) {
		return;
	}

	SafeSelectItem(item);
}

wxTreeItemId CLocalTreeView::GetNearestParent(wxString& localDir)
{
	const wxString separator = wxFileName::GetPathSeparator();
#ifdef __WXMSW__
	int pos = localDir.Find(separator);
	if (pos == -1) {
		return wxTreeItemId();
	}

	wxString drive = localDir.Left(pos);
	localDir = localDir.Mid(pos + 1);

	wxTreeItemIdValue value;
	wxTreeItemId root = GetFirstChild(m_drives, value);
	while (root) {
		if (!GetItemText(root).Left(drive.Length()).CmpNoCase(drive)) {
			break;
		}

		root = GetNextSibling(root);
	}
	if (!root) {
		if (drive[1] == ':') {
			return AddDrive(drive[0]);
		}
		return wxTreeItemId();
	}
#else
	if (localDir[0] == '/') {
		localDir = localDir.Mid(1);
	}
	wxTreeItemId root = GetRootItem();
#endif

	while (!localDir.empty()) {
		wxString subDir;
		int pos2 = localDir.Find(separator);
		if (pos2 == -1) {
			subDir = localDir;
		}
		else {
			subDir = localDir.Left(pos2);
		}

		wxTreeItemId child = GetSubdir(root, subDir);
		if (!child) {
			return root;
		}

		if (!pos2) {
			return child;
		}

		root = child;
		localDir = localDir.Mid(pos2 + 1);
	}

	return root;
}

wxTreeItemId CLocalTreeView::GetSubdir(wxTreeItemId parent, wxString const& subDir)
{
	wxTreeItemIdValue value;
	wxTreeItemId child = GetFirstChild(parent, value);
	while (child) {
#ifdef __WXMSW__
		if (!GetItemText(child).CmpNoCase(subDir))
#else
		if (GetItemText(child) == subDir)
#endif
		{
			return child;
		}

		child = GetNextSibling(child);
	}
	return wxTreeItemId();
}

#ifdef __WXMSW__

bool CLocalTreeView::DisplayDrives(wxTreeItemId parent)
{
	wxGetApp().AddStartupProfileRecord("CLocalTreeView::DisplayDrives"sv);

	std::vector<std::wstring> drives = CVolumeDescriptionEnumeratorThread::GetDrives();

	m_pVolumeEnumeratorThread = new CVolumeDescriptionEnumeratorThread(this, m_state.pool_);
	if (m_pVolumeEnumeratorThread->Failed()) {
		delete m_pVolumeEnumeratorThread;
		m_pVolumeEnumeratorThread = 0;
	}

	wxTreeItemId last;
	for (auto it = drives.begin(); it != drives.end(); ++it) {
		wxString drive = *it;
		if (drive.Right(1) == _T("\\")) {
			drive = drive.RemoveLast();
		}

		last = InsertItem(parent, last, drive, GetIconIndex(iconType::dir, _T(""), false));
		AppendItem(last, _T(""));
	}
	SortChildren(parent);

	wxGetApp().AddStartupProfileRecord("CLocalTreeView::DisplayDrives adding drives done"sv);

	return true;
}

#endif

void CLocalTreeView::DisplayDir(wxTreeItemId parent, std::wstring const& dirname, std::wstring const& knownSubdir)
{
	fz::local_filesys local_filesys;

	if (!local_filesys.begin_find_files(fz::to_native(dirname), true)) {
		if (!knownSubdir.empty()) {
			wxTreeItemId item = GetSubdir(parent, knownSubdir);
			if (item != wxTreeItemId()) {
				return;
			}

			std::wstring const fullName = dirname + knownSubdir;
			item = AppendItem(parent, knownSubdir, GetIconIndex(iconType::dir, fullName),
#ifdef __WXMSW__
					-1
#else
					GetIconIndex(iconType::opened_dir, fullName)
#endif
				);
			CheckSubdirStatus(item, fullName);
		}
		else {
			++m_setSelection;
			DeleteChildren(parent);
			--m_setSelection;
		}
		return;
	}

	wxASSERT(parent);
	++m_setSelection;
	DeleteChildren(parent);
	--m_setSelection;

	CFilterManager filter;

	bool matchedKnown = false;

	fz::native_string file;
	bool was_link{};
	int attributes{};
	fz::local_filesys::type t{};
	static int64_t const size(-1);
	fz::datetime date;

	wxTreeItemId last;
	while (local_filesys.get_next_file(file, was_link, t, 0, &date, &attributes)) {
		std::wstring wfile = fz::to_wstring(file);
		if (file.empty() || wfile.empty()) {
			wxGetApp().DisplayEncodingWarning();
			continue;
		}

		std::wstring fullName = dirname + wfile;
#ifdef __WXMSW__
		if (fz::stricmp(wfile, knownSubdir))
#else
		if (wfile != knownSubdir)
#endif
		{
			if (filter.FilenameFiltered(wfile, dirname, true, size, true, attributes, date)) {
				continue;
			}
		}
		else {
			matchedKnown = true;
		}

		last = InsertItem(parent, last, wfile, GetIconIndex(iconType::dir, fullName, true, was_link),
#ifdef __WXMSW__
				-1
#else
				GetIconIndex(iconType::opened_dir, fullName, true, was_link)
#endif
			);
#if FZ_WINDOWS
		if (was_link) {
			TreeView_SetItemState(GetHWND(), reinterpret_cast<HTREEITEM>(last.GetID()), INDEXTOOVERLAYMASK(GetLinkOverlayIndex()), TVIS_OVERLAYMASK);
		}
#endif

		CheckSubdirStatus(last, fullName);
	}

	if (!matchedKnown && !knownSubdir.empty()) {
		std::wstring const& fullName = dirname + knownSubdir;
		wxTreeItemId item = AppendItem(parent, knownSubdir, GetIconIndex(iconType::dir, fullName),
#ifdef __WXMSW__
				-1
#else
				GetIconIndex(iconType::opened_dir, fullName)
#endif
			);

		CheckSubdirStatus(item, fullName);
	}

	SortChildren(parent);
}

std::wstring CLocalTreeView::HasSubdir(std::wstring const& dirname)
{
	wxLogNull nullLog;

	CFilterManager filter;

	fz::local_filesys local_filesys;
	if (!local_filesys.begin_find_files(fz::to_native(dirname), true)) {
		return std::wstring();
	}

	fz::native_string file;
	bool was_link{};
	int attributes{};
	fz::local_filesys::type t{};
	static int64_t const size(-1);
	fz::datetime date;
	while (local_filesys.get_next_file(file, was_link, t, 0, &date, &attributes)) {

		std::wstring wfile = fz::to_wstring(file);
		if (file.empty() || wfile.empty()) {
			wxGetApp().DisplayEncodingWarning();
			continue;
		}

		if (filter.FilenameFiltered(wfile, dirname, true, size, true, attributes, date)) {
			continue;
		}

		return wfile;
	}

	return std::wstring();
}

wxTreeItemId CLocalTreeView::MakeSubdirs(wxTreeItemId parent, std::wstring dirname, wxString subDir)
{
	std::wstring segment;
	while (!subDir.empty()) {
		int pos = subDir.Find(fz::local_filesys::path_separator);
		if (!pos) {
			subDir = subDir.Mid(1);
			continue;
		}
		else if (pos == -1) {
			segment = subDir.ToStdWstring();
			subDir.clear();
		}
		else {
			segment = subDir.Left(pos).ToStdWstring();
			subDir = subDir.Mid(pos + 1);
		}

		DisplayDir(parent, dirname, segment);

		wxTreeItemId item = GetSubdir(parent, segment);
		if (!item) {
			return wxTreeItemId();
		}

		parent = item;
		dirname += segment;
		dirname += fz::local_filesys::path_separator;
	}

	// Not needed, stays unexpanded by default
	// DisplayDir(parent, dirname);
	return parent;
}

void CLocalTreeView::OnItemExpanding(wxTreeEvent& event)
{
	wxTreeItemId item = event.GetItem();

	wxTreeItemIdValue value;
	wxTreeItemId child = GetFirstChild(item, value);
	if (child && GetItemText(child).empty()) {
		wxCHECK_RET(!m_setSelection, "OnItemExpanding called on an item with empty child during item selection of one of its children.");
		DisplayDir(item, GetDirFromItem(item));
	}
}

std::wstring CLocalTreeView::GetDirFromItem(wxTreeItemId item)
{
	wchar_t const separator = fz::local_filesys::path_separator;
	std::wstring dir;
	while (item) {
#ifdef __WXMSW__
		if (item == m_desktop) {
			wchar_t* out{};
			if (SHGetKnownFolderPath(FOLDERID_Desktop, 0, 0, &out) != S_OK) {
				wxMessageBoxEx(_("Failed to get desktop path"));
				return L"/";
			}
			dir = out;
			CoTaskMemFree(out);

			if (dir.empty() || dir.back() != separator) {
				dir += separator;
			}
			return dir;
		}
		else if (item == m_documents) {
			wchar_t* out{};
			if (SHGetKnownFolderPath(FOLDERID_Documents, 0, 0, &out) != S_OK) {
				wxMessageBoxEx(_("Failed to get 'My Documents' path"));
				return L"/";
			}
			dir = out;
			CoTaskMemFree(out);

			if (dir.empty() || dir.back() != separator) {
				dir += separator;
			}
			return dir;
		}
		else if (item == m_drives) {
			return L"/";
		}
		else if (GetItemParent(item) == m_drives) {
			std::wstring text = GetItemText(item).ToStdWstring();
			size_t pos = text.find(' ');
			if (pos == std::wstring::npos) {
				return text + separator + dir;
			}
			else {
				return text.substr(0, pos) + separator + dir;
			}
		}
		else
#endif
		{
			if (item == GetRootItem()) {
				return L"/" + dir;
			}
		}

		dir = GetItemText(item).ToStdWstring() + separator + dir;

		item = GetItemParent(item);
	}

	return std::wstring(1, separator);
}

void CLocalTreeView::UpdateSortMode()
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
}

namespace {
struct t_dir final
{
	std::wstring dir;
	wxTreeItemId item;
};
}

void CLocalTreeView::RefreshListing()
{
	wxLogNull nullLog;

	wchar_t const separator = fz::local_filesys::path_separator;

	std::list<t_dir> dirsToCheck;

#ifdef __WXMSW__
	if (!m_drives) {
		return;
	}
	int prevErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS);

	wxTreeItemIdValue tmp;
	for (auto child = GetFirstChild(m_drives, tmp); child; child = GetNextSibling(child)) {
		if (IsExpanded(child)) {
			std::wstring drive = GetItemText(child).ToStdWstring();
			size_t pos = drive.find(' ');
			if (pos != std::wstring::npos) {
				drive = drive.substr(0, pos);
			}

			t_dir dir;
			dir.dir = drive + separator;
			dir.item = child;
			dirsToCheck.push_back(dir);
		}
	}
#else
	t_dir root_dir;
	root_dir.dir = separator;
	root_dir.item = GetRootItem();
	if (!root_dir.item) {
		return;
	}
	dirsToCheck.push_back(root_dir);
#endif

	CFilterManager filter;

	while (!dirsToCheck.empty()) {
		t_dir dir = dirsToCheck.front();
		dirsToCheck.pop_front();

		// Step 1: Check if directory exists
		fz::local_filesys local_filesys;
		if (!local_filesys.begin_find_files(fz::to_native(dir.dir), true)) {
			// Dir does exist (listed in parent) but may not be accessible.
			// Recurse into children anyhow, they might be accessible again.
			wxTreeItemIdValue value;
			for (auto child = GetFirstChild(dir.item, value); child; child = GetNextSibling(child)) {
				t_dir subdir;
				subdir.dir = dir.dir + GetItemText(child).ToStdWstring() + separator;
				subdir.item = child;
				dirsToCheck.push_back(subdir);
			}
			continue;
		}

		// Step 2: Enumerate subdirectories on disk and sort them
		std::vector<std::pair<std::wstring, bool>> dirs;

		fz::native_string file;
		static int64_t const size(-1);
		bool was_link{};
		fz::local_filesys::type t{};
		int attributes{};
		fz::datetime date;
		while (local_filesys.get_next_file(file, was_link, t, 0, &date, &attributes)) {
			std::wstring wfile = fz::to_wstring(file);
			if (file.empty() || wfile.empty()) {
				wxGetApp().DisplayEncodingWarning();
				continue;
			}

			if (filter.FilenameFiltered(wfile, dir.dir, true, size, true, attributes, date)) {
				continue;
			}

			dirs.emplace_back(std::move(wfile), was_link);
		}
		std::sort(dirs.begin(), dirs.end(), [&](auto const& lhs, auto const& rhs) { return sortFunction_(lhs.first, rhs.first) < 0; });

		// Step 3: Merge list of subdirectories with subtree.
		std::vector<wxTreeItemId> toDelete;

		wxTreeItemIdValue unused;
		wxTreeItemId child = GetFirstChild(dir.item, unused);

		wxTreeItemId firstInserted{};
		wxTreeItemId last = GetLastChild(dir.item);
		auto iter = dirs.begin();
		while (child || iter != dirs.end()) {
			int cmp;
			if (child && iter != dirs.end()) {
				wxString const& childName = GetItemText(child);
				cmp = sortFunction_(iter->first, std::wstring_view(childName.data(), childName.size()));
			}
			else if (child) {
				cmp = 1;
			}
			else {
				cmp = -1;
			}

			if (!cmp) {
				// Found item with same name. Mark it for further processing
				if (!IsExpanded(child)) {
					std::wstring path = dir.dir + iter->first + separator;
					if (!CheckSubdirStatus(child, path)) {
						t_dir subdir;
						subdir.dir = path;
						subdir.item = child;
						dirsToCheck.push_front(subdir);
					}
				}
				else {
					t_dir subdir;
					subdir.dir = dir.dir + iter->first + separator;
					subdir.item = child;
					dirsToCheck.push_front(subdir);
				}
				child = GetNextSibling(child);
				if (child == firstInserted) {
					child = wxTreeItemId{};
				}
				++iter;
			}
			else if (cmp > 0) {
				// Subdirectory currently in tree no longer exists.
				// Delete child from tree, unless current selection
				// is in the subtree.
				wxTreeItemId sel = GetSelection();
				while (sel && sel != child) {
					sel = GetItemParent(sel);
				}
				if (!sel) {
					toDelete.push_back(child);
				}
				child = GetNextSibling(child);
				if (child == firstInserted) {
					child = wxTreeItemId{};
				}
			}
			else if (cmp < 0) {
				// New subdirectory, add treeitem
				std::wstring fullname = dir.dir + iter->first + separator;
				last = InsertItem(dir.item, last, iter->first, GetIconIndex(iconType::dir, fullname, true, iter->second),
#ifdef __WXMSW__
						-1
#else
						GetIconIndex(iconType::opened_dir, fullname, true, iter->second)
#endif
					);

#if FZ_WINDOWS
				if (was_link) {
					TreeView_SetItemState(GetHWND(), reinterpret_cast<HTREEITEM>(last.GetID()), INDEXTOOVERLAYMASK(GetLinkOverlayIndex()), TVIS_OVERLAYMASK);
				}
#endif

				if (!firstInserted) {
					firstInserted = last;
				}

				CheckSubdirStatus(last, fullname);
				++iter;
			}
		}
		for (auto it = toDelete.rbegin(); it != toDelete.rend(); ++it) {
			Delete(*it);
		}
		if (firstInserted) {
			SortChildren(dir.item);
		}
	}

#ifdef __WXMSW__
	SetErrorMode(prevErrorMode);
#endif
}

void CLocalTreeView::OnSelectionChanged(wxTreeEvent& event)
{
	if (m_setSelection) {
		event.Skip();
		return;
	}

	wxTreeItemId item = event.GetItem();
	if (!item) {
		return;
	}

	std::wstring dir = GetDirFromItem(item);

	std::wstring error;
	changing_dir_ = true;
	if (!m_state.SetLocalDir(dir, &error)) {
		if (!error.empty()) {
			wxMessageBoxEx(error, _("Failed to change directory"), wxICON_INFORMATION);
		}
		else {
			wxBell();
		}
		++m_setSelection;
		SelectItem(event.GetOldItem());
		--m_setSelection;
	}
	changing_dir_ = false;
}

void CLocalTreeView::OnStateChange(t_statechange_notifications notification, std::wstring const&, const void*)
{
	if (notification == STATECHANGE_LOCAL_DIR) {
		SetDir(m_state.GetLocalDir().GetPath());
	}
	else if (notification == STATECHANGE_SERVER) {
		m_windowTinter->SetBackgroundTint(m_state.GetSite().m_colour);
	}
	else {
		wxASSERT(notification == STATECHANGE_APPLYFILTER);
		RefreshListing();
	}
}

void CLocalTreeView::OnBeginDrag(wxTreeEvent& event)
{
	if (options_.get_int(OPTION_DND_DISABLED) != 0) {
		return;
	}

	wxTreeItemId item = event.GetItem();
	if (!item) {
		return;
	}

#ifdef __WXMSW__
	if (item == m_drives || item == m_desktop || item == m_documents) {
		return;
	}
#endif

	std::wstring dir = GetDirFromItem(item);
	if (dir == L"/") {
		return;
	}

#ifdef __WXMSW__
	if (!dir.empty() && dir.back() == '\\') {
		dir.pop_back();
	}
#endif
	if (!dir.empty() && dir.back() == '/') {
		dir.pop_back();
	}

#ifdef __WXMSW__
	if (!dir.empty() && dir.back() == ':') {
		return;
	}
#endif

#ifdef __WXMAC__
	// Don't use wxFileDataObject on Mac, crashes on Mojave, wx bug #18232
	CLocalDataObject obj;
#else
	wxFileDataObject obj;
#endif

	CDragDropManager* pDragDropManager = CDragDropManager::Init();
	pDragDropManager->pDragSource = this;

	obj.AddFile(dir);
	pDragDropManager->dragDataObject = &obj;

	DropSource source(this);
	source.SetData(obj);
	int res = source.DoFileDragDrop(wxDrag_AllowMove);

	bool handled_internally = pDragDropManager->pDropTarget != 0;

	pDragDropManager->Release();

	if (!handled_internally && (res == wxDragCopy || res == wxDragMove)) {
		// We only need to refresh local side if the operation got handled
		// externally, the internal handlers do this for us already
		m_state.RefreshLocal();
	}

#ifdef __WXMAC__
	if (!source.m_OutDir.IsEmpty()) {
		CLocalPath target(source.m_OutDir.ToStdWstring());
		m_state.HandleDroppedFiles(&obj, target, true);
	}
#endif
}

#ifdef __WXMSW__

wxString CLocalTreeView::GetSpecialFolder(int folder, int &iconIndex, int &openIconIndex)
{
	wxString name;

	LPITEMIDLIST list{};
	if (SHGetSpecialFolderLocation((HWND)GetHandle(), folder, &list) == S_OK) {
		SHFILEINFO shFinfo{};
		if (SHGetFileInfo((LPCTSTR)list, 0, &shFinfo, sizeof(shFinfo), SHGFI_PIDL | SHGFI_ICON | SHGFI_SMALLICON) != 0) {
			if (shFinfo.hIcon) {
				DestroyIcon(shFinfo.hIcon);
			}
			iconIndex = shFinfo.iIcon;
		}

		if (SHGetFileInfo((LPCTSTR)list, 0, &shFinfo, sizeof(shFinfo), SHGFI_PIDL | SHGFI_ICON | SHGFI_SMALLICON | SHGFI_OPENICON | SHGFI_DISPLAYNAME) != 0) {
			if (shFinfo.hIcon) {
				DestroyIcon(shFinfo.hIcon);
			}
			openIconIndex = shFinfo.iIcon;
			name = shFinfo.szDisplayName;
		}

		CoTaskMemFree(list);
	}

	return name;
}

bool CLocalTreeView::CreateRoot()
{
	int iconIndex, openIconIndex;
	wxString name = GetSpecialFolder(CSIDL_DESKTOP, iconIndex, openIconIndex);
	if (name.empty()) {
		name = _("Desktop");
		iconIndex = openIconIndex = -1;
	}

	m_desktop = AddRoot(name, iconIndex, openIconIndex);

	name = GetSpecialFolder(CSIDL_PERSONAL, iconIndex, openIconIndex);
	if (name.empty()) {
		name = _("My Documents");
		iconIndex = openIconIndex = -1;
	}

	m_documents = AppendItem(m_desktop, name, iconIndex, openIconIndex);


	name = GetSpecialFolder(CSIDL_DRIVES, iconIndex, openIconIndex);
	if (name.empty()) {
		name = _("My Computer");
		iconIndex = openIconIndex = -1;
	}

	m_drives = AppendItem(m_desktop, name, iconIndex, openIconIndex);

	DisplayDrives(m_drives);
	Expand(m_desktop);
	Expand(m_drives);

	return true;
}

void CLocalTreeView::OnVolumesEnumerated(wxCommandEvent& event)
{
	if (!m_pVolumeEnumeratorThread) {
		return;
	}

	std::vector<CVolumeDescriptionEnumeratorThread::t_VolumeInfo> volumeInfo = m_pVolumeEnumeratorThread->GetVolumes();

	if (event.GetEventType() == fzEVT_VOLUMESENUMERATED) {
		delete m_pVolumeEnumeratorThread;
		m_pVolumeEnumeratorThread = 0;
	}

	for (auto const& info : volumeInfo) {
		wxString const& drive = info.volume;

		wxTreeItemIdValue tmp;
		wxTreeItemId item = GetFirstChild(m_drives, tmp);
		while (item) {
			wxString name = GetItemText(item);
			if (name == drive || name.Left(drive.Len() + 1) == drive + _T(" ")) {

				if (!info.volumeName.empty()) {
					SetItemText(item, drive + _T(" (") + info.volumeName + _T(")"));
				}
				if (info.icon != -1) {
					SetItemImage(item, info.icon);
				}
				break;
			}
			item = GetNextChild(m_drives, tmp);
		}
	}
}

#endif

void CLocalTreeView::OnContextMenu(wxTreeEvent& event)
{
	m_contextMenuItem = event.GetItem();
	if (!m_contextMenuItem.IsOk()) {
		return;
	}

	wxMenu menu;
	auto item = new wxMenuItem(&menu, XRCID("ID_UPLOAD"), _("&Upload"), _("Upload selected directory"));
	item->SetBitmap(MakeBmpBundle(wxArtProvider::GetBitmap(_T("ART_UPLOAD"), wxART_MENU)));
	menu.Append(item);
	item = new wxMenuItem(&menu, XRCID("ID_ADDTOQUEUE"), _("&Add to queue"), _("Add selected directory to the transfer queue"));
	item->SetBitmap(MakeBmpBundle(wxArtProvider::GetBitmap(_T("ART_UPLOADADD"), wxART_MENU)));
	menu.Append(item);

	menu.AppendSeparator();
	menu.Append(XRCID("ID_OPEN"), _("&Open"), _("Open directory in system's file manager"));

	menu.AppendSeparator();
	menu.Append(XRCID("ID_MKDIR"), _("&Create directory"), _("Create a new subdirectory in the current directory"));
	menu.Append(XRCID("ID_MKDIR_CHGDIR"), _("Create director&y and enter it"), _("Create a new subdirectory in the current directory and change into it"));

	menu.AppendSeparator();
	menu.Append(XRCID("ID_DELETE"), _("&Delete"), _("Delete selected directory"));
	menu.Append(XRCID("ID_RENAME"), _("&Rename"), _("Rename selected directory"));

	CLocalPath const path(GetDirFromItem(m_contextMenuItem));

	const bool hasParent = path.HasParent();
	const bool writeable = path.IsWriteable();

	const bool remoteConnected = m_state.IsRemoteConnected() && !m_state.GetRemotePath().empty();

	bool const idle = m_state.GetLocalRecursiveOperation() && !m_state.GetLocalRecursiveOperation()->IsActive();

	menu.Enable(XRCID("ID_UPLOAD"), hasParent && remoteConnected && idle);
	menu.Enable(XRCID("ID_ADDTOQUEUE"), hasParent && remoteConnected && idle);
	menu.Enable(XRCID("ID_MKDIR"), writeable);
	menu.Enable(XRCID("ID_DELETE"), writeable && hasParent);
	menu.Enable(XRCID("ID_RENAME"), writeable && hasParent);

	PopupMenu(&menu);
}

void CLocalTreeView::OnMenuUpload(wxCommandEvent& event)
{
	auto recursiveOperation = m_state.GetLocalRecursiveOperation();
	if (recursiveOperation->IsActive()) {
		return;
	}

	if (!m_contextMenuItem.IsOk()) {
		return;
	}

	CLocalPath path(GetDirFromItem(m_contextMenuItem));

	if (!path.HasParent()) {
		return;
	}

	if (!m_state.IsRemoteConnected()) {
		return;
	}

	Site const& site = m_state.GetSite();
	if (!site) {
		return;
	}

	CServerPath remotePath = m_state.GetRemotePath();
	if (remotePath.empty()) {
		return;
	}

	if (!remotePath.ChangePath(GetItemText(m_contextMenuItem).ToStdWstring())) {
		return;
	}

	local_recursion_root root;
	root.add_dir_to_visit(path, remotePath);
	recursiveOperation->AddRecursionRoot(std::move(root));

	bool const queue_only = event.GetId() == XRCID("ID_ADDTOQUEUE");

	CFilterManager filter;
	recursiveOperation->StartRecursiveOperation(recursive_operation::recursive_transfer, filter.GetActiveFilters(), !queue_only);
}

// Create a new Directory
void CLocalTreeView::OnMenuMkdir(wxCommandEvent&)
{
	CLocalPath newdir = MenuMkdir();
	if (!newdir.empty()) {
		RefreshListing();
		m_state.RefreshLocal();
	}
}

// Create a new Directory and enter the new Directory
void CLocalTreeView::OnMenuMkdirChgDir(wxCommandEvent&)
{
	CLocalPath newdir = MenuMkdir();
	if (newdir.empty()) {
		return;
	}

	// OnMenuEnter
	std::wstring error;
	if (!m_state.SetLocalDir(newdir, &error)) {
		if (!error.empty()) {
			wxMessageBoxEx(error, _("Failed to change directory"), wxICON_INFORMATION);
		}
		else {
			wxBell();
		}
	}
}

// Helper-Function to create a new Directory
// Returns the name of the new directory
CLocalPath CLocalTreeView::MenuMkdir()
{
	if (!m_contextMenuItem.IsOk()) {
		return {};
	}

	CLocalPath p(GetDirFromItem(m_contextMenuItem));
	if (p.empty() || !p.IsWriteable()) {
		wxBell();
		return {};
	}

	CInputDialog dlg;
	if (!dlg.Create(this, _("Create directory"), _("Please enter the name of the directory which should be created:"))) {
		return {};
	}

	wxString newName = _("New directory");
	dlg.SetValue(p.GetPath() + newName);
	dlg.SelectText(p.GetPath().size(), p.GetPath().size() + newName.size());

	if (dlg.ShowModal() != wxID_OK) {
		return {};
	}

	if (!p.ChangePath(dlg.GetValue().ToStdWstring()) || !p.IsWriteable()) {
		wxBell();
		return {};
	}

	if (!fz::mkdir(fz::to_native(p.GetPath()), true)) {
		wxBell();
		return {};
	}

	return p;
}

void CLocalTreeView::OnMenuRename(wxCommandEvent&)
{
	if (!m_contextMenuItem.IsOk())
		return;

#ifdef __WXMSW__
	if (m_contextMenuItem == m_desktop || m_contextMenuItem == m_documents) {
		wxBell();
		return;
	}
#endif

	CLocalPath path(GetDirFromItem(m_contextMenuItem));
	if (!path.HasParent() || !path.IsWriteable()) {
		wxBell();
		return;
	}

	EditLabel(m_contextMenuItem);
}

void CLocalTreeView::OnMenuDelete(wxCommandEvent&)
{
	if (!m_contextMenuItem.IsOk()) {
		return;
	}

	std::wstring path = GetDirFromItem(m_contextMenuItem);

	CLocalPath local_path(path);
	if (!local_path.HasParent() || !local_path.IsWriteable()) {
		return;
	}

	gui_recursive_remove rmd(this);
	rmd.remove(fz::to_native(path));

	wxTreeItemId item = GetSelection();
	while (item && item != m_contextMenuItem) {
		item = GetItemParent(item);
	}

	if (!item) {
		if (GetItemParent(m_contextMenuItem) == GetSelection()) {
			m_state.RefreshLocal();
		}
		else {
			RefreshListing();
		}
		return;
	}

	if (!path.empty() && path.back() == wxFileName::GetPathSeparator()) {
		path.pop_back();
	}
	size_t pos = path.rfind(wxFileName::GetPathSeparator());
	if (pos == std::wstring::npos || !pos) {
		path = _T("/");
	}
	else {
		path = path.substr(0, pos);
	}

	m_state.SetLocalDir(path);
	RefreshListing();
}

void CLocalTreeView::OnBeginLabelEdit(wxTreeEvent& event)
{
	wxTreeItemId item = event.GetItem();

#ifdef __WXMSW__
	if (item == m_desktop || item == m_documents)
	{
		wxBell();
		event.Veto();
		return;
	}
#endif

	CLocalPath path(GetDirFromItem(item));

	if (!path.HasParent() || !path.IsWriteable())
	{
		wxBell();
		event.Veto();
		return;
	}
}

void CLocalTreeView::OnEndLabelEdit(wxTreeEvent& event)
{
	if (event.IsEditCancelled()) {
		event.Veto();
		return;
	}

	wxTreeItemId item = event.GetItem();

#ifdef __WXMSW__
	if (item == m_desktop || item == m_documents) {
		wxBell();
		event.Veto();
		return;
	}
#endif

	std::wstring path = GetDirFromItem(item);

	CLocalPath local_path(path);
	if (!local_path.HasParent() || !local_path.IsWriteable()) {
		wxBell();
		event.Veto();
		return;
	}

	if (!path.empty() && path.back() == wxFileName::GetPathSeparator()) {
		path.pop_back();
	}

	size_t pos = path.rfind(wxFileName::GetPathSeparator());
	wxASSERT(pos != std::wstring::npos);

	std::wstring parent = path.substr(0, pos + 1);

	const wxString& oldName = GetItemText(item);
	std::wstring const newName = event.GetLabel().ToStdWstring();
	if (newName.empty()) {
		wxBell();
		event.Veto();
		return;
	}

	wxASSERT(parent + oldName == path);

	if (oldName == newName) {
		return;
	}

	if (!RenameFile(this, parent, oldName, newName)) {
		event.Veto();
		return;
	}

	// We may call SetLocalDir, item might be deleted by it, so
	// if we don't rename item now and veto the event, wxWidgets
	// might access deleted item.
	event.Veto();
	SetItemText(item, newName);

	wxTreeItemId currentSel = GetSelection();
	if (currentSel == wxTreeItemId()) {
		RefreshListing();
		return;
	}

	if (item == currentSel) {
		m_state.SetLocalDir(parent + newName);
		return;
	}

	std::wstring sub;

	wxTreeItemId tmp = currentSel;
	while (tmp != GetRootItem() && tmp != item) {
		sub = (wxFileName::GetPathSeparator() + GetItemText(tmp) + sub).ToStdWstring();
		tmp = GetItemParent(tmp);
	}

	if (tmp == GetRootItem()) {
		// Rename unrelated to current selection
		return;
	}

	// Current selection below renamed item
	m_state.SetLocalDir(parent + newName + sub);
}

void CLocalTreeView::OnChar(wxKeyEvent& event)
{
	m_contextMenuItem = GetSelection();

	wxCommandEvent cmdEvt;
	if (event.GetKeyCode() == WXK_F2) {
		OnMenuRename(cmdEvt);
	}
	else if (event.GetKeyCode() == WXK_DELETE || event.GetKeyCode() == WXK_NUMPAD_DELETE) {
		OnMenuDelete(cmdEvt);
	}
	else {
		event.Skip();
	}
}

bool CLocalTreeView::CheckSubdirStatus(wxTreeItemId& item, std::wstring const& path)
{
	wxTreeItemIdValue value;
	wxTreeItemId child = GetFirstChild(item, value);

	static int64_t const size(-1);

#ifdef __WXMAC__
	// By default, OS X has a list of servers mounted into /net,
	// listing that directory is slow.
	if (GetItemParent(item) == GetRootItem() && (path == L"/net" || path == L"/net/")) {
		CFilterManager filter;

		const int attributes = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
		if (!filter.FilenameFiltered(L"localhost", path, true, size, true, attributes, fz::datetime())) {
			if (!child) {
				AppendItem(item, L"");
			}
			return true;
		}
	}
#endif

	if (child) {
		if (!GetItemText(child).empty()) {
			return false;
		}

		CTreeItemData* pData = (CTreeItemData*)GetItemData(child);
		if (pData) {
			bool was_link;
			int attributes;
			fz::local_filesys::type type;
			fz::datetime date;
			if (!path.empty() && path.back() == fz::local_filesys::path_separator) {
				type = fz::local_filesys::get_file_info(fz::to_native(path + pData->m_known_subdir), was_link, 0, &date, &attributes);
			}
			else {
				type = fz::local_filesys::get_file_info(fz::to_native(path + static_cast<wchar_t>(fz::local_filesys::path_separator) + pData->m_known_subdir), was_link, 0, &date, &attributes);
			}
			if (type == fz::local_filesys::dir) {
				CFilterManager filter;
				if (!filter.FilenameFiltered(pData->m_known_subdir, path, true, size, true, attributes, date)) {
					return true;
				}
			}
		}
	}

	std::wstring sub = HasSubdir(path);
	if (!sub.empty()) {
		wxTreeItemId subItem = AppendItem(item, L"");
		SetItemData(subItem, new CTreeItemData(sub));
	}
	else if (child) {
		Delete(child);
	}

	return true;
}

#ifdef __WXMSW__
void CLocalTreeView::OnDevicechange(WPARAM wParam, LPARAM lParam)
{
	if (!m_drives) {
		return;
	}

	if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
		DEV_BROADCAST_HDR* pHdr = (DEV_BROADCAST_HDR*)lParam;
		if (pHdr->dbch_devicetype != DBT_DEVTYP_VOLUME) {
			return;
		}

		// Added or removed volume
		DEV_BROADCAST_VOLUME* pVolume = (DEV_BROADCAST_VOLUME*)lParam;

		wxChar drive = 'A';
		int mask = 1;
		while (drive <= 'Z') {
			if (pVolume->dbcv_unitmask & mask) {
				if (wParam == DBT_DEVICEARRIVAL)
					AddDrive(drive);
				else {
					RemoveDrive(drive);

					if (pVolume->dbcv_flags & DBTF_MEDIA) {
						// E.g. disk removed from CD-ROM drive, need to keep the drive letter
						AddDrive(drive);
					}
				}
			}
			drive++;
			mask <<= 1;
		}

		if (GetSelection() == m_drives) {
			m_state.RefreshLocal();
		}
	}
}

wxTreeItemId CLocalTreeView::AddDrive(wxChar letter)
{
	wxString drive = letter;
	drive += _T(":");

	long drivesToHide = CVolumeDescriptionEnumeratorThread::GetDrivesToHide();
	if ( CVolumeDescriptionEnumeratorThread::IsHidden(drive.wc_str(), drivesToHide) ) {
		return wxTreeItemId();
	}

	// Get the label of the drive
	wxChar volumeName[501];
	int oldErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS);
	BOOL res = GetVolumeInformation((drive + _T("\\")).wc_str(), volumeName, 500, 0, 0, 0, 0, 0);
	SetErrorMode(oldErrorMode);

	wxString itemLabel = drive;
	if (res && volumeName[0]) {
		itemLabel += _T(" (");
		itemLabel += volumeName;
		itemLabel += _T(")");
	}

	wxTreeItemIdValue value;
	wxTreeItemId driveItem = GetFirstChild(m_drives, value);
	while (driveItem) {
		if (!GetItemText(driveItem).Left(2).CmpNoCase(drive)) {
			break;
		}

		driveItem = GetNextSibling(driveItem);
	}
	if (driveItem) {
		SetItemText(driveItem, itemLabel);
		int icon = GetIconIndex(iconType::dir, drive.ToStdWstring() + L"\\");
		SetItemImage(driveItem, icon, wxTreeItemIcon_Normal);
		SetItemImage(driveItem, icon, wxTreeItemIcon_Selected);
		SetItemImage(driveItem, icon, wxTreeItemIcon_Expanded);
		SetItemImage(driveItem, icon, wxTreeItemIcon_SelectedExpanded);

		return driveItem;
	}

	wxTreeItemId item = AppendItem(m_drives, itemLabel, GetIconIndex(iconType::dir, drive.ToStdWstring() + L"\\"));
	AppendItem(item, L"");
	SortChildren(m_drives);

	return item;
}

void CLocalTreeView::RemoveDrive(wxChar drive)
{
	wxString driveName = drive;
	driveName += L":";
	wxTreeItemIdValue value;
	wxTreeItemId driveItem = GetFirstChild(m_drives, value);
	while (driveItem) {
		if (!GetItemText(driveItem).Left(2).CmpNoCase(driveName)) {
			break;
		}

		driveItem = GetNextSibling(driveItem);
	}
	if (!driveItem) {
		return;
	}

	Delete(driveItem);
}

void CLocalTreeView::OnSelectionChanging(wxTreeEvent& event)
{
	// On-demand open icon for selected items
	wxTreeItemId item = event.GetItem();

	if (!item) {
		return;
	}

	if (GetItemImage(item, wxTreeItemIcon_Selected) == -1) {
		int icon = GetIconIndex(iconType::opened_dir, GetDirFromItem(item));
		SetItemImage(item, icon, wxTreeItemIcon_Selected);
		SetItemImage(item, icon, wxTreeItemIcon_SelectedExpanded);
	}
}

#endif

void CLocalTreeView::OnMenuOpen(wxCommandEvent&)
{
	if (!m_contextMenuItem.IsOk()) {
		return;
	}

	std::wstring path = GetDirFromItem(m_contextMenuItem);
	if (path.empty()) {
		return;
	}

	OpenInFileManager(path);
}

void CLocalTreeView::OnOptionsChanged(watched_options const& options)
{
	if (options.test(OPTION_FILELIST_NAMESORT)) {
		UpdateSortMode();
		RefreshListing();
	}
}
