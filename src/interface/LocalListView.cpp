#include "filezilla.h"

#define FILELISTCTRL_INCLUDE_TEMPLATE_DEFINITION

#include "view.h"
#include "LocalListView.h"
#include "queue.h"
#include "filezillaapp.h"
#include "filter_manager.h"
#include "file_utils.h"
#include "infotext.h"
#include "inputdialog.h"
#include <algorithm>
#include "dndobjects.h"
#include "Options.h"
#ifdef __WXMSW__
#include "lm.h"
#include "volume_enumerator.h"
#endif
#include "dragdropmanager.h"
#include "drop_target_ex.h"
#include "edithandler.h"
#include "filelist_statusbar.h"
#include "graphics.h"
#include "local_recursive_operation.h"
#include "timeformatting.h"

#include "../include/sizeformatting.h"

#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/process.hpp>
#include <libfilezilla/recursive_remove.hpp>

#include <wx/menu.h>

using namespace std::literals;

class CLocalListViewDropTarget final : public CFileDropTarget<wxListCtrlEx>
{
public:
	CLocalListViewDropTarget(CLocalListView* pLocalListView, login_manager& lim)
		: CFileDropTarget<wxListCtrlEx>(pLocalListView, pLocalListView->options_, lim)
		, m_pLocalListView(pLocalListView)
	{
	}

	void ClearDropHighlight()
	{
		const int dropTarget = m_pLocalListView->m_dropTarget;
		if (dropTarget != -1)
		{
			m_pLocalListView->m_dropTarget = -1;
#ifdef __WXMSW__
			m_pLocalListView->SetItemState(dropTarget, 0, wxLIST_STATE_DROPHILITED);
#else
			m_pLocalListView->RefreshItem(dropTarget);
#endif
		}
	}

	virtual wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = FixupDragResult(def);

		if (def == wxDragError ||
			def == wxDragNone ||
			def == wxDragCancel)
		{
			return def;
		}

		if (m_pLocalListView->m_fileData.empty()) {
			return wxDragError;
		}

		if (def != wxDragCopy && def != wxDragMove) {
			return wxDragError;
		}

		CDragDropManager* pDragDropManager = CDragDropManager::Get();
		if (pDragDropManager) {
			pDragDropManager->pDropTarget = m_pLocalListView;
		}

		std::wstring subdir;
		int flags{};
		int hit = m_pLocalListView->HitTest(wxPoint(x, y), flags, 0);
		if (hit != -1 && (flags & wxLIST_HITTEST_ONITEM)) {
			const CLocalFileData* const data = m_pLocalListView->GetData(hit);
			if (data && data->is_dir()) {
				subdir = data->name;
			}
		}

		CLocalPath dir = m_pLocalListView->m_state.GetLocalDir();
		if (!subdir.empty()) {
			if (!dir.ChangePath(subdir)) {
				return wxDragError;
			}
		}

		if (!dir.IsWriteable()) {
			return wxDragError;
		}

		if (!GetData()) {
			return wxDragError;
		}

		auto const format = GetReceivedFormat();
		if (format == m_pFileDataObject->GetFormat()) {
			m_pLocalListView->m_state.HandleDroppedFiles(m_pFileDataObject, dir, def == wxDragCopy);
		}
		else if (format == LocalDataObjectFormat()) {
			m_pLocalListView->m_state.HandleDroppedFiles(GetLocalDataObject(), dir, def == wxDragCopy);
		}
		else {
			auto * obj = GetRemoteDataObject();
			if (obj->GetProcessId() != (int)wxGetProcessId()) {
				wxMessageBoxEx(_("Drag&drop between different instances of FileZilla has not been implemented yet."));
				return wxDragNone;
			}

			if (!m_pLocalListView->m_state.GetSite() || obj->GetSite().server != m_pLocalListView->m_state.GetSite().server) {
				wxMessageBoxEx(_("Drag&drop between different servers has not been implemented yet."));
				return wxDragNone;
			}

			if (!m_pLocalListView->m_state.DownloadDroppedFiles(obj, dir)) {
				return wxDragNone;
			}
		}

		return def;
	}

	virtual bool OnDrop(wxCoord x, wxCoord y)
	{
		CFileDropTarget::OnDrop(x, y);
		ClearDropHighlight();

		if (m_pLocalListView->m_fileData.empty()) {
			return false;
		}

		return true;
	}

	virtual int DisplayDropHighlight(wxPoint const& point) override
	{
		DoDisplayDropHighlight(point);
		return -1;
	}

	virtual wxString DoDisplayDropHighlight(wxPoint point)
	{
		wxString subDir;

		int flags{};
		int hit = m_pLocalListView->HitTest(point, flags, 0);
		if (!(flags & wxLIST_HITTEST_ONITEM)) {
			hit = -1;
		}

		if (hit != -1) {
			const CLocalFileData* const data = m_pLocalListView->GetData(hit);
			if (!data || !data->is_dir()) {
				hit = -1;
			}
			else {
				const CDragDropManager* pDragDropManager = CDragDropManager::Get();
				if (pDragDropManager && pDragDropManager->pDragSource == m_pLocalListView) {
					if (m_pLocalListView->GetItemState(hit, wxLIST_STATE_SELECTED)) {
						hit = -1;
					}
					else {
						subDir = data->name;
					}
				}
				else {
					subDir = data->name;
				}
			}
		}
		if (hit != m_pLocalListView->m_dropTarget) {
			ClearDropHighlight();
			if (hit != -1) {
				m_pLocalListView->m_dropTarget = hit;
#ifdef __WXMSW__
				m_pLocalListView->SetItemState(hit, wxLIST_STATE_DROPHILITED, wxLIST_STATE_DROPHILITED);
#else
				m_pLocalListView->RefreshItem(hit);
#endif
			}
		}

		return subDir;
	}

	virtual wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = CFileDropTarget::OnDragOver(x, y, def);

		if (def == wxDragError ||
			def == wxDragNone ||
			def == wxDragCancel)
		{
			ClearDropHighlight();
			return def;
		}

		if (m_pLocalListView->m_fileData.empty()) {
			ClearDropHighlight();
			return wxDragNone;
		}

		std::wstring const subdir = DoDisplayDropHighlight(wxPoint(x, y)).ToStdWstring();

		CLocalPath dir = m_pLocalListView->m_state.GetLocalDir();
		if (subdir.empty()) {
			const CDragDropManager* pDragDropManager = CDragDropManager::Get();
			if (pDragDropManager && pDragDropManager->localParent == m_pLocalListView->m_dir) {
				return wxDragNone;
			}
		}
		else {
			if (!dir.ChangePath(subdir)) {
				return wxDragNone;
			}
		}

		if (!dir.IsWriteable()) {
			return wxDragNone;
		}

		return def;
	}

	virtual void OnLeave()
	{
		CFileDropTarget::OnLeave();
		ClearDropHighlight();
	}

	virtual wxDragResult OnEnter(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = CFileDropTarget::OnEnter(x, y, def);
		return OnDragOver(x, y, def);
	}

protected:
	CLocalListView *m_pLocalListView{};
};

