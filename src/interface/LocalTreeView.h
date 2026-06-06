#ifndef FILEZILLA_INTERFACE_LOCALTREEVIEW_HEADER
#define FILEZILLA_INTERFACE_LOCALTREEVIEW_HEADER

#include "option_change_event_handler.h"
#include "systemimagelist.h"
#include "state.h"
#include "treectrlex.h"


class CQueueView;
class CWindowTinter;

#ifdef __WXMSW__
class CVolumeDescriptionEnumeratorThread;
#endif

class CLocalTreeView final : public wxTreeCtrlEx, CSystemImageList, CStateEventHandler, public COptionChangeEventHandler
{
	friend class CLocalTreeViewDropTarget;

public:
	CLocalTreeView(wxWindow* parent, wxWindowID id, CState& state, CQueueView *pQueueView, COptionsBase & options, login_manager & lim);
	virtual ~CLocalTreeView();

#ifdef __WXMSW__
	// React to changed drive letters
	void OnDevicechange(WPARAM wParam, LPARAM lParam);
#endif

protected:
	virtual void OnStateChange(t_statechange_notifications notification, std::wstring const& data, const void* data2) override;

	void SetDir(wxString const& localDir);
	void RefreshListing();

#ifdef __WXMSW__
	bool CreateRoot();
	bool DisplayDrives(wxTreeItemId parent);
	wxString GetSpecialFolder(int folder, int &iconIndex, int &openIconIndex);

	wxTreeItemId m_desktop;
	wxTreeItemId m_drives;
	wxTreeItemId m_documents;
#endif

	void UpdateSortMode();

	virtual void OnOptionsChanged(watched_options const& options);

	wxTreeItemId GetNearestParent(wxString& localDir);
	wxTreeItemId GetSubdir(wxTreeItemId parent, const wxString& subDir);
	void DisplayDir(wxTreeItemId parent, std::wstring const& dirname, std::wstring const& knownSubdir = std::wstring());
	std::wstring HasSubdir(std::wstring const& dirname);
	wxTreeItemId MakeSubdirs(wxTreeItemId parent, std::wstring dirname, wxString subDir);
	wxString m_currentDir;

	bool CheckSubdirStatus(wxTreeItemId& item, std::wstring const& path);

	CLocalPath MenuMkdir();

	DECLARE_EVENT_TABLE()
	void OnItemExpanding(wxTreeEvent& event);
#ifdef __WXMSW__
	void OnSelectionChanging(wxTreeEvent& event);
#endif
	void OnSelectionChanged(wxTreeEvent& event);
	void OnBeginDrag(wxTreeEvent& event);
#ifdef __WXMSW__
	void OnVolumesEnumerated(wxCommandEvent& event);
	CVolumeDescriptionEnumeratorThread* m_pVolumeEnumeratorThread;
#endif
	void OnContextMenu(wxTreeEvent& event);
	void OnMenuUpload(wxCommandEvent& event);
	void OnMenuMkdir(wxCommandEvent& event);
	void OnMenuMkdirChgDir(wxCommandEvent& event);
	void OnMenuRename(wxCommandEvent& event);
	void OnMenuDelete(wxCommandEvent& event);
	void OnBeginLabelEdit(wxTreeEvent& event);
	void OnEndLabelEdit(wxTreeEvent& event);
	void OnChar(wxKeyEvent& event);
	void OnMenuOpen(wxCommandEvent&);

#ifdef __WXMSW__
	// React to changed drive letters
	wxTreeItemId AddDrive(wxChar letter);
	void RemoveDrive(wxChar letter);
#endif

	std::wstring GetDirFromItem(wxTreeItemId item);

	CQueueView* m_pQueueView;

	wxTreeItemId m_contextMenuItem;

	std::unique_ptr<CWindowTinter> m_windowTinter;

	COptionsBase & options_;

	bool changing_dir_{};
};

#endif
