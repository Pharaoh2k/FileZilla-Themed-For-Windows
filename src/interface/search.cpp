#include "filezilla.h"

#define FILELISTCTRL_INCLUDE_TEMPLATE_DEFINITION

#include "search.h"
#include "commandqueue.h"
#include "filelistctrl.h"
#include "file_utils.h"
#include "Options.h"
#include "queue.h"
#include "remote_recursive_operation.h"
#include "textctrlex.h"
#include "timeformatting.h"
#include "window_state_manager.h"
#include "xrc_helper.h"
#include "customheightlistctrl.h"

#include "../commonui/ipcmutex.h"
#include "../commonui/misc.h"

#include "../include/sizeformatting.h"

#include <libfilezilla/process.hpp>
#include <libfilezilla/uri.hpp>
#include <libfilezilla/translate.hpp>

#include <wx/clipbrd.h>
#include <wx/dirdlg.h>
#include <wx/menu.h>
#include <wx/statline.h>

class CRemoteSearchFileData final : public CDirentry
{
public:
	CServerPath path;
};

class CLocalSearchFileData final : public CLocalRecursiveOperation::listing::entry
{
public:
	CLocalPath path;
};

template<>
inline int DoCmpName(CRemoteSearchFileData const& data1, CRemoteSearchFileData const& data2, NameSortMode const nameSortMode)
{
	int res;
	switch (nameSortMode)
	{
	case NameSortMode::case_sensitive:
		res = CFileListCtrlSortBase::CmpCase(data1.name, data2.name);
		break;
	default:
	case NameSortMode::case_insensitive:
		res = CFileListCtrlSortBase::CmpNoCase(data1.name, data2.name);
		break;
	case NameSortMode::natural:
		res = CFileListCtrlSortBase::CmpNatural(data1.name, data2.name);
		break;
	}

	if (!res) {
		res = data1.path.compare_case(data2.path);
	}

	return res;
}

template<>
inline int DoCmpName(CLocalSearchFileData const& data1, CLocalSearchFileData const& data2, NameSortMode const nameSortMode)
{
	int res;
	switch (nameSortMode)
	{
	case NameSortMode::case_sensitive:
		res = CFileListCtrlSortBase::CmpCase(data1.name, data2.name);
		break;
	default:
	case NameSortMode::case_insensitive:
		res = CFileListCtrlSortBase::CmpNoCase(data1.name, data2.name);
		break;
	case NameSortMode::natural:
		res = CFileListCtrlSortBase::CmpNatural(data1.name, data2.name);
		break;
	}

	if (!res) {
		res = data1.path.compare_case(data2.path);
	}

	return res;
}

class CSearchDialogFileList final : public CFileListCtrl<CGenericFileData>
{
	friend class CSearchDialog;
	friend class CSearchSortType;

public:
	CSearchDialogFileList(CSearchDialog* pParent, CQueueView* pQueue, COptionsBase & options, TimeFormatter & time_formatter);

	void clear();
	void set_mode(CSearchDialog::search_mode mode);

protected:
	virtual bool ItemIsDir(int index) const;

	virtual int64_t ItemGetSize(int index) const;

	virtual void UpdateSortComparisonObject() override;

	CSearchDialog *m_searchDialog;

	virtual wxString GetItemText(int item, unsigned int column);
	virtual int OnGetItemImage(long item) const;
	virtual wxListItemAttr* OnGetItemAttr(long item) const;

#ifdef __WXMSW__
	virtual int GetOverlayIndex(int item);
#endif

private:
	virtual bool CanStartComparison() { return m_canStartComparison; }
	virtual void StartComparison() override;
	virtual bool get_next_file(std::wstring_view& name, std::wstring& path, bool& dir, int64_t& size, fz::datetime& date) override;
	virtual void FinishComparison();

	int m_dirIcon;

	bool get_next_file(std::vector<CLocalSearchFileData> const& fileData, const unsigned int index, std::wstring_view& name, std::wstring & path, bool& dir, int64_t& size, fz::datetime& date);
	bool get_next_file(std::vector<CRemoteSearchFileData> const& fileData, const unsigned int index, std::wstring_view& name, std::wstring & path, bool& dir, int64_t& size, fz::datetime& date);

	CSearchDialog::search_mode mode_{};

	std::vector<CLocalSearchFileData> localFileData_;
	std::vector<CRemoteSearchFileData> remoteFileData_;

	bool m_canStartComparison{};

	DECLARE_EVENT_TABLE()
	void OnColumnClicked(wxListEvent &event);
};

// Search dialog file list
// -----------------------
BEGIN_EVENT_TABLE(CSearchDialogFileList, CFileListCtrl<CGenericFileData>)
EVT_LIST_COL_CLICK(wxID_ANY, CSearchDialogFileList::OnColumnClicked)
END_EVENT_TABLE()

CSearchDialogFileList::CSearchDialogFileList(CSearchDialog* pParent, CQueueView* pQueue, COptionsBase & options, TimeFormatter & time_formatter)
	: CFileListCtrl<CGenericFileData>(pParent, pQueue, options, time_formatter,  true),
	m_searchDialog(pParent)
{
	m_hasParent = false;

	SetImageList(GetSystemImageList(), wxIMAGE_LIST_SMALL);

	m_dirIcon = GetIconIndex(iconType::dir);

	InitHeaderSortImageList();

	const unsigned long widths[7] = { 130, 130, 75, 80, 120, 80, 80 };

	AddColumn(_("Filename"), wxLIST_FORMAT_LEFT, widths[0]);
	AddColumn(_("Path"), wxLIST_FORMAT_LEFT, widths[1]);
	AddColumn(_("Filesize"), wxLIST_FORMAT_RIGHT, widths[2]);
	AddColumn(_("Filetype"), wxLIST_FORMAT_LEFT, widths[3]);
	AddColumn(_("Last modified"), wxLIST_FORMAT_LEFT, widths[4]);
	AddColumn(_("Permissions"), wxLIST_FORMAT_LEFT, widths[5]);
	AddColumn(_("Owner/Group"), wxLIST_FORMAT_LEFT, widths[6]);
	LoadColumnSettings(options_, OPTION_SEARCH_COLUMN_WIDTHS, OPTION_SEARCH_COLUMN_SHOWN, OPTION_SEARCH_COLUMN_ORDER);

	InitSort(OPTION_SEARCH_SORTORDER);
}

void CSearchDialogFileList::clear()
{
	ClearSelection();
	m_indexMapping.clear();
	m_fileData.clear();
	localFileData_.clear();
	remoteFileData_.clear();
	SetItemCount(0);
	RefreshListOnly(true);
	if (auto* sbar = GetFilelistStatusBar()) {
		sbar->Clear();
	}
	m_canStartComparison = false;
}

void CSearchDialogFileList::set_mode(CSearchDialog::search_mode mode)
{
	mode_ = mode;
	UpdateSortComparisonObject();
}

bool CSearchDialogFileList::ItemIsDir(int index) const
{
	if (mode_ == CSearchDialog::search_mode::local) {
		return localFileData_[index].is_dir();
	}
	else {
		return remoteFileData_[index].is_dir();
	}
}

int64_t CSearchDialogFileList::ItemGetSize(int index) const
{
	if (mode_ == CSearchDialog::search_mode::local) {
		return localFileData_[index].size;
	}
	else {
		return remoteFileData_[index].size;
	}
}