BEGIN_EVENT_TABLE(CLocalListView, CFileListCtrl<CLocalFileData>)
	EVT_LIST_ITEM_ACTIVATED(wxID_ANY, CLocalListView::OnItemActivated)
	EVT_CONTEXT_MENU(CLocalListView::OnContextMenu)
	// Map both ID_UPLOAD and ID_ADDTOQUEUE to OnMenuUpload, code is identical
	EVT_MENU(XRCID("ID_UPLOAD"), CLocalListView::OnMenuUpload)
	EVT_MENU(XRCID("ID_ADDTOQUEUE"), CLocalListView::OnMenuUpload)
	EVT_MENU(XRCID("ID_MKDIR"), CLocalListView::OnMenuMkdir)
	EVT_MENU(XRCID("ID_MKDIR_CHGDIR"), CLocalListView::OnMenuMkdirChgDir)
	EVT_MENU(XRCID("ID_DELETE"), CLocalListView::OnMenuDelete)
	EVT_MENU(XRCID("ID_RENAME"), CLocalListView::OnMenuRename)
	EVT_KEY_DOWN(CLocalListView::OnKeyDown)
	EVT_SIZE(CLocalListView::OnSize)
	EVT_LIST_BEGIN_DRAG(wxID_ANY, CLocalListView::OnBeginDrag)
	EVT_MENU(XRCID("ID_OPEN"), CLocalListView::OnMenuOpen)
	EVT_MENU(XRCID("ID_EDIT"), CLocalListView::OnMenuEdit)
	EVT_MENU(XRCID("ID_ENTER"), CLocalListView::OnMenuEnter)
#ifdef __WXMSW__
	EVT_COMMAND(-1, fzEVT_VOLUMESENUMERATED, CLocalListView::OnVolumesEnumerated)
	EVT_COMMAND(-1, fzEVT_VOLUMEENUMERATED, CLocalListView::OnVolumesEnumerated)
#endif
	EVT_MENU(XRCID("ID_CONTEXT_REFRESH"), CLocalListView::OnMenuRefresh)
END_EVENT_TABLE()

CLocalListView::CLocalListView(CView* pParent, CState& state, CQueueView *pQueue, COptionsBase & options, TimeFormatter & time_formatter, login_manager& lim, CEditHandler* edit_handler)
	: CFileListCtrl<CLocalFileData>(pParent, pQueue, options, time_formatter)
	, CStateEventHandler(state)
	, m_parentView(pParent)
	, edit_handler_(edit_handler)
{
	wxGetApp().AddStartupProfileRecord("CLocalListView::CLocalListView"sv);
	m_state.RegisterHandler(this, STATECHANGE_LOCAL_DIR);
	m_state.RegisterHandler(this, STATECHANGE_APPLYFILTER);
	m_state.RegisterHandler(this, STATECHANGE_LOCAL_REFRESH_FILE);
	m_state.RegisterHandler(this, STATECHANGE_SERVER);

	const unsigned long widths[4] = { 170, 80, 120, 120 };

	AddColumn(_("Filename"), wxLIST_FORMAT_LEFT, widths[0], true);
	AddColumn(_("Filesize"), wxLIST_FORMAT_RIGHT, widths[1]);
	AddColumn(_("Filetype"), wxLIST_FORMAT_LEFT, widths[2]);
	AddColumn(_("Last modified"), wxLIST_FORMAT_LEFT, widths[3]);
	LoadColumnSettings(options_, OPTION_LOCALFILELIST_COLUMN_WIDTHS, OPTION_LOCALFILELIST_COLUMN_SHOWN, OPTION_LOCALFILELIST_COLUMN_ORDER);

	SetImageList(GetSystemImageList(), wxIMAGE_LIST_SMALL);

	InitHeaderSortImageList();

	InitSort(OPTION_LOCALFILELIST_SORTORDER);

	SetDropTarget(new CLocalListViewDropTarget(this, lim));

	EnablePrefixSearch(true);

	m_windowTinter = std::make_unique<CWindowTinter>(*GetMainWindow());

	m_pInfoText = new CInfoText(*this);
}

CLocalListView::~CLocalListView()
{
	wxString str = wxString::Format(_T("%d %d"), m_sortDirection, m_sortColumn);
	options_.set(OPTION_LOCALFILELIST_SORTORDER, str.ToStdWstring());

#ifdef __WXMSW__
	volumeEnumeratorThread_.reset();
#endif
}

bool CLocalListView::DisplayDir(CLocalPath const& dirname)
{
	CancelLabelEdit();

	std::wstring focused;
	int focusedItem = -1;
	std::vector<std::wstring> selectedNames;
	bool ensureVisible = false;
	if (m_dir != dirname) {
		ResetSearchPrefix();

		if (IsComparing()) {
			ExitComparisonMode();
		}

		ClearSelection();
		focused = m_state.GetPreviouslyVisitedLocalSubdir();
		ensureVisible = !focused.empty();
		if (focused.empty()) {
			focused = _T("..");
		}

		if (GetItemCount()) {
			EnsureVisible(0);
		}
		m_dir = dirname;
	}
	else {
		// Remember which items were selected
		selectedNames = RememberSelectedItems(focused, focusedItem);
	}

	if (m_pFilelistStatusBar) {
		m_pFilelistStatusBar->UnselectAll();
	}

	const int oldItemCount = m_indexMapping.size();

	m_fileData.clear();
	m_indexMapping.clear();

	m_hasParent = m_dir.HasLogicalParent();

	if (m_hasParent) {
		CLocalFileData data;
		data.flags = CLocalFileData::flag_dir;
		data.name = _T("..");
		data.size = -1;
		data.attributes = {};
		m_fileData.push_back(data);
		m_indexMapping.push_back(0);
	}

#ifdef __WXMSW__
	if (m_dir.GetPath() == _T("\\")) {
		DisplayDrives();
	}
	else if (m_dir.GetPath().substr(0, 2) == _T("\\\\")) {
		auto pos = m_dir.GetPath().find('\\', 2);
		if (pos != std::wstring::npos && pos + 1 < m_dir.GetPath().size()) {
			goto regular_dir;
		}

		// UNC path without shares
		DisplayShares(m_dir.GetPath());
	}
	else
#endif
	{
#ifdef __WXMSW__
regular_dir:
#endif
		CStateFilterManager const& filter = m_state.GetStateFilterManager();
		fz::local_filesys local_filesys;

		auto result = local_filesys.begin_find_files(fz::to_native(m_dir.GetPath()), false);
		if (!result) {

			if (result.error_ == fz::result::noperm) {
				SetInfoText(_("You do not have permission to list this directory"));
			}
			else {
				SetInfoText(_("Could not list directory contents"));
			}

			SetItemCount(1);
			if (m_pFilelistStatusBar) {
				m_pFilelistStatusBar->SetDirectoryContents(0, 0, 0, 0, 0);
			}

			return false;
		}

		SetInfoText(wxString());

		int64_t totalSize{};
		int unknown_sizes = 0;
		int totalFileCount = 0;
		int totalDirCount = 0;
		int hidden = 0;

		int num = m_fileData.size();
		CLocalFileData data;
		bool wasLink{};
		fz::local_filesys::type t{};
		fz::native_string name;
		while (local_filesys.get_next_file(name, wasLink, t, &data.size, &data.time, &data.attributes)) {
			data.name = fz::to_wstring(name);
			data.flags = (t == fz::local_filesys::dir) ? CLocalFileData::flag_dir : 0;
			if (wasLink) {
				data.flags |= CLocalFileData::flag_link;
			}
			if (name.empty() || data.name.empty()) {
				wxGetApp().DisplayEncodingWarning();
				continue;
			}

			m_fileData.push_back(data);
			if (!filter.FilenameFiltered(data.name, m_dir.GetPath(), data.is_dir(), data.size, true, data.attributes, data.time)) {
				if (data.is_dir()) {
					++totalDirCount;
				}
				else {
					if (data.size != -1) {
						totalSize += data.size;
					}
					else {
						++unknown_sizes;
					}
					++totalFileCount;
				}
				m_indexMapping.push_back(num);
			}
			else {
				++hidden;
			}
			++num;
		}

		if (m_pFilelistStatusBar) {
			m_pFilelistStatusBar->SetDirectoryContents(totalFileCount, totalDirCount, totalSize, unknown_sizes, hidden);
		}
	}

	if (m_dropTarget != -1) {
		CLocalFileData* data = GetData(m_dropTarget);
		if (!data || !data->is_dir()) {
			SetItemState(m_dropTarget, 0, wxLIST_STATE_DROPHILITED);
			m_dropTarget = -1;
		}
	}

	const int count = m_indexMapping.size();
	if (oldItemCount != count) {
		SetItemCount(count);
	}

	SortList(-1, -1, false);

	if (IsComparing()) {
		m_originalIndexMapping.clear();
		RefreshComparison();
	}

	ReselectItems(selectedNames, std::move(focused), focusedItem, ensureVisible);

	RefreshListOnly();

	return true;
}

