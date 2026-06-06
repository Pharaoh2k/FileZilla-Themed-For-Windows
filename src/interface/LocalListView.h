#ifndef FILEZILLA_INTERFACE_LOCALLISTVIEW_HEADER
#define FILEZILLA_INTERFACE_LOCALLISTVIEW_HEADER

#include "filelistctrl.h"
#include "state.h"

class CEditHandler;
class CInfoText;
class CQueueView;
class CLocalListViewDropTarget;
#ifdef __WXMSW__
class CVolumeDescriptionEnumeratorThread;
#endif
class CView;
class CWindowTinter;
class TimeFormatter;

class CLocalFileData final : public CGenericFileData
{
public:
	std::wstring name;
#ifdef __WXMSW__
	fz::sparse_optional<std::wstring> label;
#endif
	fz::datetime time;
	int64_t size;
	int attributes;
	enum : uint8_t {
		flag_dir = 0x1u,
		flag_link = 0x2u
	};
	uint8_t flags;
	bool is_dir() const { return flags & flag_dir; }
	bool is_link() const { return flags & flag_link; }
};

class CLocalListView final : public CFileListCtrl<CLocalFileData>, CStateEventHandler
{
	friend class CLocalListViewDropTarget;
	friend class CLocalListViewSortType;

public:
	CLocalListView(CView* parent, CState& state, CQueueView *pQueue, COptionsBase & options, TimeFormatter & time_formatter, login_manager& lim, CEditHandler* edit_handler);
	virtual ~CLocalListView();

protected:
	void OnStateChange(t_statechange_notifications notification, std::wstring const& data, const void*) override;
	bool DisplayDir(CLocalPath const& dirname);
	void ApplyCurrentFilter();

	// Declared const due to design error in wxWidgets.
	// Won't be fixed since a fix would break backwards compatibility
	// Both functions use a const_cast<CLocalListView *>(this) and modify
	// the instance.
	virtual int OnGetItemImage(long item) const;
	virtual wxListItemAttr* OnGetItemAttr(long item) const;

	// Clears all selections and returns the list of items that were selected
	std::vector<std::wstring> RememberSelectedItems(std::wstring & focused, int & focusedItem);

	// Select a list of items based in their names.
	// Sort order may not change between call to RememberSelectedItems and
	// ReselectItems
	void ReselectItems(std::vector<std::wstring> const& selectedNames, std::wstring focused, int focusedItem, bool ensureVisible = false);

#ifdef __WXMSW__
	void DisplayDrives();
	void DisplayShares(wxString computer);

	virtual int GetOverlayIndex(int item);
#endif

public:
	virtual bool CanStartComparison();
	virtual void StartComparison();
	virtual bool get_next_file(std::wstring_view & name, std::wstring & path, bool &dir, int64_t &size, fz::datetime& date) override;
	virtual void FinishComparison();

	virtual bool ItemIsDir(int index) const;
	virtual int64_t ItemGetSize(int index) const;

protected:
	virtual wxString GetItemText(int item, unsigned int column);

	bool IsItemValid(unsigned int item) const;
	CLocalFileData *GetData(unsigned int item);

	void UpdateSortComparisonObject() override;

	void RefreshFile(std::wstring const& file);

	virtual void OnNavigationEvent(bool forward);

	virtual bool OnBeginRename(const wxListEvent& event);
	virtual bool OnAcceptRename(const wxListEvent& event);

	CLocalPath m_dir;

	int m_dropTarget{-1};

	CLocalPath MenuMkdir();

	std::unique_ptr<CWindowTinter> m_windowTinter;

	CView *m_parentView{};
	CEditHandler* edit_handler_{};

	CInfoText* m_pInfoText{};
	void SetInfoText(wxString const& text);

	// Event handlers
	DECLARE_EVENT_TABLE()
	void OnItemActivated(wxListEvent& event);
	void OnContextMenu(wxContextMenuEvent& event);
	void OnMenuUpload(wxCommandEvent& event);
	void OnMenuMkdir(wxCommandEvent& event);
	void OnMenuMkdirChgDir(wxCommandEvent&);
	void OnMenuDelete(wxCommandEvent& event);
	void OnMenuRename(wxCommandEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnSize(wxSizeEvent& event);
	void OnBeginDrag(wxListEvent& event);
	void OnMenuOpen(wxCommandEvent& event);
	void OnMenuEdit(wxCommandEvent& event);
	void OnMenuEnter(wxCommandEvent& event);
	void OnMenuRefresh(wxCommandEvent& event);

#ifdef __WXMSW__
	void OnVolumesEnumerated(wxCommandEvent& event);
	std::unique_ptr<CVolumeDescriptionEnumeratorThread> volumeEnumeratorThread_;
#endif
};

#endif
