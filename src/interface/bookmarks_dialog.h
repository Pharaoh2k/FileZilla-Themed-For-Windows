#ifndef FILEZILLA_INTERFACE_BOOKMARKS_DIALOG_HEADER
#define FILEZILLA_INTERFACE_BOOKMARKS_DIALOG_HEADER

#include "dialogex.h"

class COptionsBase;
class Site;
class login_manager;
class CNewBookmarkDialog final : public wxDialogEx
{
public:
	CNewBookmarkDialog(wxWindow* parent, COptionsBase & options, login_manager & lim, std::wstring& site_path, Site const* site);
	virtual ~CNewBookmarkDialog();

	int Run(wxString const& local_path, CServerPath const& remote_path);

protected:
	wxWindow* m_parent;
	COptionsBase & options_;
	login_manager & login_manager_;
	std::wstring &m_site_path;
	Site const* site_;

	void OnOK();
	void OnBrowse();

	struct impl;
	std::unique_ptr<impl> impl_;
};

class wxTreeCtrlEx;
class CBookmarksDialog final : public wxDialogEx
{
public:
	CBookmarksDialog(wxWindow* parent, COptionsBase & options, login_manager & lim, std::wstring& site_path, Site const* site);

	int Run();

	static bool GetGlobalBookmarks(std::vector<std::wstring> &bookmarks);
	static bool GetBookmark(const wxString& name, wxString &local_dir, CServerPath &remote_dir, bool &sync, bool &comparison);
	static bool AddBookmark(const wxString& name, const wxString &local_dir, const CServerPath &remote_dir, bool sync, bool comparison);

protected:
	bool Verify();
	void UpdateBookmark();
	void DisplayBookmark();

	void LoadGlobalBookmarks();
	void LoadSiteSpecificBookmarks();

	void SaveSiteSpecificBookmarks();
	void SaveGlobalBookmarks();

	wxWindow* m_parent{};
	COptionsBase & options_;
	login_manager & login_manager_;
	std::wstring &m_site_path;
	Site const* site_{};

	wxTreeCtrlEx* tree_{};
	wxTreeItemId m_bookmarks_global;
	wxTreeItemId m_bookmarks_site;

	bool m_is_deleting{};

	DECLARE_EVENT_TABLE()
	void OnSelChanging(wxTreeEvent& event);
	void OnSelChanged(wxTreeEvent& event);
	void OnOK(wxCommandEvent& event);
	void OnBrowse(wxCommandEvent& event);
	void OnNewBookmark(wxCommandEvent& event);
	void OnRename(wxCommandEvent& event);
	void OnDelete(wxCommandEvent& event);
	void OnCopy(wxCommandEvent& event);
	void OnBeginLabelEdit(wxTreeEvent& event);
	void OnEndLabelEdit(wxTreeEvent& event);
};

#endif