// See comment to OnGetItemText
int CLocalListView::OnGetItemImage(long item) const
{
	CLocalListView *pThis = const_cast<CLocalListView *>(this);
	CLocalFileData *data = pThis->GetData(item);
	if (!data) {
		return -1;
	}
	int &icon = data->icon;

	if (icon == -2) {
		std::wstring path;
		if (data->name != L"..") {
#ifdef __WXMSW__
			if (m_dir.GetPath() == L"\\") {
				path = data->name + L"\\";
			}
			else
#endif
			{
				path = m_dir.GetPath() + data->name;
			}
		}

		icon = pThis->GetIconIndex(data->is_dir() ? iconType::dir : iconType::file, path, true, data->is_link());
	}
	return icon;
}

void CLocalListView::OnItemActivated(wxListEvent &event)
{
	int count = 0;
	bool back = false;

	int item = -1;
	for (;;) {
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1) {
			break;
		}

		count++;

		if (!item && m_hasParent) {
			back = true;
		}
	}
	if (count > 1) {
		if (back) {
			wxBell();
			return;
		}

		wxCommandEvent cmdEvent;
		OnMenuUpload(cmdEvent);
		return;
	}

	item = event.GetIndex();

	CLocalFileData *data = GetData(item);
	if (!data) {
		return;
	}

	if (data->is_dir()) {
		const int action = options_.get_int(OPTION_DOUBLECLICK_ACTION_DIRECTORY);
		if (action == 3) {
			// No action
			wxBell();
			return;
		}

		if (!action || data->name == _T("..")) {
			// Enter action

			std::wstring error;
			if (!m_state.SetLocalDir(data->name, &error)) {
				if (!error.empty()) {
					wxMessageBoxEx(error, _("Failed to change directory"), wxICON_INFORMATION);
				}
				else {
					wxBell();
				}
			}
			return;
		}

		wxCommandEvent evt(0, action == 1 ? XRCID("ID_UPLOAD") : XRCID("ID_ADDTOQUEUE"));
		OnMenuUpload(evt);
		return;
	}

	if (data->comparison_flags == fill) {
		wxBell();
		return;
	}

	const int action = options_.get_int(OPTION_DOUBLECLICK_ACTION_FILE);
	if (action == 3) {
		// No action
		wxBell();
		return;
	}

	if (action == 2) {
		// View / Edit action
		wxCommandEvent evt;
		OnMenuEdit(evt);
		return;
	}

	Site const& site = m_state.GetSite();
	if (!site) {
		wxBell();
		return;
	}

	CServerPath path = m_state.GetRemotePath();
	if (path.empty()) {
		wxBell();
		return;
	}

	const bool queue_only = action == 1;

	m_pQueue->QueueFile(queue_only, false, data->name, wxEmptyString, m_dir, path, site, data->size);
	m_pQueue->QueueFile_Finish(true);
}

void CLocalListView::OnMenuEnter(wxCommandEvent &)
{
	int item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (item == -1) {
		wxBell();
		return;
	}

	if (GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) != -1) {
		wxBell();
		return;
	}

	CLocalFileData *data = GetData(item);
	if (!data || !data->is_dir()) {
		wxBell();
		return;
	}

	std::wstring error;
	if (!m_state.SetLocalDir(data->name, &error)) {
		if (!error.empty()) {
			wxMessageBoxEx(error, _("Failed to change directory"), wxICON_INFORMATION);
		}
		else {
			wxBell();
		}
	}
}

#ifdef __WXMSW__
void CLocalListView::DisplayDrives()
{
	SetInfoText(wxString());
	int count = m_fileData.size();

	std::vector<std::wstring> drives = CVolumeDescriptionEnumeratorThread::GetDrives();
	for (auto it = drives.begin(); it != drives.end(); ++it) {
		std::wstring drive = *it;
		if (!drive.empty() && drive.back() == '\\') {
			drive.pop_back();
		}

		CLocalFileData data;
		data.name = drive;
		data.label = fz::sparse_optional<std::wstring>(data.name);
		data.flags = CLocalFileData::flag_dir;
		data.size = -1;
		data.icon = GetIconIndex(iconType::dir, std::wstring(), false);

		m_fileData.push_back(data);
		m_indexMapping.push_back(count);
		++count;
	}

	if (m_pFilelistStatusBar) {
		m_pFilelistStatusBar->SetDirectoryContents(0, drives.size(), 0, false, 0);
	}

	if (!volumeEnumeratorThread_) {
		volumeEnumeratorThread_ = std::make_unique<CVolumeDescriptionEnumeratorThread>(this, m_state.pool_);
		if (volumeEnumeratorThread_->Failed()) {
			volumeEnumeratorThread_.reset();
		}
	}
}

void CLocalListView::DisplayShares(wxString computer)
{
	SetInfoText(wxString());

	// Cast through a union to avoid warning about breaking strict aliasing rule
	union
	{
		SHARE_INFO_1* pShareInfo;
		LPBYTE pShareInfoBlob;
	} si;

	DWORD read, total;
	DWORD resume_handle = 0;

	if (!computer.empty() && computer.Last() == '\\') {
		computer.RemoveLast();
	}

	int j = m_fileData.size();
	int share_count = 0;
	int res = 0;
	do {
		const wxWX2WCbuf buf = computer.wc_str(wxConvLocal);
		res = NetShareEnum((wchar_t*)(const wchar_t*)buf, 1, &si.pShareInfoBlob, MAX_PREFERRED_LENGTH, &read, &total, &resume_handle);

		if (res != ERROR_SUCCESS && res != ERROR_MORE_DATA) {
			break;
		}

		SHARE_INFO_1* p = si.pShareInfo;
		for (unsigned int i = 0; i < read; ++i, ++p) {
			if (p->shi1_type != STYPE_DISKTREE) {
				continue;
			}

			CLocalFileData data;
			data.name = p->shi1_netname;
#ifdef __WXMSW__
			data.label = fz::sparse_optional<std::wstring>(data.name);
#endif
			data.flags = CLocalFileData::flag_dir;
			data.size = -1;

			m_fileData.push_back(data);
			m_indexMapping.push_back(j++);

			share_count++;
		}

		NetApiBufferFree(si.pShareInfo);
	}
	while (res == ERROR_MORE_DATA);

	if (m_pFilelistStatusBar) {
		m_pFilelistStatusBar->SetDirectoryContents(0, share_count, 0, false, 0);
	}
}

