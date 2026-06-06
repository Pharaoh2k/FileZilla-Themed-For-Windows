#ifndef FILEZILLA_INTERFACE_SITEMANAGER_DIALOG_HEADER
#define FILEZILLA_INTERFACE_SITEMANAGER_DIALOG_HEADER

#include "dialogex.h"
#include "sitemanager.h"

class CInterProcessMutex;
class CWindowStateManager;
class CSiteManagerDropTarget;
class CSiteManagerSite;
class COptionsBase;
class wxTreeCtrlEx;
class login_manager;

class CSiteManagerDialog final : public wxDialogEx
{
	friend class CSiteManagerDropTarget;

	DECLARE_EVENT_TABLE()

public:
	struct _connected_site
	{
		Site site;
		std::wstring old_path;
	};

	/// Constructors
	CSiteManagerDialog(COptionsBase & options, login_manager & lim);
	virtual ~CSiteManagerDialog();

	// Creation. If pServer is set, it will cause a new item to be created.
	bool Create(wxWindow* parent, std::vector<_connected_site> *connected_sites, Site const* site = 0);

	bool GetServer(Site& data, Bookmark& bookmark);

protected:

	bool Verify();
	bool UpdateItem();
	bool UpdateBookmark(Bookmark &bookmark, Site const& site);
	bool Load();
	bool Save(pugi::xml_node element = pugi::xml_node(), wxTreeItemId treeId = wxTreeItemId());
	bool SaveChild(pugi::xml_node element, wxTreeItemId child);
	bool UpdateServer(Site & site, wxString const& name);
	void SetCtrlState();
	bool LoadDefaultSites();

	bool IsPredefinedItem(wxTreeItemId item);

	wxString FindFirstFreeName(const wxTreeItemId &parent, const wxString& name);

	void AddNewSite(wxTreeItemId parent, Site const& site, bool connected = false);
	void CopyAddServer(Site const& site);

	void AddNewBookmark(wxTreeItemId parent);

	void RememberLastSelected();

	std::wstring GetSitePath(wxTreeItemId item, bool stripBookmark = true);

	void MarkConnectedSites();
	void MarkConnectedSite(int connected_site);

	void OnOK(wxCommandEvent&);
	void OnCancel(wxCommandEvent&);
	void OnConnect(wxCommandEvent& event);
	void OnNewSite(wxCommandEvent& event);
	void OnNewFolder(wxCommandEvent& event);
	void OnRename(wxCommandEvent& event);
	void OnDelete(wxCommandEvent& event);
	void OnBeginLabelEdit(wxTreeEvent& event);
	void OnEndLabelEdit(wxTreeEvent& event);
	void OnSelChanging(wxTreeEvent& event);
	void OnSelChanged(wxTreeEvent& event);
	void OnItemActivated(wxTreeEvent& event);
	void OnBeginDrag(wxTreeEvent& event);
	void OnChar(wxKeyEvent& event);
	void OnCopySite(wxCommandEvent& event);
	void OnContextMenu(wxTreeEvent& event);
	void OnExportSelected(wxCommandEvent&);
	void OnNewBookmark(wxCommandEvent&);
	void OnBookmarkBrowse(wxCommandEvent&);
	void OnSearch(wxCommandEvent&);

	COptionsBase & options_;
	login_manager & login_manager_;

	CInterProcessMutex* m_pSiteManagerMutex{};

	wxTreeItemId m_predefinedSites;
	wxTreeItemId m_ownSites;

	std::vector<wxTreeItemId> draggedItems_;

	wxTreeItemId MoveItems(wxTreeItemId source, wxTreeItemId target, bool copy, bool use_existing_name);

	wxTreeCtrlEx* tree_{};
protected:
	CWindowStateManager* m_pWindowStateManager{};

	CSiteManagerSite *m_pNotebook_Site{};
	wxNotebook *m_pNotebook_Bookmark{};

	std::vector<_connected_site> *m_connected_sites{};

	bool m_is_deleting{};
	bool lastEditVetoed_{};
};

#endif
