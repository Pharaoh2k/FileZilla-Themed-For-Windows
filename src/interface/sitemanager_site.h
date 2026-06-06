#ifndef FILEZILLA_INTERFACE_SITEMANAGER_SITE_HEADER
#define FILEZILLA_INTERFACE_SITEMANAGER_SITE_HEADER

#include <wx/notebook.h>

class Site;
class SiteControls;
class COptionsBase;
class wxChoice;
class wxTextCtrlEx;
class CSiteManagerSite : public wxNotebook
{
public:
	CSiteManagerSite(COptionsBase & options);

	bool Load(wxWindow * parent, bool with_comments_and_color = true);

	bool UpdateSite(Site &site, bool silent);
	void SetSite(Site const& site, bool predefined);

private:
	void SetControlVisibility(ServerProtocol protocol, LogonType type);

	COptionsBase & options_;

	wxNotebookPage *generalPage_{};
	wxNotebookPage *advancedPage_{};
	wxNotebookPage *charsetPage_{};
	wxNotebookPage *transferPage_{};
	wxNotebookPage *sftpPage_{};
	wxNotebookPage *s3Page_{};
	wxNotebookPage *dropboxPage_{};
	wxNotebookPage *swiftPage_{};

	wxTextCtrlEx* comments_{};
	wxChoice* color_{};

	std::vector<std::unique_ptr<SiteControls>> controls_;
	wxString m_charsetPageText;
	size_t m_totalPages = -1;

	bool predefined_{};
};

#endif