#endif //__WXMSW__

CLocalFileData* CLocalListView::GetData(unsigned int item)
{
	if (!IsItemValid(item)) {
		return 0;
	}

	return &m_fileData[m_indexMapping[item]];
}

bool CLocalListView::IsItemValid(unsigned int item) const
{
	if (item >= m_indexMapping.size()) {
		return false;
	}

	unsigned int index = m_indexMapping[item];
	if (index >= m_fileData.size()) {
		return false;
	}

	return true;
}

void CLocalListView::UpdateSortComparisonObject()
{
	CFileListCtrlSortBase::DirSortMode dirSortMode = GetDirSortMode();
	NameSortMode nameSortMode = GetNameSortMode();

	if (!m_sortDirection) {
		if (m_sortColumn == 1) {
			sortComparisonObject_ = std::make_unique<CFileListCtrlSortSize<std::vector<CLocalFileData>, CLocalFileData>>(m_fileData, m_fileData, dirSortMode, nameSortMode, this);
		}
		else if (m_sortColumn == 2) {
			sortComparisonObject_ = std::make_unique<CFileListCtrlSortType<std::vector<CLocalFileData>, CLocalFileData>>(m_fileData, m_fileData, dirSortMode, nameSortMode, this);
		}
		else if (m_sortColumn == 3) {
			sortComparisonObject_ = std::make_unique<CFileListCtrlSortTime<std::vector<CLocalFileData>, CLocalFileData>>(m_fileData, m_fileData, dirSortMode, nameSortMode, this);
		}
		else {
			sortComparisonObject_ = std::make_unique<CFileListCtrlSortName<std::vector<CLocalFileData>, CLocalFileData>>(m_fileData, m_fileData, dirSortMode, nameSortMode, this);
		}
	}
	else {
		if (m_sortColumn == 1) {
			sortComparisonObject_ = std::make_unique<CReverseSort<CFileListCtrlSortSize<std::vector<CLocalFileData>, CLocalFileData>, CLocalFileData>>(m_fileData, m_fileData, dirSortMode, nameSortMode, this);
		}
		else if (m_sortColumn == 2) {
			sortComparisonObject_ = std::make_unique<CReverseSort<CFileListCtrlSortType<std::vector<CLocalFileData>, CLocalFileData>, CLocalFileData>>(m_fileData, m_fileData, dirSortMode, nameSortMode, this);
		}
		else if (m_sortColumn == 3) {
			sortComparisonObject_ = std::make_unique<CReverseSort<CFileListCtrlSortTime<std::vector<CLocalFileData>, CLocalFileData>, CLocalFileData>>(m_fileData, m_fileData, dirSortMode, nameSortMode, this);
		}
		else {
			sortComparisonObject_ = std::make_unique<CReverseSort<CFileListCtrlSortName<std::vector<CLocalFileData>, CLocalFileData>, CLocalFileData>>(m_fileData, m_fileData, dirSortMode, nameSortMode, this);
		}
	}
}