void CSearchDialogFileList::UpdateSortComparisonObject()
{
	CFileListCtrlSortBase::DirSortMode dirSortMode = GetDirSortMode();
	NameSortMode nameSortMode = GetNameSortMode();

	if (mode_ == CSearchDialog::search_mode::local) {
		if (!m_sortDirection) {
			if (m_sortColumn == 1) {
				sortComparisonObject_ = std::make_unique<CFileListCtrlSortPath<std::vector<CLocalSearchFileData>, CGenericFileData>>(localFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else if (m_sortColumn == 2) {
				sortComparisonObject_ = std::make_unique<CFileListCtrlSortSize<std::vector<CLocalSearchFileData>, CGenericFileData>>(localFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else if (m_sortColumn == 3) {
				sortComparisonObject_ = std::make_unique<CFileListCtrlSortType<std::vector<CLocalSearchFileData>, CGenericFileData>>(localFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else if (m_sortColumn == 4) {
				sortComparisonObject_ = std::make_unique<CFileListCtrlSortTime<std::vector<CLocalSearchFileData>, CGenericFileData>>(localFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else {
				sortComparisonObject_ = std::make_unique<CFileListCtrlSortNamePath<std::vector<CLocalSearchFileData>, CGenericFileData>>(localFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
		}
		else {
			if (m_sortColumn == 1) {
				sortComparisonObject_ = std::make_unique<CReverseSort<CFileListCtrlSortPath<std::vector<CLocalSearchFileData>, CGenericFileData>, CGenericFileData>>(localFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else if (m_sortColumn == 2) {
				sortComparisonObject_ = std::make_unique<CReverseSort<CFileListCtrlSortSize<std::vector<CLocalSearchFileData>, CGenericFileData>, CGenericFileData>>(localFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else if (m_sortColumn == 3) {
				sortComparisonObject_ = std::make_unique<CReverseSort<CFileListCtrlSortType<std::vector<CLocalSearchFileData>, CGenericFileData>, CGenericFileData>>(localFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else if (m_sortColumn == 4) {
				sortComparisonObject_ = std::make_unique<CReverseSort<CFileListCtrlSortTime<std::vector<CLocalSearchFileData>, CGenericFileData>, CGenericFileData>>(localFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else {
				sortComparisonObject_ = std::make_unique<CReverseSort<CFileListCtrlSortNamePath<std::vector<CLocalSearchFileData>, CGenericFileData>, CGenericFileData>>(localFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
		}
	}
	else {
		if (!m_sortDirection) {
			if (m_sortColumn == 1) {
				sortComparisonObject_ = std::make_unique<CFileListCtrlSortPath<std::vector<CRemoteSearchFileData>, CGenericFileData>>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else if (m_sortColumn == 2) {
				sortComparisonObject_ = std::make_unique<CFileListCtrlSortSize<std::vector<CRemoteSearchFileData>, CGenericFileData>>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else if (m_sortColumn == 3) {
				sortComparisonObject_ = std::make_unique<CFileListCtrlSortType<std::vector<CRemoteSearchFileData>, CGenericFileData>>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else if (m_sortColumn == 4) {
				sortComparisonObject_ = std::make_unique<CFileListCtrlSortTime<std::vector<CRemoteSearchFileData>, CGenericFileData>>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else if (m_sortColumn == 5) {
				sortComparisonObject_ = std::make_unique<CFileListCtrlSortPermissions<std::vector<CRemoteSearchFileData>, CGenericFileData>>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else if (m_sortColumn == 6) {
				sortComparisonObject_ = std::make_unique<CFileListCtrlSortOwnerGroup<std::vector<CRemoteSearchFileData>, CGenericFileData>>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else {
				sortComparisonObject_ = std::make_unique<CFileListCtrlSortNamePath<std::vector<CRemoteSearchFileData>, CGenericFileData>>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
		}
		else {
			if (m_sortColumn == 1) {
				sortComparisonObject_ = std::make_unique<CReverseSort<CFileListCtrlSortPath<std::vector<CRemoteSearchFileData>, CGenericFileData>, CGenericFileData>>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else if (m_sortColumn == 2) {
				sortComparisonObject_ = std::make_unique<CReverseSort<CFileListCtrlSortSize<std::vector<CRemoteSearchFileData>, CGenericFileData>, CGenericFileData>>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else if (m_sortColumn == 3) {
				sortComparisonObject_ = std::make_unique<CReverseSort<CFileListCtrlSortType<std::vector<CRemoteSearchFileData>, CGenericFileData>, CGenericFileData>>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else if (m_sortColumn == 4) {
				sortComparisonObject_ = std::make_unique<CReverseSort<CFileListCtrlSortTime<std::vector<CRemoteSearchFileData>, CGenericFileData>, CGenericFileData>>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else if (m_sortColumn == 5) {
				sortComparisonObject_ = std::make_unique<CReverseSort<CFileListCtrlSortPermissions<std::vector<CRemoteSearchFileData>, CGenericFileData>, CGenericFileData>>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else if (m_sortColumn == 6) {
				sortComparisonObject_ = std::make_unique<CReverseSort<CFileListCtrlSortOwnerGroup<std::vector<CRemoteSearchFileData>, CGenericFileData>, CGenericFileData>>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
			else {
				sortComparisonObject_ = std::make_unique<CReverseSort<CFileListCtrlSortNamePath<std::vector<CRemoteSearchFileData>, CGenericFileData>, CGenericFileData>>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this);
			}
		}
	}
}

wxString CSearchDialogFileList::GetItemText(int item, unsigned int column)
{
	if (item < 0 || item >= (int)m_indexMapping.size()) {
		return wxString();
	}
	int index = m_indexMapping[item];

	if (mode_ == CSearchDialog::search_mode::local) {
		CLocalSearchFileData const& entry = localFileData_[index];
		if (!column) {
			return entry.name;
		}
		else if (column == 1) {
			return entry.path.GetPath();
		}
		else if (column == 2) {
			if (entry.is_dir() || entry.size < 0) {
				return wxString();
			}
			else {
				return SizeFormatter(options_).Format(entry.size, SizeFormatPurpose::normal);
			}
		}
		else if (column == 3) {
			auto & data = m_fileData[index];

			if (data.comparison_flags == fill) {
				return wxString();
			}

			if (data.fileType.empty()) {
				data.fileType = GetType(entry.name, entry.is_dir());
			}

			return data.fileType;
		}
		else if (column == 4) {
			return time_formatter_.Format(entry.time);
		}
	}
	else {
		CRemoteSearchFileData const& entry = remoteFileData_[index];
		if (!column) {
			return entry.name;
		}
		else if (column == 1) {
			return entry.path.GetPath();
		}
		else if (column == 2) {
			if (entry.is_dir() || entry.size < 0) {
				return wxString();
			}
			else {
				return SizeFormatter(options_).Format(entry.size, SizeFormatPurpose::normal);
			}
		}
		else if (column == 3) {
			auto & data = m_fileData[index];

			if (data.comparison_flags == fill) {
				return wxString();
			}

			if (data.fileType.empty()) {
				if (entry.path.GetType() == VMS) {
					data.fileType = GetType(StripVMSRevision(entry.name), entry.is_dir());
				}
				else {
					data.fileType = GetType(entry.name, entry.is_dir());
				}
			}

			return data.fileType;
		}
		else if (column == 4) {
			return time_formatter_.Format(entry.time);
		}
		else if (column == 5) {
			return *entry.permissions;
		}
		else if (column == 6) {
			return *entry.ownerGroup;
		}
	}
	return wxString();
}

int CSearchDialogFileList::OnGetItemImage(long item) const
{
	CSearchDialogFileList *pThis = const_cast<CSearchDialogFileList *>(this);
	if (item < 0 || item >= (int)m_indexMapping.size()) {
		return -1;
	}
	unsigned int index = m_indexMapping[item];
	if (index >= pThis->m_fileData.size()) {
		return -1;
	}

	int &icon = pThis->m_fileData[index].icon;

	if (icon != -2) {
		return icon;
	}

	if (mode_ == CSearchDialog::search_mode::local) {
		if (index >= localFileData_.size()) {
			return -1;
		}
		auto const& file = localFileData_[index];
		icon = pThis->GetIconIndex(iconType::file, file.path.GetPath() + file.name, true, file.is_link());
	}
	else {
		if (index >= remoteFileData_.size()) {
			return -1;
		}
		auto const& file = remoteFileData_[index];
		icon = pThis->GetIconIndex(iconType::file, file.name, false, file.is_link());
	}

	return icon;
}

wxListItemAttr* CSearchDialogFileList::OnGetItemAttr(long item) const
{
	CSearchDialogFileList *pThis = const_cast<CSearchDialogFileList *>(this);

	const unsigned int index = m_indexMapping[item];
	if (index >= m_fileData.size()) {
		return 0;
	}

	switch (m_fileData[index].comparison_flags)
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

void CSearchDialogFileList::StartComparison()
{
	ComparisonRememberSelections();

	if (m_originalIndexMapping.empty()) {
		m_originalIndexMapping.swap(m_indexMapping);
	}
	else {
		m_indexMapping.clear();
	}

	m_comparisonIndex = -1;

	if (m_fileData.empty() || m_fileData.back().comparison_flags != fill) {
		CGenericFileData data;
		data.icon = -1;
		data.comparison_flags = fill;
		data.fileType.clear();
		m_fileData.push_back(data);

		if (mode_ == CSearchDialog::search_mode::local) {
			CLocalSearchFileData localData;
			localData.flags = 0;
			localData.size = -1;
			localFileData_.push_back(localData);
		}
		else {
			CRemoteSearchFileData remoteData;
			remoteFileData_.push_back(remoteData);
		}
	}
}

bool CSearchDialogFileList::get_next_file(std::vector<CLocalSearchFileData> const& fileData, const unsigned int index, std::wstring_view & name, std::wstring & path, bool& dir, int64_t& size, fz::datetime& date)
{
	if (index >= fileData.size()) {
		return false;
	}

	auto const& data = fileData[index];

	name = data.name;
	path.clear();

	CLocalPath const& rootPath = m_searchDialog->m_local_search_root;
	CLocalPath dataPath = data.path;
	if (dataPath.IsSubdirOf(rootPath)) {
		std::vector<std::wstring> segments;
		do {
			segments.push_back(dataPath.GetLastSegment());
			dataPath.MakeParent();
		}
		while (dataPath != rootPath);
		for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
			path += *it;
			path += L"\x1d";
		}
		path.pop_back();
	}

	dir = data.is_dir();
	size = data.size;
	date = data.time;

	return true;
}

bool CSearchDialogFileList::get_next_file(std::vector<CRemoteSearchFileData> const& fileData, const unsigned int index, std::wstring_view& name, std::wstring & path, bool& dir, int64_t& size, fz::datetime& date)
{
	if (index >= fileData.size()) {
		return false;
	}

	auto const& data = fileData[index];

	name = data.name;
	path.clear();

	CServerPath const& rootPath = m_searchDialog->m_remote_search_root;
	CServerPath dataPath = data.path;
	if (dataPath.IsSubdirOf(rootPath, false)) {
		std::vector<std::wstring> segments;
		do {
			segments.push_back(dataPath.GetLastSegment());
			dataPath.MakeParent();
		}
		while (dataPath != rootPath);
		for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
			path += *it;
			path += L"\x1d";
		}
		path.pop_back();
	}

	dir = data.is_dir();
	size = data.size;
	date = data.time;

	return true;
}

bool CSearchDialogFileList::get_next_file(std::wstring_view& name, std::wstring& path, bool& dir, int64_t& size, fz::datetime& date)
{
	if (++m_comparisonIndex >= (int)m_originalIndexMapping.size()) {
		return false;
	}
	const unsigned int index = m_originalIndexMapping[m_comparisonIndex];

	if (mode_ == CSearchDialog::search_mode::local) {
		return get_next_file(localFileData_, index, name, path, dir, size, date);
	}
	else {
		return get_next_file(remoteFileData_, index, name, path, dir, size, date);
	}
}

void CSearchDialogFileList::FinishComparison()
{
	SetItemCount(m_indexMapping.size());

	ComparisonRestoreSelections();

	RefreshListOnly();

	CComparableListing* pOther = GetOther();
	if (mode_ == CSearchDialog::search_mode::local && pOther) {
		pOther->ScrollTopItem(GetTopItem());
	}
}

void CSearchDialogFileList::OnColumnClicked(wxListEvent &event)
{
	if (!m_searchDialog->IsIdle()) {
		return;
	}
	CFileListCtrl<CGenericFileData>::OnColumnClicked(event);
}

// Search dialog
// -------------

BEGIN_EVENT_TABLE(CSearchDialog, CFilterConditionsDialog)
EVT_BUTTON(XRCID("ID_START"), CSearchDialog::OnSearch)
EVT_CONTEXT_MENU(CSearchDialog::OnContextMenu)
EVT_MENU(XRCID("ID_MENU_SEARCH_DOWNLOAD"), CSearchDialog::OnDownload)
EVT_MENU(XRCID("ID_MENU_SEARCH_UPLOAD"), CSearchDialog::OnUpload)
EVT_MENU(XRCID("ID_MENU_SEARCH_EDIT"), CSearchDialog::OnEdit)
EVT_MENU(XRCID("ID_MENU_SEARCH_DELETE"), CSearchDialog::OnDeleteLocal)
EVT_MENU(XRCID("ID_MENU_SEARCH_DELETE_REMOTE"), CSearchDialog::OnDeleteRemote)
EVT_MENU(XRCID("ID_MENU_SEARCH_GETURL"), CSearchDialog::OnGetUrl)
EVT_MENU(XRCID("ID_MENU_SEARCH_GETURL_PASSWORD"), CSearchDialog::OnGetUrl)
EVT_MENU(XRCID("ID_MENU_SEARCH_LOCAL_OPEN"), CSearchDialog::OnLocalOpen)
EVT_MENU(XRCID("ID_MENU_SEARCH_REMOTE_OPEN"), CSearchDialog::OnRemoteOpen)
EVT_MENU(XRCID("ID_MENU_SEARCH_FILEMANAGER"), CSearchDialog::OnShowFileManager)
EVT_CHAR_HOOK(CSearchDialog::OnCharHook)
EVT_RADIOBUTTON(XRCID("ID_LOCAL_SEARCH"), CSearchDialog::OnChangeSearchMode)
EVT_RADIOBUTTON(XRCID("ID_REMOTE_SEARCH"), CSearchDialog::OnChangeSearchMode)
EVT_RADIOBUTTON(XRCID("ID_COMPARATIVE_SEARCH"), CSearchDialog::OnChangeSearchMode)
EVT_RADIOBUTTON(XRCID("ID_COMPARE_SIZE"), CSearchDialog::OnChangeCompareOption)
EVT_RADIOBUTTON(XRCID("ID_COMPARE_DATE"), CSearchDialog::OnChangeCompareOption)
EVT_CHECKBOX(XRCID("ID_COMPARE_HIDEIDENTICAL"), CSearchDialog::OnChangeCompareOption)
END_EVENT_TABLE()

CSearchDialog::CSearchDialog(wxWindow* parent, CState& state, CQueueView* pQueue, COptionsBase & options, TimeFormatter & time_formatter, CEditHandler* edit_handler)
	: CStateEventHandler(state)
	, m_parent(parent)
	, m_pQueue(pQueue)
	, options_(options)
	, time_formatter_(time_formatter)
	, edit_handler_(edit_handler)
{
	m_pComparisonManager = new CComparisonManager(state, options);
}

CSearchDialog::~CSearchDialog()
{
	if (m_pWindowStateManager) {
		if (mode_ != search_mode::comparison) {
			m_pWindowStateManager->Remember(OPTION_SEARCH_SIZE);
		}
		delete m_pWindowStateManager;
	}

	Stop();
	delete m_pComparisonManager;
}

bool CSearchDialog::Load()
{
	if (!wxDialogEx::Create(m_parent, -1, _("File search"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)) {
		return false;
	}

	auto const& lay = layout();

	auto main = new wxBoxSizer(wxVERTICAL);
	SetSizer(main);

	auto optionsSizer = lay.createFlex(3);
	optionsSizer->AddGrowableCol(1);
	main->Add(optionsSizer, 0, wxALL | wxGROW, lay.border);

	optionsSizer->Add(new wxStaticText(this, wxID_ANY, _("Search type:")), 0, wxALIGN_CENTRE_VERTICAL);

	auto typesSizer = lay.createFlex(3);

	typesSizer->Add(new wxRadioButton(this, XRCID("ID_LOCAL_SEARCH"), _("&Local search"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP), 0, wxALIGN_CENTRE_VERTICAL);
	typesSizer->Add(new wxRadioButton(this, XRCID("ID_REMOTE_SEARCH"), _("&Remote search")), 0, wxALIGN_CENTRE_VERTICAL);
	typesSizer->Add(new wxRadioButton(this, XRCID("ID_COMPARATIVE_SEARCH"), _("C&omparative search")), 0, wxALIGN_CENTRE_VERTICAL);
	optionsSizer->Add(typesSizer, 0, wxALIGN_CENTRE_VERTICAL);

	optionsSizer->Add(new wxButton(this, XRCID("ID_START"), _("&Search")), 0, wxALIGN_CENTRE_VERTICAL);
	optionsSizer->Add(new wxStaticText(this, XRCID("ID_PATH_LABEL"), _("Search &directory:")), 0, wxALIGN_CENTRE_VERTICAL);
	optionsSizer->Add(new wxTextCtrlEx(this, XRCID("ID_PATH")), 0, wxALIGN_CENTRE_VERTICAL | wxGROW);
	auto stop = new wxButton(this, XRCID("ID_STOP"), _("S&top"));
	stop->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Stop(); });
	optionsSizer->Add(stop, 0, wxALIGN_CENTRE_VERTICAL);
	optionsSizer->Add(new wxStaticText(this, XRCID("ID_REMOTE_PATH_LABEL"), _("Search &directory:")), 0, wxALIGN_CENTRE_VERTICAL);
	optionsSizer->Add(new wxTextCtrlEx(this, XRCID("ID_REMOTE_PATH")), 0, wxALIGN_CENTRE_VERTICAL | wxGROW);
	optionsSizer->AddSpacer(0);
	optionsSizer->Add(new wxStaticText(this, wxID_ANY, _("Search &conditions:")), 0, wxALIGN_CENTRE_VERTICAL);

	wxChoice* matchTypeChoice = new wxChoice(this, XRCID("ID_MATCHTYPE"));
	matchTypeChoice->Append(_("Match all of the following"));
	matchTypeChoice->Append(_("Match any of the following"));
	matchTypeChoice->Append(_("Match none of the following"));
	matchTypeChoice->Append(_("Match not all of the following"));
	matchTypeChoice->SetSelection(0);
	optionsSizer->Add(matchTypeChoice, 0, wxALIGN_CENTRE_VERTICAL);
	optionsSizer->AddSpacer(0);

	auto conditionsSizer = new wxFlexGridSizer(1, 5, 0);
	conditionsSizer->AddGrowableCol(0);
	main->Add(conditionsSizer, 0, wxALL|wxGROW, lay.border);

	conditionsSizer->Add(new wxCustomHeightListCtrl(this, XRCID("ID_CONDITIONS"), wxDefaultPosition, wxSize(350, 120), wxVSCROLL| wxSUNKEN_BORDER | wxTAB_TRAVERSAL), 0, wxGROW);

	auto criteriaSizer = lay.createFlex(3, 1);
	criteriaSizer->Add(new wxCheckBox(this, XRCID("ID_CASE"), _("Conditions are c&ase sensitive")), 0, wxALIGN_CENTRE_VERTICAL);
	criteriaSizer->Add(new wxCheckBox(this, XRCID("ID_FIND_FILES"), _("Find &files")), 0, wxALIGN_CENTRE_VERTICAL);
	criteriaSizer->Add(new wxCheckBox(this, XRCID("ID_FIND_DIRS"), _("Find d&irectories")), 0, wxALIGN_CENTRE_VERTICAL);
	conditionsSizer->Add(criteriaSizer, 0);

	auto compareSizer = lay.createFlex(4, 1);
	compareSizer->Add(new wxStaticText(this, XRCID("ID_COMPARE_LABEL"), _("Compare:")), 0, wxALIGN_CENTRE_VERTICAL);
	compareSizer->Add(new wxRadioButton(this, XRCID("ID_COMPARE_SIZE"), _("Fil&e sizes"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP), 0, wxALIGN_CENTRE_VERTICAL);
	compareSizer->Add(new wxRadioButton(this, XRCID("ID_COMPARE_DATE"), _("Modification tim&e")), 0, wxALIGN_CENTRE_VERTICAL);
	compareSizer->Add(new wxCheckBox(this, XRCID("ID_COMPARE_HIDEIDENTICAL"), _("&Hide identical files")), 0, wxALIGN_CENTRE_VERTICAL);
	conditionsSizer->Add(compareSizer, 0);

	main->Add(new wxStaticLine(this), 0, wxALL|wxGROW, lay.border);
	main->Add(new wxStaticText(this, XRCID("ID_RESULTS_LABEL"), _("Results:")), 0, wxLEFT|wxRIGHT, lay.border);

	auto resultSizer = lay.createFlex(2, 3);
	resultSizer->AddGrowableCol(0);
	resultSizer->AddGrowableCol(1);
	resultSizer->AddGrowableRow(1);
	main->Add(resultSizer, 1, wxALL|wxGROW, lay.border);

	resultSizer->Add(new wxStaticText(this, XRCID("ID_LOCAL_RESULTS_LABEL"), _("Local results:")));
	resultSizer->Add(new wxStaticText(this, XRCID("ID_REMOTE_RESULTS_LABEL"), _("Remote results:")));
	resultSizer->Add(new wxListCtrl(this, XRCID("ID_RESULTS"), wxDefaultPosition, wxDefaultSize, wxLC_REPORT), 0, wxGROW);
	resultSizer->Add(new wxListCtrl(this, XRCID("ID_REMOTE_RESULTS"), wxDefaultPosition, wxDefaultSize, wxLC_REPORT), 0, wxGROW);

	CFilelistStatusBar* pStatusBar = new CFilelistStatusBar(this, options_);
	pStatusBar->SetEmptyString(_("No search results"));
	pStatusBar->SetConnected(true);

	resultSizer->Add(pStatusBar, 0, wxGROW, lay.border);

	m_remoteStatusBar = new CFilelistStatusBar(this, options_);
	m_remoteStatusBar->SetEmptyString(_("No search results"));
	m_remoteStatusBar->SetConnected(true);

	resultSizer->Add(m_remoteStatusBar, 0, wxGROW, lay.border);

	if (!CreateListControl(filter_name | filter_size | filter_path | filter_date)) {
		return false;
	}

	m_results = new CSearchDialogFileList(this, 0, options_, time_formatter_);
	ReplaceControl(XRCCTRL(*this, "ID_RESULTS", wxWindow), m_results);
	m_results->SetFilelistStatusBar(pStatusBar);

	m_remoteResults = new CSearchDialogFileList(this, 0, options_, time_formatter_);
	ReplaceControl(XRCCTRL(*this, "ID_REMOTE_RESULTS", wxWindow), m_remoteResults);
	m_remoteResults->SetFilelistStatusBar(m_remoteStatusBar);
	m_remoteResults->Show(false);
	m_remoteStatusBar->Show(false);

	if (m_state.IsRemoteConnected()) {
		mode_ = search_mode::remote;
	}

	SetCtrlState();

	const int mode = options_.get_int(OPTION_COMPARISONMODE);
	if (mode == 0) {
		xrc_call(*this, "ID_COMPARE_SIZE", &wxRadioButton::SetValue, true);
	}
	else {
		xrc_call(*this, "ID_COMPARE_DATE", &wxRadioButton::SetValue, true);
	}
	xrc_call(*this, "ID_COMPARE_HIDEIDENTICAL", &wxCheckBox::SetValue, options_.get_int(OPTION_COMPARE_HIDEIDENTICAL) != 0);

	LoadConditions();
	EditFilter(m_search_filter);

	xrc_call(*this, "ID_REMOTE_SEARCH", &wxRadioButton::SetValue, true);
	xrc_call(*this, "ID_START", &wxButton::SetDefault);
	xrc_call(*this, "ID_PATH", &wxTextCtrl::SetFocus);

	xrc_call(*this, "ID_CONDITIONS", &wxCustomHeightListCtrl::SetBackgroundColour, wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

	xrc_call(*this, "ID_CASE", &wxCheckBox::SetValue, m_search_filter.matchCase);
	xrc_call(*this, "ID_FIND_FILES", &wxCheckBox::SetValue, m_search_filter.filterFiles);
	xrc_call(*this, "ID_FIND_DIRS", &wxCheckBox::SetValue, m_search_filter.filterDirs);

	Layout();

	SetMinClientSize(GetSizer()->GetMinSize());

	m_pWindowStateManager = new CWindowStateManager(this, options_);
	m_pWindowStateManager->Restore(OPTION_SEARCH_SIZE, wxSize(1750, 500));

	if (mode_ == search_mode::remote) {
		xrc_call(*this, "ID_REMOTE_SEARCH", &wxRadioButton::SetValue, true);

		CServerPath const path = m_state.GetRemotePath();
		if (!path.empty()) {
			xrc_call(*this, "ID_PATH", &wxTextCtrl::ChangeValue, path.GetPath());
		}
	}
	else {
		xrc_call(*this, "ID_LOCAL_SEARCH", &wxRadioButton::SetValue, true);

		CLocalPath const path = m_state.GetLocalDir();
		xrc_call(*this, "ID_PATH", &wxTextCtrl::ChangeValue, path.GetPath());
	}

	return true;
}

void CSearchDialog::Run()
{
	m_state.RegisterHandler(this, STATECHANGE_REMOTE_DIR_OTHER, m_state.GetRemoteRecursiveOperation());
	m_state.RegisterHandler(this, STATECHANGE_REMOTE_IDLE, m_state.GetRemoteRecursiveOperation());
	m_state.RegisterHandler(this, STATECHANGE_LOCAL_RECURSION_LISTING);
	m_state.RegisterHandler(this, STATECHANGE_LOCAL_RECURSION_STATUS);

	ShowModal();

	Stop();

	SaveConditions();

	m_state.UnregisterHandler(this, STATECHANGE_LOCAL_RECURSION_STATUS);
	m_state.UnregisterHandler(this, STATECHANGE_LOCAL_RECURSION_LISTING);
	m_state.UnregisterHandler(this, STATECHANGE_REMOTE_IDLE);
	m_state.UnregisterHandler(this, STATECHANGE_REMOTE_DIR_OTHER);
}

void CSearchDialog::OnStateChange(t_statechange_notifications notification, std::wstring const&, const void* data2)
{
	if (!searching_) {
		SetCtrlState();
		return;
	}

	if (notification == STATECHANGE_REMOTE_DIR_OTHER && data2) {
		if (searching_ && mode_ != search_mode::local) {
			auto recursiveOperation = m_state.GetRemoteRecursiveOperation();
			if (recursiveOperation && recursiveOperation->GetOperationMode() == recursive_operation::recursive_list) {
				std::shared_ptr<CDirectoryListing> const& listing = *reinterpret_cast<std::shared_ptr<CDirectoryListing> const*>(data2);

				ProcessDirectoryListing(listing);
			}
		}
	}
	else if (notification == STATECHANGE_REMOTE_IDLE) {
		if (mode_ != search_mode::local) {
			if (!m_state.IsRemoteIdle()) {
				return;
			}

			if (mode_ == search_mode::comparison) {
				m_remoteResults->m_canStartComparison = true;
				m_remoteResults->m_originalIndexMapping.clear();
				if (!m_state.IsLocalIdle()) {
					return;
				}
				m_pComparisonManager->CompareListings();
			}
		}
		searching_ = false;
		SetCtrlState();
	}
	else if (notification == STATECHANGE_LOCAL_RECURSION_LISTING) {
		if (mode_ != search_mode::remote) {
			auto listing = reinterpret_cast<CLocalRecursiveOperation::listing const*>(data2);

			ProcessDirectoryListing(*listing);
		}
	}
	else if (notification == STATECHANGE_LOCAL_RECURSION_STATUS) {
		if (mode_ != search_mode::remote) {
			if (!m_state.IsLocalIdle()) {
				return;
			}

			if (mode_ == search_mode::comparison) {
				m_results->m_canStartComparison = true;
				m_results->m_originalIndexMapping.clear();
				if (!m_state.IsRemoteIdle()) {
					return;
				}
				m_pComparisonManager->CompareListings();
			}

			searching_ = false;
			SetCtrlState();
		}
	}
}

void CSearchDialog::ProcessDirectoryListing(std::shared_ptr<CDirectoryListing> const& listing)
{
	if (!searching_ || mode_ == search_mode::local) {
		return;
	}

	if (!listing || listing->failed()) {
		return;
	}

	// Do not process same directory multiple times
	if (!m_visited.insert(listing->path).second) {
		return;
	}

	CSearchDialogFileList *results = m_results;
	if (mode_ == search_mode::comparison) {
		results = m_remoteResults;
	}

	int old_count = results->m_fileData.size();
	int added_count = 0;

	std::wstring const path = listing->path.GetPath();

	bool const has_selections = results->GetSelectedItemCount() != 0;

	std::vector<int> added_indexes;
	if (has_selections) {
		added_indexes.reserve(listing->size());
	}

	auto & compare = results->GetSortComparisonObject();
	for (size_t i = 0; i < listing->size(); ++i) {
		CDirentry const& entry = (*listing)[i];

		if (!CFilterManager::FilenameFilteredByFilter(m_search_filter, entry.name, path, entry.is_dir(), entry.size, 0, entry.time)) {
			continue;
		}

		CRemoteSearchFileData remoteData;
		static_cast<CDirentry&>(remoteData) = entry;
		remoteData.path = listing->path;

		CGenericFileData data;
		if (entry.is_dir()) {
			data.icon = m_results->m_dirIcon;
#ifndef __WXMSW__
			if (entry.is_link()) {
				data.icon += 3;
			}
#endif
		}
		else {
			data.icon = -2;
		}

		results->remoteFileData_.emplace_back(std::move(remoteData));
		results->m_fileData.emplace_back(std::move(data));

		auto insertPos = std::lower_bound(results->m_indexMapping.begin(), results->m_indexMapping.end(), old_count + added_count, SortPredicate(compare));
		int const added_index = insertPos - results->m_indexMapping.begin();
		results->m_indexMapping.insert(insertPos, old_count + added_count++);

		// Remember inserted index
		if (has_selections) {
			auto const added_indexes_insert_pos = std::lower_bound(added_indexes.begin(), added_indexes.end(), added_index);
			for (auto index = added_indexes_insert_pos; index != added_indexes.end(); ++index) {
				++(*index);
			}
			added_indexes.insert(added_indexes_insert_pos, added_index);
		}

		if (auto* sbar = results->GetFilelistStatusBar()) {
			if (entry.is_dir()) {
				sbar->AddDirectory();
			}
			else {
				sbar->AddFile(entry.size);
			}
		}
	}

	if (added_count) {
		results->SetItemCount(old_count + added_count);
		results->UpdateSelections_ItemsAdded(added_indexes);
		results->RefreshListOnly(false);
	}
}

void CSearchDialog::ProcessDirectoryListing(CLocalRecursiveOperation::listing const& listing)
{
	if (!searching_ || mode_ == search_mode::remote) {
		return;
	}

	int old_count = m_results->m_fileData.size();
	int added_count = 0;

	std::wstring const& path = listing.localPath.GetPath();

	bool const has_selections = m_results->GetSelectedItemCount() != 0;

	std::vector<int> added_indexes;
	if (has_selections) {
		added_indexes.reserve(listing.files.size() + listing.dirs.size());
	}

	auto & compare = m_results->GetSortComparisonObject();

	auto const& add_entry = [&](CLocalRecursiveOperation::listing::entry const& entry, bool dir) {
		if (!CFilterManager::FilenameFilteredByFilter(m_search_filter, entry.name, path, dir, entry.size, entry.attributes, entry.time)) {
			return;
		}

		CLocalSearchFileData localData;
		static_cast<CLocalRecursiveOperation::listing::entry&>(localData) = entry;
		localData.path = listing.localPath;

		CGenericFileData data;
		if (dir) {
			data.icon = m_results->m_dirIcon;
#ifndef FZ_WINDOWS
			if (localData.is_link()) {
				data.icon += 3;
			}
#endif
		}
		else {
			data.icon = -2;
		}

		m_results->localFileData_.emplace_back(std::move(localData));
		m_results->m_fileData.emplace_back(std::move(data));

		auto insertPos = std::lower_bound(m_results->m_indexMapping.begin(), m_results->m_indexMapping.end(), old_count + added_count, SortPredicate(compare));
		int const added_index = insertPos - m_results->m_indexMapping.begin();
		m_results->m_indexMapping.insert(insertPos, old_count + added_count++);

		// Remember inserted index
		if (has_selections) {
			auto const added_indexes_insert_pos = std::lower_bound(added_indexes.begin(), added_indexes.end(), added_index);
			for (auto index = added_indexes_insert_pos; index != added_indexes.end(); ++index) {
				++(*index);
			}
			added_indexes.insert(added_indexes_insert_pos, added_index);
		}

		if (auto* sbar = m_results->GetFilelistStatusBar()) {
			if (dir) {
				sbar->AddDirectory();
			}
			else {
				sbar->AddFile(entry.size);
			}
		}
	};

	for (auto const& file : listing.files) {
		add_entry(file, false);
	}
	for (auto const& dir : listing.dirs) {
		add_entry(dir, true);
	}

	if (added_count) {
		m_results->SetItemCount(old_count + added_count);
		m_results->UpdateSelections_ItemsAdded(added_indexes);
		m_results->RefreshListOnly(false);
	}
}

void CSearchDialog::OnSearch(wxCommandEvent&)
{
	if (searching_) {
		return;
	}

	// Prepare filter
	wxString error;
	if (!ValidateFilter(error, true)) {
		wxMessageBoxEx(wxString::Format(_("Invalid search conditions: %s"), error), _("File search"), wxICON_EXCLAMATION);
		return;
	}

	if (mode_ != search_mode::remote) {
		if (!m_state.IsLocalIdle()) {
			wxBell();
			return;
		}

		CLocalPath path;
		if (!path.SetPath(xrc_call(*this, "ID_PATH", &wxTextCtrl::GetValue).ToStdWstring()) || path.empty()) {
			wxMessageBoxEx(_("Need to enter valid local path"), _("Local file search"), wxICON_EXCLAMATION);
			return;
		}

		std::wstring error;
		if (!path.Exists(&error)) {
			wxMessageBoxEx(error, _("Local file search"), wxICON_EXCLAMATION);
			return;
		}

		m_local_search_root = std::move(path);
	}

	if (mode_ != search_mode::local) {
		if (!m_state.IsRemoteIdle()) {
			wxBell();
			return;
		}

		Site const& site = m_state.GetSite();
		if (!site) {
			wxMessageBoxEx(_("Connection to server lost."), _("Remote file search"), wxICON_EXCLAMATION);
			return;
		}

		CServerPath path;
		path.SetType(site.server.GetType());
		if (!path.SetPath(xrc_call(*this, (mode_ == search_mode::comparison) ? "ID_REMOTE_PATH" : "ID_PATH", &wxTextCtrl::GetValue).ToStdWstring()) || path.empty()) {
			wxMessageBoxEx(_("Need to enter valid remote path"), _("Remote file search"), wxICON_EXCLAMATION);
			return;
		}

		m_remote_search_root = std::move(path);
	}

	searching_ = true;

	bool const matchCase = xrc_call(*this, "ID_CASE", &wxCheckBox::GetValue);
	m_search_filter = GetFilter(matchCase);
	m_search_filter.matchCase = matchCase;
	m_search_filter.filterFiles = xrc_call(*this, "ID_FIND_FILES", &wxCheckBox::GetValue);
	m_search_filter.filterDirs = xrc_call(*this, "ID_FIND_DIRS", &wxCheckBox::GetValue);

	m_pComparisonManager->ExitComparisonMode();

	// Delete old results
	m_results->clear();
	if (mode_ == search_mode::remote) {
		m_results->set_mode(search_mode::remote);
	}
	else {
		m_results->set_mode(search_mode::local);
	}

	if (mode_ == search_mode::comparison) {
		if (m_results->m_sortColumn != 0 && m_results->m_sortColumn != 1) {
			m_results->SortList(0);
		}
		if (m_remoteResults->m_sortColumn != m_results->m_sortColumn || m_remoteResults->m_sortDirection != m_results->m_sortDirection) {
			m_remoteResults->SortList(m_results->m_sortColumn, m_results->m_sortDirection);
		}

		m_remoteResults->clear();
		m_remoteResults->set_mode(search_mode::remote);
	}

	m_visited.clear();

	// Start
	if (mode_ == search_mode::comparison) {
		m_pComparisonManager->SetListings(m_results, m_remoteResults);

		m_pComparisonManager->SetComparisonMode(xrc_call(*this, "ID_COMPARE_SIZE", &wxRadioButton::GetValue) ? 0 : 1);
		m_pComparisonManager->SetHideIdentical(xrc_call(*this, "ID_COMPARE_HIDEIDENTICAL", &wxCheckBox::GetValue) != 0);
	}

	if (mode_ != search_mode::remote) {
		local_recursion_root root;
		root.add_dir_to_visit(m_local_search_root);
		m_state.GetLocalRecursiveOperation()->AddRecursionRoot(std::move(root));
		ActiveFilters const filters; // Empty, recurse into everything
		m_state.GetLocalRecursiveOperation()->StartRecursiveOperation(recursive_operation::recursive_list, filters, true, false);
	}

	if (mode_ != search_mode::local) {
		recursion_root root(m_remote_search_root, true);
		root.add_dir_to_visit_restricted(m_remote_search_root, std::wstring(), true);
		m_state.GetRemoteRecursiveOperation()->AddRecursionRoot(std::move(root));
		ActiveFilters const filters; // Empty, recurse into everything
		m_state.GetRemoteRecursiveOperation()->StartRecursiveOperation(recursive_operation::recursive_list, filters);
	}

	SetCtrlState();
}

void CSearchDialog::Stop()
{
	if (!searching_) {
		return;
	}

	if (mode_ != search_mode::local) {
		if (!m_state.IsRemoteIdle()) {
			m_state.m_pCommandQueue->Cancel();
			m_state.GetRemoteRecursiveOperation()->StopRecursiveOperation();
		}
	}

	if (mode_ != search_mode::remote) {
		if (!m_state.IsLocalIdle()) {
			m_state.GetLocalRecursiveOperation()->StopRecursiveOperation();
		}
	}

	m_pComparisonManager->ExitComparisonMode();

	searching_ = false;
}

void CSearchDialog::SetCtrlState()
{
	bool can_start = !searching_;
	if (mode_ != search_mode::local && !m_state.IsRemoteIdle()) {
		can_start = false;
	}
	if (mode_ != search_mode::remote && !m_state.IsLocalIdle()) {
		can_start = false;
	}
	xrc_call(*this, "ID_START", &wxButton::Enable, can_start);
	xrc_call(*this, "ID_STOP", &wxButton::Enable, searching_);
	xrc_call(*this, "ID_LOCAL_SEARCH", &wxRadioButton::Enable, !searching_);
	xrc_call(*this, "ID_REMOTE_SEARCH", &wxRadioButton::Enable, !searching_ && m_state.IsRemoteConnected());
	xrc_call(*this, "ID_COMPARATIVE_SEARCH", &wxRadioButton::Enable, !searching_ && m_state.IsRemoteConnected());
	xrc_call(*this, "ID_PATH", &wxTextCtrl::Enable, !searching_);
	xrc_call(*this, "ID_REMOTE_PATH", &wxTextCtrl::Enable, !searching_);

	bool const comparison = mode_ == search_mode::comparison;
	xrc_call(*this, "ID_REMOTE_PATH_LABEL", &wxStaticText::Show, comparison);
	xrc_call(*this, "ID_REMOTE_PATH", &wxTextCtrl::Show, comparison);
	xrc_call(*this, "ID_RESULTS_LABEL", &wxStaticText::Show, !comparison);
	xrc_call(*this, "ID_LOCAL_RESULTS_LABEL", &wxStaticText::Show, comparison);
	xrc_call(*this, "ID_REMOTE_RESULTS_LABEL", &wxStaticText::Show, comparison);
	if (comparison) {
		xrc_call(*this, "ID_PATH_LABEL", &wxStaticText::SetLabel, _("Local &directory:"));
	}
	else {
		xrc_call(*this, "ID_PATH_LABEL", &wxStaticText::SetLabel, _("Search &directory:"));
	}
	xrc_call(*this, "ID_COMPARE_LABEL", &wxStaticText::Show, comparison);
	xrc_call(*this, "ID_COMPARE_SIZE", &wxRadioButton::Show, comparison);
	xrc_call(*this, "ID_COMPARE_DATE", &wxRadioButton::Show, comparison);
	xrc_call(*this, "ID_COMPARE_HIDEIDENTICAL", &wxCheckBox::Show, comparison);

	m_remoteResults->Show(comparison);
	m_remoteStatusBar->Show(comparison);
}

void CSearchDialog::OnContextMenu(wxContextMenuEvent& event)
{
	auto const evObj = event.GetEventObject();
	if (!((evObj == m_results || evObj == m_results->GetMainWindow()) ||
		(mode_ == search_mode::comparison && (evObj == m_remoteResults || evObj == m_remoteResults->GetMainWindow())))) {
		event.Skip();
		return;
	}

	CSearchDialogFileList* r = m_results;

	bool local_results{};
	if (mode_ == search_mode::comparison) {
		local_results = (evObj == m_results || evObj == m_results->GetMainWindow());
		if (!local_results) {
			r = m_remoteResults;
		}
	}
	else {
		local_results = mode_ == search_mode::local;
	}
	bool const connected = m_state.IsRemoteIdle() && m_state.IsRemoteConnected();

	bool hasSelection{};
	bool multipleSelection{};
	int item = r->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (item != -1) {
		hasSelection = true;
		if (r->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) != -1) {
			multipleSelection = true;
		}
	}

	wxMenu menu;
	if (local_results) {
		menu.Append(XRCID("ID_MENU_SEARCH_UPLOAD"), _("&Upload..."));
		menu.Append(XRCID("ID_MENU_SEARCH_LOCAL_OPEN"), _("&Set as local directory and close dialog"));
		menu.Append(XRCID("ID_MENU_SEARCH_FILEMANAGER"), _("Show in file &manager"));
		menu.Append(XRCID("ID_MENU_SEARCH_DELETE"), _("D&elete"));

		menu.Enable(XRCID("ID_MENU_SEARCH_UPLOAD"), connected && hasSelection);
		menu.Enable(XRCID("ID_MENU_SEARCH_LOCAL_OPEN"), hasSelection && !multipleSelection);
		menu.Enable(XRCID("ID_MENU_SEARCH_FILEMANAGER"), hasSelection);
		menu.Enable(XRCID("ID_MENU_SEARCH_DELETE"), hasSelection);
	}
	else {
		menu.Append(XRCID("ID_MENU_SEARCH_DOWNLOAD"), _("&Download..."));
		menu.Append(XRCID("ID_MENU_SEARCH_REMOTE_OPEN"), _("&Set as remote directory and close dialog"));
		menu.Append(XRCID("ID_MENU_SEARCH_EDIT"), _("&View/Edit"));
		menu.Append(XRCID("ID_MENU_SEARCH_DELETE_REMOTE"), _("D&elete"));

		if (wxGetKeyState(WXK_SHIFT)) {
			menu.Append(XRCID("ID_MENU_SEARCH_GETURL_PASSWORD"), _("C&opy URL(s) with password to clipboard"));
			menu.Enable(XRCID("ID_MENU_SEARCH_GETURL_PASSWORD"), hasSelection);
		}
		else {
			menu.Append(XRCID("ID_MENU_SEARCH_GETURL"), _("C&opy URL(s) to clipboard"));
			menu.Enable(XRCID("ID_MENU_SEARCH_GETURL"), hasSelection);
		}

		menu.Enable(XRCID("ID_MENU_SEARCH_DOWNLOAD"), connected && hasSelection);
		menu.Enable(XRCID("ID_MENU_SEARCH_REMOTE_OPEN"), connected && hasSelection && !multipleSelection);
		menu.Enable(XRCID("ID_MENU_SEARCH_DELETE_REMOTE"), connected && hasSelection);
		menu.Enable(XRCID("ID_MENU_SEARCH_EDIT"), connected && hasSelection);
	}

	PopupMenu(&menu);
}

bool CSearchDialog::IsIdle()
{
	bool idle = true;
	if (mode_ != search_mode::local) {
		idle &= m_state.IsRemoteIdle();
	}
	if (mode_ != search_mode::remote) {
		idle &= m_state.IsLocalIdle();
	}
	return idle;
}

class CSearchTransferDialog final : public wxDialogEx
{
public:
	bool Run(wxWindow* parent, bool download, const wxString& path, int count_files, int count_dirs)
	{
		download_ = download;

		if (!Create(parent, nullID, download ? _("Download search results") : _("Download search results"))) {
			return false;
		}

		auto & lay = layout();
		auto main = lay.createMain(this, 1);
		main->AddGrowableCol(0);

		std::wstring desc;
		if (!count_dirs) {
			desc = fz::sprintf(fztranslate("Selected %d file for transfer.", "Selected %d files for transfer.", count_files), count_files);
		}
		else if (!count_files) {
			desc = fz::sprintf(fztranslate("Selected %d directory with its contents for transfer.", "Selected %d directories with their contents for transfer.", count_dirs), count_dirs);
		}
		else {
			std::wstring files = fz::sprintf(fztranslate("%d file", "%d files", count_files), count_files);
			std::wstring dirs = fz::sprintf(fztranslate("%d directory with its contents", "%d directories with their contents", count_dirs), count_dirs);
			desc = fz::sprintf(fztranslate("Selected %s and %s for transfer."), files, dirs);
		}
		main->Add(new wxStaticText(this, nullID, desc));
		main->Add(new wxStaticText(this, nullID, download ? _("&Download to:") : _("&Upload to:")));

		auto row = lay.createFlex(2);
		row->AddGrowableCol(0);
		main->Add(row, lay.grow);
		path_ = new wxTextCtrlEx(this, nullID, path);
		row->Add(path_, lay.grow);

		auto browse = new wxButton(this, nullID, _("&Browse..."));
		row->Add(browse);
		browse->Bind(wxEVT_BUTTON, [&](wxCommandEvent&){ OnBrowse(); });

		main->Add(new wxStaticText(this, nullID, download ? _("Remote path handling:") : _("Local path handling:")));

		auto keep = new wxRadioButton(this, nullID, download ?_("&Keep remote path structure relative to search root") : _("&Keep local path structure relative to search root"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		keep->SetValue(true);
		main->Add(keep);
		flatten_ = new wxRadioButton(this, nullID, download ?_("&Flatten remote paths, download all contained files directly into target directory") : _("&Flatten local paths, upload all contained files directly into target directory"));
		main->Add(flatten_);

		main->Add(new wxStaticText(this, nullID, _("Queueing:")));

		auto start = new wxRadioButton(this, nullID, _("&Start transfer immediately"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		start->SetValue(true);
		main->Add(start);
		queue_ = new wxRadioButton(this, nullID, _("Add files to &queue only"));
		main->Add(queue_);

		auto [buttons, ok, cancel] = lay.createButtonSizerButtons(this, main, true);
		ok->Bind(wxEVT_BUTTON, [&](wxCommandEvent&){ OnOK(); });

		GetSizer()->Fit(this);

		if (ShowModal() != wxID_OK) {
			return false;
		}

		return true;
	}

	std::wstring GetPath() const { return path_ ? path_->GetValue().ToStdWstring() : std::wstring(); }
	bool Flatten() const { return flatten_ && flatten_->GetValue(); }
	bool Queue() const { return queue_ && queue_->GetValue(); }

protected:
	bool download_{};

	wxTextCtrlEx* path_{};
	wxRadioButton* flatten_{};
	wxRadioButton* queue_{};

	void OnBrowse();
	void OnOK();
};

void CSearchTransferDialog::OnBrowse()
{
	wxDirDialog dlg(this, _("Select target download directory"), path_->GetValue(), wxDD_NEW_DIR_BUTTON);
	if (dlg.ShowModal() == wxID_OK) {
		path_->ChangeValue(dlg.GetPath());
	}
}

void CSearchTransferDialog::OnOK()
{
	if (download_) {
		CLocalPath path(path_->GetValue().ToStdWstring());
		if (path.empty()) {
			wxMessageBoxEx(_("You have to enter a local directory."), _("Download search results"), wxICON_EXCLAMATION);
			return;
		}

		if (!path.IsWriteable()) {
			wxMessageBoxEx(_("You have to enter a writable local directory."), _("Download search results"), wxICON_EXCLAMATION);
			return;
		}
	}
	else {
		CServerPath path(path_->GetValue().ToStdWstring());
		if (path.empty()) {
			wxMessageBoxEx(_("You have to enter a remote directory."), _("Upload search results"), wxICON_EXCLAMATION);
			return;
		}
	}

	EndDialog(wxID_OK);
}

namespace {

bool isSubdir(CServerPath const& a, CServerPath const&b) {
	return a.IsSubdirOf(b, false);
}

bool isSubdir(CLocalPath const& a, CLocalPath const&b) {
	return a.IsSubdirOf(b);
}

template<typename Path, typename FileData>
void ProcessSelection(std::list<int> &selected_files, std::deque<Path> &selected_dirs, std::vector<FileData> const& fileData, CSearchDialogFileList const* results)
{
	std::deque<Path> dirs;

	int sel = -1;
	while ((sel = results->GetNextItem(sel, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		if (static_cast<size_t>(sel) >= results->indexMapping().size()) {
			continue;
		}
		int index = results->indexMapping()[sel];

		if (fileData[index].is_dir()) {
			Path path = fileData[index].path;
			path.ChangePath(fileData[index].name);
			if (path.empty()) {
				continue;
			}

			dirs.push_back(path);
		}
		else {
			selected_files.push_back(index);
		}
	}

	// Make sure that selected_dirs does not contain
	// any directories that are in a parent-child relationship
	// Resolve by only keeping topmost parents
	std::sort(dirs.begin(), dirs.end());
	for (Path const& path : dirs) {
		if (!selected_dirs.empty() && (isSubdir(path, selected_dirs.back()) || path == selected_dirs.back())) {
			continue;
		}
		selected_dirs.push_back(path);
	}

	// Now in a second phase filter out all files that are also in a directory
	std::list<int> selected_files_new;
	for (auto const& sel_file : selected_files) {
		Path const& path = fileData[sel_file].path;
		typename std::deque<Path>::iterator path_iter;
		for (path_iter = selected_dirs.begin(); path_iter != selected_dirs.end(); ++path_iter) {
			if (*path_iter == path || isSubdir(path, *path_iter)) {
				break;
			}
		}
		if (path_iter == selected_dirs.end()) {
			selected_files_new.push_back(sel_file);
		}
	}
	selected_files.swap(selected_files_new);

	// At this point selected_dirs contains uncomparable
	// paths and selected_files contains only files not
	// covered by any of those directories.
}
}

void CSearchDialog::OnDownload(wxCommandEvent&)
{
	if (!m_state.IsRemoteIdle()) {
		return;
	}

	CSearchDialogFileList *results = m_results;
	if (mode_ == search_mode::comparison) {
		results = m_remoteResults;
	}

	// Find all selected files and directories
	std::deque<CServerPath> selected_dirs;
	std::list<int> selected_files;
	ProcessSelection(selected_files, selected_dirs, results->remoteFileData_, results);

	if (selected_files.empty() && selected_dirs.empty()) {
		return;
	}

	CSearchTransferDialog dlg;
	if (!dlg.Run(this, true, m_state.GetLocalDir().GetPath(), selected_files.size(), selected_dirs.size())) {
		return;
	}

	CLocalPath path(dlg.GetPath());
	if (path.empty() || !path.IsWriteable()) {
		wxBell();
		return;
	}

	Site const& site = m_state.GetSite();
	if (!site) {
		wxBell();
		return;
	}

	for (auto const& sel : selected_files) {
		CRemoteSearchFileData const& entry = results->remoteFileData_[sel];

		CLocalPath target_path = path;
		if (!dlg.Flatten()) {
			// Append relative path to search root to local target path
			CServerPath remote_path = entry.path;
			std::list<std::wstring> segments;
			while (m_remote_search_root.IsParentOf(remote_path, false) && remote_path.HasParent()) {
				segments.push_front(remote_path.GetLastSegment());
				remote_path = remote_path.GetParent();
			}
			for (auto const& segment : segments) {
				target_path.AddSegment(segment);
			}
		}

		CServerPath remote_path = entry.path;
		std::wstring localName = CQueueView::ReplaceInvalidCharacters(options_, entry.name);
		if (!entry.is_dir() && remote_path.GetType() == VMS && options_.get_int(OPTION_STRIP_VMS_REVISION))
			localName = StripVMSRevision(localName);

		m_pQueue->QueueFile(dlg.Queue(), true,
			entry.name, (localName != entry.name) ? localName : std::wstring(),
			target_path, remote_path, site, entry.size);
	}
	m_pQueue->QueueFile_Finish(!dlg.Queue());

	auto const mode = dlg.Flatten() ? recursive_operation::recursive_transfer_flatten : recursive_operation::recursive_transfer;

	for (auto const& dir : selected_dirs) {
		CLocalPath target_path = path;
		if (!dlg.Flatten() && dir.HasParent()) {
			target_path.AddSegment(dir.GetLastSegment());
		}

		recursion_root root(dir, true);
		root.add_dir_to_visit(dir, std::wstring(), target_path, false);
		m_state.GetRemoteRecursiveOperation()->AddRecursionRoot(std::move(root));
	}
	ActiveFilters const filters; // Empty, recurse into everything
	m_state.GetRemoteRecursiveOperation()->StartRecursiveOperation(mode, filters, !dlg.Queue());
}

void CSearchDialog::OnUpload(wxCommandEvent&)
{
	if (!m_state.IsLocalIdle()) {
		return;
	}

	// Find all selected files and directories
	std::deque<CLocalPath> selected_dirs;
	std::list<int> selected_files;
	ProcessSelection(selected_files, selected_dirs, m_results->localFileData_, m_results);

	if (selected_files.empty() && selected_dirs.empty()) {
		return;
	}

	CSearchTransferDialog dlg;
	if (!dlg.Run(this, false, m_state.GetRemotePath().GetPath(), selected_files.size(), selected_dirs.size())) {
		return;
	}

	CServerPath path(dlg.GetPath());
	if (path.empty()) {
		wxBell();
		return;
	}

	Site const& site = m_state.GetSite();
	if (!site) {
		wxBell();
		return;
	}

	for (auto const& sel : selected_files) {
		CLocalSearchFileData const& entry = m_results->localFileData_[sel];

		CServerPath target_path = path;
		if (!dlg.Flatten()) {
			// Append relative path to search root to local target path
			CLocalPath local_path = entry.path;
			std::list<std::wstring> segments;
			while (m_local_search_root.IsParentOf(local_path) && local_path.HasParent()) {
				segments.push_front(local_path.GetLastSegment());
				local_path = local_path.GetParent();
			}
			for (auto const& segment : segments) {
				target_path.AddSegment(segment);
			}
		}

		CLocalPath local_path = entry.path;
		std::wstring localName = CQueueView::ReplaceInvalidCharacters(options_, entry.name);

		m_pQueue->QueueFile(dlg.Queue(), false,
			entry.name, (localName != entry.name) ? localName : std::wstring(),
			local_path, target_path, site, entry.size);
	}
	m_pQueue->QueueFile_Finish(!dlg.Queue());

	auto const mode = dlg.Flatten() ? recursive_operation::recursive_transfer_flatten : recursive_operation::recursive_transfer;

	for (auto const& dir : selected_dirs) {
		CServerPath target_path = path;
		if (!dlg.Flatten() && dir.HasParent()) {
			target_path.AddSegment(dir.GetLastSegment());
		}

		local_recursion_root root;
		root.add_dir_to_visit(dir, target_path);
		m_state.GetLocalRecursiveOperation()->AddRecursionRoot(std::move(root));
	}
	ActiveFilters const filters; // Empty, recurse into everything
	m_state.GetLocalRecursiveOperation()->StartRecursiveOperation(mode, filters, !dlg.Queue());
}

void CSearchDialog::OnEdit(wxCommandEvent&)
{
	if (!m_state.IsRemoteIdle()) {
		return;
	}

	CSearchDialogFileList *results = m_results;
	if (mode_ == search_mode::comparison) {
		results = m_remoteResults;
	}

	if (results->mode_ != search_mode::remote) {
		return;
	}

	// Find all selected files and directories
	std::deque<CServerPath> selected_dirs;
	std::list<int> selected_files;
	ProcessSelection(selected_files, selected_dirs, results->remoteFileData_, results);

	if (selected_files.empty() && selected_dirs.empty()) {
		return;
	}

	if (!selected_dirs.empty()) {
		wxMessageBoxEx(_("Editing directories is not supported"), _("Editing search results"), wxICON_EXCLAMATION);
		return;
	}

	if (!edit_handler_) {
		wxBell();
		return;
	}

	const wxString& localDir = edit_handler_->GetLocalDirectory();
	if (localDir.empty()) {
		wxMessageBoxEx(_("Could not get temporary directory to download file into."), _("Cannot edit file"), wxICON_STOP);
		return;
	}

	Site const& site = m_state.GetSite();
	if (!site) {
		wxBell();
		return;
	}

	if (selected_files.size() > 10) {
		CConditionalDialog dlg(this, CConditionalDialog::many_selected_for_edit, CConditionalDialog::yesno, options_);
		dlg.SetTitle(_("Confirmation needed"));
		dlg.AddText(_("You have selected more than 10 files for editing, do you really want to continue?"));

		if (!dlg.Run()) {
			return;
		}
	}

	for (auto const& item : selected_files) {
		const CDirentry& entry = results->remoteFileData_[item];
		const CServerPath path = results->remoteFileData_[item].path;

		edit_handler_->Edit(CEditHandler::remote, entry.name, path, site, entry.size, this);
	}
}

void CSearchDialog::OnDeleteRemote(wxCommandEvent&)
{
	if (!m_state.IsRemoteIdle()) {
		return;
	}

	CSearchDialogFileList *results = m_results;
	if (mode_ == search_mode::comparison) {
		results = m_remoteResults;
	}

	// Find all selected files and directories
	std::deque<CServerPath> selected_dirs;
	std::list<int> selected_files;
	ProcessSelection(selected_files, selected_dirs, results->remoteFileData_, results);

	if (selected_files.empty() && selected_dirs.empty()) {
		return;
	}

	std::wstring question;
	if (selected_dirs.empty()) {
		question = fz::sprintf(fztranslate("Really delete %d file from the server?", "Really delete %d files from the server?", selected_files.size()), selected_files.size());
	}
	else if (selected_files.empty()) {
		question = fz::sprintf(fztranslate("Really delete %d directory with its contents from the server?", "Really delete %d directories with their contents from the server?", selected_dirs.size()), selected_dirs.size());
	}
	else {
		std::wstring files = fz::sprintf(fztranslate("%d file", "%d files", selected_files.size()), selected_files.size());
		std::wstring dirs = fz::sprintf(fztranslate("%d directory with its contents", "%d directories with their contents", selected_dirs.size()), selected_dirs.size());
		question = fz::sprintf(fztranslate("Really delete %s and %s from the server?"), files, dirs);
	}

	if (wxMessageBoxEx(question, _("Confirm deletion"), wxICON_QUESTION | wxYES_NO) != wxYES) {
		return;
	}

	for (auto const& file : selected_files) {
		CRemoteSearchFileData const& entry = results->remoteFileData_[file];
		// Possible improvement: Group by path
		std::vector<std::wstring> files_to_delete;
		files_to_delete.push_back(entry.name);
		m_state.m_pCommandQueue->ProcessCommand(new CDeleteCommand(entry.path, std::move(files_to_delete)));
	}

	bool deleting_current{};
	for (auto path : selected_dirs) {
		if (path.IsParentOf(m_state.GetRemotePath(), false, true)) {
			deleting_current = true;
		}

		std::wstring segment;
		if (path.HasParent()) {
			segment = path.GetLastSegment();
			path = path.GetParent();
		}
		recursion_root root(path, !path.HasParent());
		root.add_dir_to_visit(path, segment);
		m_state.GetRemoteRecursiveOperation()->AddRecursionRoot(std::move(root));
	}

	if (deleting_current) {
		m_state.ChangeRemoteDir(m_remote_search_root);
	}

	ActiveFilters const filters; // Empty, recurse into everything
	m_state.GetRemoteRecursiveOperation()->StartRecursiveOperation(recursive_operation::recursive_delete, filters);
}

void CSearchDialog::OnDeleteLocal(wxCommandEvent&)
{
	std::deque<CLocalPath> selected_dirs;
	std::list<int> selected_files;
	ProcessSelection(selected_files, selected_dirs, m_results->localFileData_, m_results);

	if (selected_files.empty() && selected_dirs.empty()) {
		return;
	}

	std::list<fz::native_string> pathsToDelete;
	for (auto const& file : selected_files) {
		CLocalSearchFileData const& entry = m_results->localFileData_[file];
		pathsToDelete.push_back(fz::to_native(entry.path.GetPath() + entry.name));
	}
	for (auto path : selected_dirs) {
		pathsToDelete.push_back(fz::to_native(path.GetPath()));
	}

	gui_recursive_remove rmd(this);
	rmd.remove(pathsToDelete);
}

void CSearchDialog::OnCharHook(wxKeyEvent& event)
{
	if (IsEscapeKey(event)) {
		EndDialog(wxID_CANCEL);
		return;
	}

	event.Skip();
}

#ifdef __WXMSW__
int CSearchDialogFileList::GetOverlayIndex(int item)
{
	if (item < 0 || item >= (int)m_indexMapping.size()) {
		return 0;
	}
	int index = m_indexMapping[item];

	if (mode_ == CSearchDialog::search_mode::local) {
		auto const& file = localFileData_[index];
		return file.is_link() ? GetLinkOverlayIndex() : 0;
	}
	else {
		auto const& file = remoteFileData_[index];
		return file.is_link() ? GetLinkOverlayIndex() : 0;
	}
}
#endif

void CSearchDialog::LoadConditions()
{
	CInterProcessMutex mutex(MUTEX_SEARCHCONDITIONS);

	CXmlFile file(wxGetApp().GetSettingsFile(L"search"));
	auto document = file.Load(true);
	if (!document) {
		wxMessageBoxEx(file.GetError(), _("Error loading xml file"), wxICON_ERROR);
		return;
	}

	auto filter = document.child("Filter");
	if (!filter) {
		return;
	}

	if (!load_filter(filter, m_search_filter)) {
		m_search_filter = CFilter();
	}

	auto comparative = document.child("Comparative");
	if (!comparative) {
		return;
	}

	if (GetTextElement(comparative, "CompareSizes") == L"1") {
		xrc_call(*this, "ID_COMPARE_SIZE", &wxRadioButton::SetValue, true);
	}
	else {
		xrc_call(*this, "ID_COMPARE_DATE", &wxRadioButton::SetValue, true);
	}
	xrc_call(*this, "ID_COMPARE_HIDEIDENTICAL", &wxCheckBox::SetValue, GetTextElement(comparative, "HideIdentical") == L"1");
}

void CSearchDialog::SaveConditions()
{
	CInterProcessMutex mutex(MUTEX_SEARCHCONDITIONS);

	CXmlFile file(wxGetApp().GetSettingsFile(L"search"));
	auto document = file.Load(true);
	if (!document) {
		wxMessageBoxEx(file.GetError(), _("Error loading xml file"), wxICON_ERROR);
		return;
	}

	pugi::xml_node filter;
	while ((filter = document.child("Filter"))) {
		document.remove_child(filter);
	}
	filter = document.append_child("Filter");

	save_filter(filter, m_search_filter);

	pugi::xml_node comparative;
	while ((comparative = document.child("Comparative"))) {
		document.remove_child(comparative);
	}
	comparative = document.append_child("Comparative");

	AddTextElement(comparative, "CompareSizes", xrc_call(*this, "ID_COMPARE_SIZE", &wxRadioButton::GetValue) ? "1" : "0");
	AddTextElement(comparative, "HideIdentical", xrc_call(*this, "ID_COMPARE_HIDEIDENTICAL", &wxCheckBox::GetValue) ? "1" : "0");

	SaveWithErrorDialog(file);
}

void CSearchDialog::OnChangeSearchMode(wxCommandEvent&)
{
	wxString const strPath = xrc_call(*this, "ID_PATH", &wxTextCtrl::GetValue);

	CLocalPath const localPath = m_state.GetLocalDir();
	CServerPath const remotePath = m_state.GetRemotePath();

	search_mode new_mode = search_mode::local;
	if (xrc_call(*this, "ID_REMOTE_SEARCH", &wxRadioButton::GetValue)) {
		new_mode = search_mode::remote;
	}
	else if (xrc_call(*this, "ID_COMPARATIVE_SEARCH", &wxRadioButton::GetValue)) {
		new_mode = search_mode::comparison;
	}

	if (new_mode == search_mode::local) {
		if (strPath == remotePath.GetPath() && !localPath.empty()) {
			xrc_call(*this, "ID_PATH", &wxTextCtrl::ChangeValue, localPath.GetPath());
		}
		xrc_call(*this, "ID_REMOTE_PATH", &wxTextCtrl::Clear);
	}
	else if (new_mode == search_mode::remote) {
		if (strPath == localPath.GetPath() && !remotePath.empty()) {
			xrc_call(*this, "ID_PATH", &wxTextCtrl::ChangeValue, remotePath.GetPath());
		}
		xrc_call(*this, "ID_REMOTE_PATH", &wxTextCtrl::Clear);
	}
	else {
		// comparative search
		xrc_call(*this, "ID_PATH", &wxTextCtrl::ChangeValue, localPath.GetPath());
		xrc_call(*this, "ID_REMOTE_PATH", &wxTextCtrl::ChangeValue, remotePath.GetPath());
	}

	bool comparison_toggle = (new_mode == search_mode::comparison) != (mode_ == search_mode::comparison);
	mode_ = new_mode;

	SetCtrlState();

	if (comparison_toggle) {
		wxSize s = m_otherSize;
		m_otherSize = GetClientSize();

		SetMinClientSize(GetSizer()->GetMinSize());
		GetSizer()->Fit(this);
		Layout();

		if (s.IsFullySpecified()) {
			SetClientSize(s);
		}
	}
}

void CSearchDialog::OnChangeCompareOption(wxCommandEvent&)
{
	if (mode_ != search_mode::comparison) {
		return;
	}

	if (!m_pComparisonManager->IsComparing()) {
		return;
	}

	m_pComparisonManager->ExitComparisonMode();

	m_pComparisonManager->SetComparisonMode(xrc_call(*this, "ID_COMPARE_SIZE", &wxRadioButton::GetValue) ? 0 : 1);
	m_pComparisonManager->SetHideIdentical(xrc_call(*this, "ID_COMPARE_HIDEIDENTICAL", &wxCheckBox::GetValue) != 0);

	m_remoteResults->m_canStartComparison = true;
	m_results->m_canStartComparison = true;

	m_pComparisonManager->CompareListings();
}

void CSearchDialog::OnGetUrl(wxCommandEvent& event)
{
	if (m_results->mode_ != search_mode::remote) {
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

	std::wstring const url = site.Format((event.GetId() == XRCID("ID_MENU_SEARCH_GETURL_PASSWORD")) ? ServerFormat::url_with_password : ServerFormat::url);

	auto getUrl = [](std::wstring const& serverPart, CServerPath const& path, std::wstring const& name) {
		std::wstring url = serverPart;

		auto const pathPart = fz::percent_encode_w(path.FormatFilename(name, false), true);
		if (!pathPart.empty() && pathPart[0] != '/') {
			url += '/';
		}
		url += pathPart;

		return url;
	};

	std::wstring urls;

	int sel = -1;
	while ((sel = m_results->GetNextItem(sel, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) >= 0) {
		if (static_cast<size_t>(sel) >= m_results->m_indexMapping.size()) {
			continue;
		}
		int index = m_results->m_indexMapping[sel];

		auto const& entry = m_results->remoteFileData_[index];
		urls += getUrl(url, entry.path, entry.name);
#ifdef __WXMSW__
		urls += L"\r\n";
#else
		urls += L"\n";
#endif
	}

	wxTheClipboard->SetData(new wxURLDataObject(urls));

	wxTheClipboard->Flush();
	wxTheClipboard->Close();
}

void CSearchDialog::OnLocalOpen(wxCommandEvent&)
{
	int sel = m_results->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel == -1 || m_results->GetNextItem(sel, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) != -1) {
		wxBell();
		return;
	}
	if (static_cast<size_t>(sel) >= m_results->indexMapping().size()) {
		wxBell();
		return;
	}
	int index = m_results->indexMapping()[sel];
	auto & data = m_results->localFileData_[index];

	CLocalPath path = data.path;
	if (data.is_dir()) {
		path.ChangePath(data.name);
		if (path.empty()) {
			return;
		}
	}

	m_state.SetLocalDir(path);

	EndDialog(wxID_OK);
}

void CSearchDialog::OnRemoteOpen(wxCommandEvent&)
{
	CSearchDialogFileList *results = m_results;
	if (mode_ == search_mode::comparison) {
		results = m_remoteResults;
	}

	int sel = results->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel == -1 || results->GetNextItem(sel, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) != -1) {
		wxBell();
		return;
	}
	if (static_cast<size_t>(sel) >= results->indexMapping().size()) {
		wxBell();
		return;
	}
	int index = results->indexMapping()[sel];
	auto & data = results->remoteFileData_[index];

	CServerPath path = data.path;
	if (data.is_dir()) {
		path.ChangePath(data.name);
		if (path.empty()) {
			return;
		}
	}

	m_state.ChangeRemoteDir(path);

	EndDialog(wxID_OK);
}

void CSearchDialog::OnShowFileManager(wxCommandEvent& event)
{
	std::list<CLocalSearchFileData> selected_item_list;

	int sel = -1;
	while ((sel = m_results->GetNextItem(sel, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		if (static_cast<size_t>(sel) >= m_results->indexMapping().size()) {
			continue;
		}
		int index = m_results->indexMapping()[sel];

		selected_item_list.push_back(m_results->localFileData_[index]);
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

	bool const open = event.GetId() == XRCID("ID_MENU_SEARCH_OPEN");

	for (auto const& data : selected_item_list) {

		std::wstring const fn = data.path.GetPath() + data.name;
		if (data.is_dir()) {
			OpenInFileManager(fn);
			continue;
		}

		if (!open) {
			OpenInFileManager(data.path.GetPath());
			continue;
		}

		if (wxLaunchDefaultApplication(fn, 0)) {
			continue;
		}
		auto cmd_with_args = GetSystemAssociation(fn);
		if (cmd_with_args.empty()) {
			cmd_with_args = edit_handler_->GetAssociation(fn);
		}
		if (cmd_with_args.empty()) {
			wxMessageBoxEx(wxString::Format(_("The file '%s' could not be opened:\nNo program has been associated on your system with this file type."), fn), _("Opening failed"), wxICON_EXCLAMATION);
			continue;
		}
		if (!ProgramExists(cmd_with_args.front())) {
			wxString msg = wxString::Format(_("The file '%s' cannot be opened:\nThe associated program (%s) could not be found.\nPlease check your filetype associations."), fn, cmd_with_args.front());
			wxMessageBoxEx(msg, _("Cannot edit file"), wxICON_EXCLAMATION);
			continue;
		}
		if (fz::spawn_detached_process(AssociationToCommand(cmd_with_args, fn))) {
			continue;
		}
		wxMessageBoxEx(wxString::Format(_("The file '%s' could not be opened:\nThe associated command failed"), fn), _("Opening failed"), wxICON_EXCLAMATION);
	}
}