void CLocalListView::OnContextMenu(wxContextMenuEvent& event)
{
	if (GetEditControl()) {
		event.Skip();
		return;
	}

	wxMenu menu;

	auto item = new wxMenuItem(&menu, XRCID("ID_UPLOAD"), _("&Upload"), _("Upload selected files and directories"));
	item->SetBitmap(MakeBmpBundle(wxArtProvider::GetBitmap(_T("ART_UPLOAD"), wxART_MENU)));
	menu.Append(item);
	item = new wxMenuItem(&menu, XRCID("ID_ADDTOQUEUE"), _("&Add files to queue"), _("Add selected files and folders to the transfer queue"));
	item->SetBitmap(MakeBmpBundle(wxArtProvider::GetBitmap(_T("ART_UPLOADADD"), wxART_MENU)));
	menu.Append(item);
	menu.Append(XRCID("ID_ENTER"), _("E&nter directory"), _("Enter selected directory"));

	menu.AppendSeparator();
	menu.Append(XRCID("ID_OPEN"), _("&Open"), _("Open the file."));
	menu.Append(XRCID("ID_EDIT"), _("&Edit"), _("Edit the file with the configured editor and upload changes to the server."));

	menu.AppendSeparator();
	menu.Append(XRCID("ID_MKDIR"), _("&Create directory"), _("Create a new subdirectory in the current directory"));
	menu.Append(XRCID("ID_MKDIR_CHGDIR"), _("Create director&y and enter it"), _("Create a new subdirectory in the current directory and change into it"));
	menu.Append(XRCID("ID_CONTEXT_REFRESH"), _("Re&fresh"));

	menu.AppendSeparator();
	menu.Append(XRCID("ID_DELETE"),_("&Delete"), _("Delete selected files and directories"));
	menu.Append(XRCID("ID_RENAME"), _("&Rename"), _("Rename selected files and directories"));

	const bool connected = m_state.IsRemoteConnected();
	if (!connected) {
		menu.Enable(XRCID("ID_EDIT"), options_.get_int(OPTION_EDIT_TRACK_LOCAL) == 0);
		menu.Enable(XRCID("ID_UPLOAD"), false);
		menu.Enable(XRCID("ID_ADDTOQUEUE"), false);
	}

	int index = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	int count = 0;
	int fillCount = 0;
	bool selectedDir = false;
	while (index != -1) {
		++count;
		const CLocalFileData* const data = GetData(index);
		if (!data || (!index && m_hasParent)) {
			menu.Enable(XRCID("ID_OPEN"), false);
			menu.Enable(XRCID("ID_RENAME"), false);
			menu.Enable(XRCID("ID_EDIT"), false);
		}
		if ((data && data->comparison_flags == fill) || (!index && m_hasParent)) {
			fillCount++;
		}
		if (data && data->is_dir()) {
			selectedDir = true;
		}
		index = GetNextItem(index, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	}
	if (!count || fillCount == count) {
		menu.Delete(XRCID("ID_ENTER"));
		menu.Enable(XRCID("ID_UPLOAD"), false);
		menu.Enable(XRCID("ID_ADDTOQUEUE"), false);
		menu.Enable(XRCID("ID_DELETE"), false);
		menu.Enable(XRCID("ID_RENAME"), false);
		menu.Enable(XRCID("ID_EDIT"), false);
	}
	else if (count > 1) {
		menu.Delete(XRCID("ID_ENTER"));
		menu.Enable(XRCID("ID_RENAME"), false);
	}
	else {
		// Exactly one item selected
		if (!selectedDir) {
			menu.Delete(XRCID("ID_ENTER"));
		}
	}
	if (selectedDir) {
		menu.Enable(XRCID("ID_EDIT"), false);
		if (m_state.GetLocalRecursiveOperation() && m_state.GetLocalRecursiveOperation()->IsActive()) {
			menu.Enable(XRCID("ID_UPLOAD"), false);
			menu.Enable(XRCID("ID_ADDTOQUEUE"), false);
		}
	}

	PopupMenu(&menu);
}

void CLocalListView::OnMenuUpload(wxCommandEvent& event)
{
	Site const& site = m_state.GetSite();
	if (!site) {
		wxBell();
		return;
	}

	bool added = false;

	bool queue_only = event.GetId() == XRCID("ID_ADDTOQUEUE");

	auto recursiveOperation = m_state.GetLocalRecursiveOperation();
	if (!recursiveOperation || recursiveOperation->IsActive()) {
		wxBell();
		return;
	}

	local_recursion_root root;

	long item = -1;
	for (;;) {
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (!item && m_hasParent) {
			continue;
		}
		if (item == -1) {
			break;
		}

		const CLocalFileData *data = GetData(item);
		if (!data) {
			break;
		}

		if (data->comparison_flags == fill) {
			continue;
		}

		CServerPath remotePath = m_state.GetRemotePath();
		if (remotePath.empty()) {
			wxBell();
			break;
		}

		if (data->is_dir()) {
			CLocalPath localPath = m_dir;
			if (!localPath.ChangePath(data->name)) {
				continue;
			}
			if (!remotePath.ChangePath(data->name)) {
				continue;
			}

			root.add_dir_to_visit(localPath, remotePath);
		}
		else {
			m_pQueue->QueueFile(queue_only, false, data->name, wxEmptyString, m_dir, remotePath, site, data->size);
			added = true;
		}
	}

	if (added) {
		m_pQueue->QueueFile_Finish(!queue_only);
	}

	if (!root.empty()) {
		recursiveOperation->AddRecursionRoot(std::move(root));
		CFilterManager filter;
		recursiveOperation->StartRecursiveOperation(recursive_operation::recursive_transfer, filter.GetActiveFilters(), !queue_only);
	}
}

// Create a new Directory
void CLocalListView::OnMenuMkdir(wxCommandEvent&)
{
	CLocalPath newdir = MenuMkdir();
	if (!newdir.empty()) {
		m_state.RefreshLocal();
	}
}

// Create a new Directory and enter the new Directory
void CLocalListView::OnMenuMkdirChgDir(wxCommandEvent&)
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
CLocalPath CLocalListView::MenuMkdir()
{
	CInputDialog dlg;
	if (!dlg.Create(this, _("Create directory"), _("Please enter the name of the directory which should be created:"))) {
		return {};
	}

	if (dlg.ShowModal() != wxID_OK) {
		return {};
	}

	if (dlg.GetValue().empty()) {
		wxBell();
		return {};
	}

	CLocalPath p = m_dir;
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

void CLocalListView::OnMenuDelete(wxCommandEvent&)
{
	std::list<fz::native_string> pathsToDelete;
	long item = -1;
	while ((item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		if (!item && m_hasParent) {
			continue;
		}

		CLocalFileData *data = GetData(item);
		if (!data) {
			continue;
		}

		if (data->comparison_flags == fill) {
			continue;
		}

		pathsToDelete.push_back(fz::to_native(m_dir.GetPath() + data->name));
	}
	gui_recursive_remove rmd(this);
	rmd.remove(pathsToDelete);

	m_state.SetLocalDir(m_dir);
}

void CLocalListView::OnMenuRename(wxCommandEvent&)
{
	if (GetEditControl()) {
		GetEditControl()->SetFocus();
		return;
	}

	int item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (item < 0 || (!item && m_hasParent)) {
		wxBell();
		return;
	}

	if (GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) != -1) {
		wxBell();
		return;
	}

	CLocalFileData *data = GetData(item);
	if (!data || data->comparison_flags == fill) {
		wxBell();
		return;
	}

	EditLabel(item);
}

void CLocalListView::OnKeyDown(wxKeyEvent& event)
{
#ifdef __WXMAC__
#define CursorModifierKey wxMOD_CMD
#else
#define CursorModifierKey wxMOD_ALT
#endif

	const int code = event.GetKeyCode();
	if (code == WXK_DELETE || code == WXK_NUMPAD_DELETE) {
		if (GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) == -1) {
			wxBell();
			return;
		}

		wxCommandEvent tmp;
		OnMenuDelete(tmp);
	}
	else if (code == WXK_F2) {
		wxCommandEvent tmp;
		OnMenuRename(tmp);
	}
	else if (code == WXK_RIGHT && event.GetModifiers() == CursorModifierKey) {
		wxListEvent evt;
		evt.m_itemIndex = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_FOCUSED);
		OnItemActivated(evt);
	}
	else if (code == WXK_DOWN && event.GetModifiers() == CursorModifierKey) {
		wxCommandEvent cmdEvent;
		OnMenuUpload(cmdEvent);
	}
	else if (code == 'N' && event.GetModifiers() == (wxMOD_CONTROL | wxMOD_SHIFT)) {
		wxCommandEvent cmdEvent;
		OnMenuMkdir(cmdEvent);
	}
	else if (code == 'F' && event.GetModifiers() == wxMOD_CONTROL) {
		if (m_parentView) {
			m_parentView->ShowSearchPanel(options_);
		}
	}
	else {
		event.Skip();
	}
}

bool CLocalListView::OnBeginRename(const wxListEvent& event)
{
	if (!m_state.GetLocalDir().IsWriteable()) {
		return false;
	}

	if (event.GetIndex() == 0 && m_hasParent) {
		return false;
	}

	const CLocalFileData * const data = GetData(event.GetIndex());
	if (!data || data->comparison_flags == fill) {
		return false;
	}

	return true;
}

bool CLocalListView::OnAcceptRename(const wxListEvent& event)
{
	const int index = event.GetIndex();
	if (!index && m_hasParent) {
		return false;
	}

	if (event.GetLabel().empty()) {
		return false;
	}

	if (!m_state.GetLocalDir().IsWriteable()) {
		return false;
	}

	CLocalFileData *const data = GetData(event.GetIndex());
	if (!data || data->comparison_flags == fill) {
		return false;
	}

	std::wstring newname = event.GetLabel().ToStdWstring();
#ifdef __WXMSW__
	newname = newname.substr(0, 255);
#endif

	if (newname == data->name) {
		return false;
	}

	if (!RenameFile(this, m_dir.GetPath(), data->name, newname)) {
		return false;
	}

	data->name = newname;
#ifdef __WXMSW__
	data->label.clear();
#endif
	CallAfter([&](){m_state.RefreshLocal();});

	return true;
}

void CLocalListView::ApplyCurrentFilter()
{
	CStateFilterManager const& filter = m_state.GetStateFilterManager();

	if (!filter.HasSameLocalAndRemoteFilters() && IsComparing()) {
		ExitComparisonMode();
	}

	unsigned int min = m_hasParent ? 1 : 0;
	if (m_fileData.size() <= min) {
		return;
	}

	int focusedItem = -1;
	std::wstring focused;
	std::vector<std::wstring> const& selectedNames = RememberSelectedItems(focused, focusedItem);

	if (m_pFilelistStatusBar) {
		m_pFilelistStatusBar->UnselectAll();
	}

	int64_t totalSize{};
	int unknown_sizes = 0;
	int totalFileCount = 0;
	int totalDirCount = 0;
	int hidden = 0;

	m_indexMapping.clear();
	if (m_hasParent) {
		m_indexMapping.push_back(0);
	}
	for (unsigned int i = min; i < m_fileData.size(); ++i) {
		const CLocalFileData& data = m_fileData[i];
		if (data.comparison_flags == fill) {
			continue;
		}
		if (filter.FilenameFiltered(data.name, m_dir.GetPath(), data.is_dir(), data.size, true, data.attributes, data.time)) {
			++hidden;
			continue;
		}

		if (data.is_dir()) {
			++totalDirCount;
		}
		else {
			if (data.size != -1) {
				totalSize += data.size;
			}
			else {
				++unknown_sizes;
			}
			++totalFileCount;
		}

		m_indexMapping.push_back(i);
	}
	SetItemCount(m_indexMapping.size());

	if (m_pFilelistStatusBar) {
		m_pFilelistStatusBar->SetDirectoryContents(totalFileCount, totalDirCount, totalSize, unknown_sizes, hidden);
	}

	SortList(-1, -1, false);

	if (IsComparing()) {
		m_originalIndexMapping.clear();
		RefreshComparison();
	}

	ReselectItems(selectedNames, std::move(focused), focusedItem);

	if (!IsComparing()) {
		RefreshListOnly();
	}
}

std::vector<std::wstring> CLocalListView::RememberSelectedItems(std::wstring& focused, int & focusedItem)
{
	std::vector<std::wstring> selectedNames;
	// Remember which items were selected
#ifndef __WXMSW__
	// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
	if (GetSelectedItemCount())
#endif
	{
		int item = -1;
		for (;;) {
			item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			if (item < 0 || static_cast<size_t>(item) >= m_indexMapping.size()) {
				break;
			}
			const CLocalFileData &data = m_fileData[m_indexMapping[item]];
			if (data.comparison_flags != fill) {
				if (data.is_dir()) {
					selectedNames.push_back(L"d" + data.name);
				}
				else {
					selectedNames.push_back(L"-" + data.name);
				}
			}
			SetSelection(item, false);
		}
	}

	focusedItem = -1;
	int item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_FOCUSED);
	if (item >= 0 && static_cast<size_t>(item) < m_indexMapping.size()) {
		const CLocalFileData &data = m_fileData[m_indexMapping[item]];
		if (data.comparison_flags != fill) {
			focused = data.name;
		}
		focusedItem = item;
	}

	return selectedNames;
}

void CLocalListView::ReselectItems(const std::vector<std::wstring>& selectedNames, std::wstring focused, int focusedItem, bool ensureVisible)
{
	if (!GetItemCount()) {
		return;
	}

	// Reselect previous items if neccessary.
	// Sorting direction did not change. We just have to scan through items once

	if (selectedNames.empty()) {
		if (focused.empty()) {
			return;
		}
		for (unsigned int i = 0; i < m_indexMapping.size(); ++i) {
			const CLocalFileData &data = m_fileData[m_indexMapping[i]];
			if (data.name == focused) {
				SetItemState(i, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
				if (ensureVisible) {
					EnsureVisible(i);
				}
				return;
			}
		}

		if (focusedItem != -1 && GetItemCount() != 0) {
			if (focusedItem >= GetItemCount()) {
				focusedItem = GetItemCount() - 1;
			}
			SetItemState(focusedItem, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
		}
		return;
	}

	int firstSelected = -1;

	int i = -1;
	for (auto const& selectedName : selectedNames) {
		while (++i < (int)m_indexMapping.size()) {
			const CLocalFileData &data = m_fileData[m_indexMapping[i]];
			if (data.name == focused) {
				SetItemState(i, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
				if (ensureVisible) {
					EnsureVisible(i);
				}
				focused.clear();
				focusedItem = -1;
			}
			if (data.is_dir() && selectedName == (_T("d") + data.name)) {
				if (firstSelected == -1) {
					firstSelected = i;
				}
				if (m_pFilelistStatusBar) {
					m_pFilelistStatusBar->SelectDirectory();
				}
				SetSelection(i, true);
				break;
			}
			else if (selectedName == (_T("-") + data.name)) {
				if (firstSelected == -1) {
					firstSelected = i;
				}
				if (m_pFilelistStatusBar) {
					m_pFilelistStatusBar->SelectFile(data.size);
				}
				SetSelection(i, true);
				break;
			}
		}
		if (i == (int)m_indexMapping.size()) {
			break;
		}
	}
	if (!focused.empty()) {
		if (firstSelected != -1) {
			SetItemState(firstSelected, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
		}
		else {
			if (GetItemCount() != 0) {
				if (focusedItem == -1) {
					focusedItem = 0;
				}
				else if (focusedItem >= GetItemCount()) {
					focusedItem = GetItemCount() - 1;
				}
				SetItemState(focusedItem, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
			}
		}
	}
}

void CLocalListView::OnSize(wxSizeEvent& event)
{
	event.Skip();
	if (m_pInfoText) {
		m_pInfoText->Reposition();
	}
}

void CLocalListView::OnStateChange(t_statechange_notifications notification, std::wstring const& data, const void*)
{
	if (notification == STATECHANGE_LOCAL_DIR) {
		DisplayDir(m_state.GetLocalDir());
	}
	else if (notification == STATECHANGE_APPLYFILTER) {
		ApplyCurrentFilter();
	}
	else if (notification == STATECHANGE_SERVER) {
		if (m_windowTinter) {
			m_windowTinter->SetBackgroundTint(m_state.GetSite().m_colour);
		}
		if (m_pInfoText) {
			m_pInfoText->SetBackgroundTint(m_state.GetSite().m_colour);
		}
	}
	else {
		wxASSERT(notification == STATECHANGE_LOCAL_REFRESH_FILE);
		RefreshFile(data);
	}
}

void CLocalListView::SetInfoText(wxString const& text)
{
	if (!m_pInfoText) {
		return;
	}

	if (IsComparing() || text.empty()) {
		m_pInfoText->Hide();
	}
	else {
		m_pInfoText->SetText(text);
		m_pInfoText->Reposition();
		m_pInfoText->Show();
	}
}

void CLocalListView::OnBeginDrag(wxListEvent&)
{
	if (options_.get_int(OPTION_DND_DISABLED) != 0) {
		return;
	}

	long item = -1;
	for (;;) {
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1) {
			break;
		}

		if (!item && m_hasParent) {
			return;
		}
	}

#ifdef __WXMAC__
	// Don't use wxFileDataObject on Mac, crashes on Mojave, wx bug #18232
	CLocalDataObject obj;
#else
	wxFileDataObject obj;
#endif

	CDragDropManager* pDragDropManager = CDragDropManager::Init();
	pDragDropManager->pDragSource = this;
	pDragDropManager->localParent = m_dir;
	pDragDropManager->dragDataObject = &obj;

	auto const& path = m_dir.GetPath();

	bool added = false;

	item = -1;
	for (;;) {
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1) {
			break;
		}

		CLocalFileData *data = GetData(item);
		if (!data) {
			continue;
		}

		if (data->comparison_flags == fill) {
			continue;
		}

		std::wstring name = path + data->name;
		obj.AddFile(name);
		added = true;
	}

	if (!added) {
		pDragDropManager->Release();
		return;
	}

	CLabelEditBlocker b(*this);

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

void CLocalListView::RefreshFile(std::wstring const& file)
{
	CLocalFileData data;

	bool wasLink;
	fz::local_filesys::type type = fz::local_filesys::get_file_info(fz::to_native(m_dir.GetPath() + file), wasLink, &data.size, &data.time, &data.attributes);
	if (type == fz::local_filesys::unknown) {
		return;
	}

	data.name = file;
	data.flags = (type == fz::local_filesys::dir) ? CLocalFileData::flag_dir : 0;
	if (wasLink) {
		data.flags |= CLocalFileData::flag_link;
	}

	CStateFilterManager const& filter = m_state.GetStateFilterManager();
	if (filter.FilenameFiltered(data.name, m_dir.GetPath(), data.is_dir(), data.size, true, data.attributes, data.time)) {
		return;
	}

	CancelLabelEdit();

	// Look if file data already exists
	unsigned int i = 0;
	for (auto iter = m_fileData.begin(); iter != m_fileData.end(); ++iter, ++i) {
		const CLocalFileData& oldData = *iter;
		if (oldData.name != file) {
			continue;
		}

		// Update file list status bar
		if (m_pFilelistStatusBar) {
#ifndef __WXMSW__
			// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
			if (GetSelectedItemCount())
#endif
			{
				int item = -1;
				for (;;) {
					item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
					if (item == -1) {
						break;
					}
					if (m_indexMapping[item] != i) {
						continue;
					}

					if (oldData.is_dir()) {
						m_pFilelistStatusBar->UnselectDirectory();
					}
					else {
						m_pFilelistStatusBar->UnselectFile(oldData.size);
					}
					if (data.is_dir()) {
						m_pFilelistStatusBar->SelectDirectory();
					}
					else {
						m_pFilelistStatusBar->SelectFile(data.size);
					}
					break;
				}
			}

			if (oldData.is_dir()) {
				m_pFilelistStatusBar->RemoveDirectory();
			}
			else {
				m_pFilelistStatusBar->RemoveFile(oldData.size);
			}
			if (data.is_dir()) {
				m_pFilelistStatusBar->AddDirectory();
			}
			else {
				m_pFilelistStatusBar->AddFile(data.size);
			}
		}

		// Update the data
		data.fileType = oldData.fileType;

		*iter = data;
		if (IsComparing()) {
			// Sort order doesn't change
			RefreshComparison();
		}
		else {
			if (m_sortColumn) {
				SortList();
			}
			RefreshListOnly(false);
		}
		return;
	}

	if (data.is_dir()) {
		m_pFilelistStatusBar->AddDirectory();
	}
	else {
		m_pFilelistStatusBar->AddFile(data.size);
	}

	std::wstring focused;
	int focusedItem = -1;
	std::vector<std::wstring> selectedNames;
	if (IsComparing()) {
		selectedNames = RememberSelectedItems(focused, focusedItem);
		if (!m_originalIndexMapping.empty()) {
			m_indexMapping.clear();
			m_originalIndexMapping.swap(m_indexMapping);
		}
	}

	// Insert new entry
	int index = m_fileData.size();
	m_fileData.push_back(data);

	// Find correct position in index mapping
	std::vector<unsigned int>::iterator start = m_indexMapping.begin();
	if (m_hasParent) {
		++start;
	}
	auto& compare = GetSortComparisonObject();
	std::vector<unsigned int>::iterator insertPos = std::lower_bound(start, m_indexMapping.end(), index, SortPredicate(compare));

	const int item = insertPos - m_indexMapping.begin();
	m_indexMapping.insert(insertPos, index);

	if (!IsComparing()) {
		SetItemCount(m_indexMapping.size());

		// Move selections
		int prevState = 0;
		for (unsigned int j = item; j < m_indexMapping.size(); ++j) {
			int state = GetItemState(j, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
			if (state != prevState) {
				SetItemState(j, prevState, wxLIST_STATE_FOCUSED);
				SetSelection(j, (prevState & wxLIST_STATE_SELECTED) != 0);
				prevState = state;
			}
		}
		RefreshListOnly();
	}
	else {
		RefreshComparison();
		if (m_pFilelistStatusBar) {
			m_pFilelistStatusBar->UnselectAll();
		}
		ReselectItems(selectedNames, focused, focusedItem);
	}
}

wxListItemAttr* CLocalListView::OnGetItemAttr(long item) const
{
	CLocalListView *pThis = const_cast<CLocalListView *>(this);
	const CLocalFileData* const data = pThis->GetData((unsigned int)item);

	if (!data) {
		return 0;
	}

#ifndef __WXMSW__
	if (item == m_dropTarget) {
		return &pThis->m_dropHighlightAttribute;
	}
#endif

	switch (data->comparison_flags)
	{
	case different:
		return &pThis->m_comparisonBackgrounds[0];
	case lonely:
		return &pThis->m_comparisonBackgrounds[1];
	case newer:
		return &pThis->m_comparisonBackgrounds[2];
	default:
		return 0;
	}
}

void CLocalListView::StartComparison()
{
	if (m_sortDirection || m_sortColumn != 0) {
		wxASSERT(m_originalIndexMapping.empty());
		SortList(0, 0);
	}

	ComparisonRememberSelections();

	if (m_originalIndexMapping.empty()) {
		m_originalIndexMapping.swap(m_indexMapping);
	}
	else {
		m_indexMapping.clear();
	}

	m_comparisonIndex = -1;

	if (m_fileData.empty() || m_fileData.back().comparison_flags != fill) {
		CLocalFileData data;
		data.flags = 0;
		data.icon = -1;
		data.size = -1;
		data.attributes = {};
		data.comparison_flags = fill;
		m_fileData.push_back(data);
	}
}

bool CLocalListView::get_next_file(std::wstring_view & name, std::wstring &, bool& dir, int64_t& size, fz::datetime& date)
{
	if (++m_comparisonIndex >= (int)m_originalIndexMapping.size()) {
		return false;
	}

	const unsigned int index = m_originalIndexMapping[m_comparisonIndex];
	if (index >= m_fileData.size()) {
		return false;
	}

	CLocalFileData const& data = m_fileData[index];

	name = data.name;
	dir = data.is_dir();
	size = data.size;
	date = data.time;

	return true;
}

void CLocalListView::FinishComparison()
{
	SetInfoText(wxString());

	SetItemCount(m_indexMapping.size());

	ComparisonRestoreSelections();

	RefreshListOnly();

	CComparableListing* pOther = GetOther();
	if (pOther) {
		pOther->ScrollTopItem(GetTopItem());
	}
}

bool CLocalListView::CanStartComparison()
{
	return true;
}

wxString CLocalListView::GetItemText(int item, unsigned int column)
{
	CLocalFileData *data = GetData(item);
	if (!data) {
		return wxString();
	}

	if (!column) {
#ifdef __WXMSW__
		return data->label ? *data->label : data->name;
#else
		return data->name;
#endif
	}
	else if (column == 1) {
		if (data->size < 0) {
			return wxString();
		}
		else {
			return SizeFormatter(options_).Format(data->size, SizeFormatPurpose::normal);
		}
	}
	else if (column == 2) {
		if (!item && m_hasParent) {
			return wxString();
		}

		if (data->comparison_flags == fill) {
			return wxString();
		}

		if (data->fileType.empty()) {
			data->fileType = GetType(data->name, data->is_dir(), m_dir.GetPath());
		}

		return data->fileType;
	}
	else if (column == 3) {
		return time_formatter_.Format(data->time);
	}
	return wxString();
}

void CLocalListView::OnMenuEdit(wxCommandEvent&)
{
	Site site;
	CServerPath path;

	if (!m_state.GetSite()) {
		if (options_.get_int(OPTION_EDIT_TRACK_LOCAL)) {
			wxMessageBoxEx(_("Cannot edit file, not connected to any server."), _("Editing failed"), wxICON_EXCLAMATION);
			return;
		}
	}
	else {
		site = m_state.GetSite();

		path = m_state.GetRemotePath();
		if (path.empty()) {
			wxMessageBoxEx(_("Cannot edit file, remote path unknown."), _("Editing failed"), wxICON_EXCLAMATION);
			return;
		}
	}

	std::vector<CEditHandler::FileData> selected_item;

	long item = -1;
	while ((item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		if (!item && m_hasParent) {
			wxBell();
			return;
		}

		const CLocalFileData *data = GetData(item);
		if (!data) {
			continue;
		}

		if (data->is_dir()) {
			wxBell();
			return;
		}

		if (data->comparison_flags == fill) {
			continue;
		}

		selected_item.push_back({m_dir.GetPath() + data->name, data->size});
	}

	if (!edit_handler_) {
		wxBell();
		return;
	}
	edit_handler_->Edit(CEditHandler::local, selected_item, path, site, this);
}

void CLocalListView::OnMenuOpen(wxCommandEvent&)
{
	long item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (item == -1) {
		OpenInFileManager(m_dir.GetPath());
		return;
	}

	std::list<CLocalFileData> selected_item_list;

	item = -1;
	while ((item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		if (!item && m_hasParent) {
			wxBell();
			return;
		}

		const CLocalFileData *data = GetData(item);
		if (!data) {
			continue;
		}

		if (data->comparison_flags == fill) {
			continue;
		}

		selected_item_list.push_back(*data);
	}

	if (!edit_handler_) {
		wxBell();
		return;
	}

	if (selected_item_list.empty()) {
		wxBell();
		return;
	}

	if (selected_item_list.size() > 10) {
		CConditionalDialog dlg(this, CConditionalDialog::many_selected_for_edit, CConditionalDialog::yesno, options_);
		dlg.SetTitle(_("Confirmation needed"));
		dlg.AddText(_("You have selected more than 10 files or directories to open, do you really want to continue?"));

		if (!dlg.Run()) {
			return;
		}
	}

	for (auto const& data : selected_item_list) {
		if (data.is_dir()) {
			CLocalPath path(m_dir);
			if (!path.ChangePath(data.name)) {
				wxBell();
				continue;
			}

			OpenInFileManager(path.GetPath());
			continue;
		}


		wxFileName fn(m_dir.GetPath(), data.name);
		if (wxLaunchDefaultApplication(fn.GetFullPath(), 0)) {
			continue;
		}
		auto cmd_with_args = GetSystemAssociation(fn.GetFullPath().ToStdWstring());
		if (cmd_with_args.empty()) {
			cmd_with_args = edit_handler_->GetAssociation(fn.GetFullPath().ToStdWstring());
		}
		if (cmd_with_args.empty()) {
			wxMessageBoxEx(wxString::Format(_("The file '%s' could not be opened:\nNo program has been associated on your system with this file type."), fn.GetFullPath()), _("Opening failed"), wxICON_EXCLAMATION);
			continue;
		}
		if (!ProgramExists(cmd_with_args.front())) {
			wxString msg = wxString::Format(_("The file '%s' cannot be opened:\nThe associated program (%s) could not be found.\nPlease check your filetype associations."), fn.GetFullPath(), cmd_with_args.front());
			wxMessageBoxEx(msg, _("Cannot edit file"), wxICON_EXCLAMATION);
			continue;
		}

		if (fz::spawn_detached_process(AssociationToCommand(cmd_with_args, fn.GetFullPath().ToStdWstring()))) {
			continue;
		}
		wxMessageBoxEx(wxString::Format(_("The file '%s' could not be opened:\nThe associated command failed"), fn.GetFullPath()), _("Opening failed"), wxICON_EXCLAMATION);
	}
}

bool CLocalListView::ItemIsDir(int index) const
{
	if (index < 0 || static_cast<size_t>(index) >= m_fileData.size()) {
		return false;
	}
	return m_fileData[index].is_dir();
}

int64_t CLocalListView::ItemGetSize(int index) const
{
	if (index < 0 || static_cast<size_t>(index) >= m_fileData.size()) {
		return -1;
	}
	return m_fileData[index].size;
}

#ifdef __WXMSW__

void CLocalListView::OnVolumesEnumerated(wxCommandEvent& event)
{
	if (!volumeEnumeratorThread_) {
		return;
	}

	std::vector<CVolumeDescriptionEnumeratorThread::t_VolumeInfo> volumeInfo = volumeEnumeratorThread_->GetVolumes();

	if (event.GetEventType() == fzEVT_VOLUMESENUMERATED) {
		volumeEnumeratorThread_.reset();
	}

	if (m_dir.GetPath() != _T("\\")) {
		return;
	}

	for (auto const& info : volumeInfo) {
		std::wstring const& drive = info.volume;

		unsigned int item, index;
		for (item = m_hasParent ? 1 : 0; item < m_indexMapping.size(); ++item) {
			index = m_indexMapping[item];
			if (m_fileData[index].name == drive || m_fileData[index].name.substr(0, drive.size() + 1) == drive + _T(" ")) {
				break;
			}
		}
		if (item >= m_indexMapping.size()) {
			continue;
		}

		if (!info.volumeName.empty()) {
			m_fileData[index].label = fz::sparse_optional<std::wstring>(drive + _T(" (") + info.volumeName + _T(")"));
		}
		if (info.icon != -1) {
			m_fileData[index].icon = info.icon;
		}

		RefreshItem(item);
	}
}

int CLocalListView::GetOverlayIndex(int item)
{
	CLocalFileData* data = GetData(item);
	if (!data || !data->is_link()) {
		return 0;
	}
	return GetLinkOverlayIndex();
}

#endif

void CLocalListView::OnMenuRefresh(wxCommandEvent&)
{
	m_state.RefreshLocal();
}

void CLocalListView::OnNavigationEvent(bool forward)
{
	if (!forward) {
		if (!m_hasParent) {
			wxBell();
			return;
		}

		std::wstring error;
		if (!m_state.SetLocalDir(_T(".."), &error)) {
			if (!error.empty()) {
				wxMessageBoxEx(error, _("Failed to change directory"), wxICON_INFORMATION);
			}
			else {
				wxBell();
			}
		}
	}
}
