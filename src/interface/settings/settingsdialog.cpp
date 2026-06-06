#include "../filezilla.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_connection.h"
#include "optionspage_connection_ftp.h"
#include "optionspage_connection_active.h"
#include "optionspage_connection_passive.h"
#include "optionspage_ftpproxy.h"
#include "optionspage_connection_sftp.h"
#include "optionspage_filetype.h"
#include "optionspage_fileexists.h"
#include "optionspage_themes.h"
#include "optionspage_language.h"
#include "optionspage_transfer.h"
#include "optionspage_updatecheck.h"
#include "optionspage_logging.h"
#include "optionspage_debug.h"
#include "optionspage_interface.h"
#include "optionspage_dateformatting.h"
#include "optionspage_sizeformatting.h"
#include "optionspage_edit.h"
#include "optionspage_edit_associations.h"
#include "optionspage_passwords.h"
#include "optionspage_proxy.h"
#include "optionspage_filelists.h"
#include "../filezillaapp.h"
#include "../Mainfrm.h"
#include "../treectrlex.h"

CSettingsDialog::CSettingsDialog(COptions & options, login_manager & lim, CFileZillaEngineContext & engine_context)
	: options_(options)
	, login_manager_(lim)
	, m_engine_context(engine_context)
{
}

CSettingsDialog::~CSettingsDialog()
{
	m_activePanel = nullptr;
	m_pages.clear();

	if (tree_) {
		// Trees can generate these events during destruction, not good.
		tree_->Unbind(wxEVT_TREE_SEL_CHANGING, &CSettingsDialog::OnPageChanging, this);
		tree_->Unbind(wxEVT_TREE_SEL_CHANGED, &CSettingsDialog::OnPageChanged, this);
	}
}

bool CSettingsDialog::Create(CMainFrame* pMainFrame)
{
	m_pMainFrame = pMainFrame;

	SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
	if (!wxDialogEx::Create(pMainFrame, nullID, _("Settings"))) {
		return false;
	}

	auto & lay = layout();
	auto * main = lay.createMain(this, 2);
	main->AddGrowableRow(0);

	auto* left = lay.createFlex(1);
	left->AddGrowableRow(1);
	main->Add(left, 1, wxGROW);

	left->Add(new wxStaticText(this, nullID, _("Select &page:")));

	tree_ = new wxTreeCtrlEx(this, nullID, wxDefaultPosition, wxDefaultSize, DEFAULT_TREE_STYLE | wxTR_HIDE_ROOT);
	tree_->SetFocus();
	left->Add(tree_, 1, wxGROW);

	auto ok = new wxButton(this, wxID_OK, _("OK"));
	ok->Bind(wxEVT_BUTTON, &CSettingsDialog::OnOK, this);
	ok->SetDefault();
	left->Add(ok, lay.grow);
	auto cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
	cancel->Bind(wxEVT_BUTTON, &CSettingsDialog::OnCancel, this);
	left->Add(cancel, lay.grow);

	pagePanel_ = new wxPanel(this);
	main->Add(pagePanel_, lay.grow);

	tree_->Bind(wxEVT_TREE_SEL_CHANGING, &CSettingsDialog::OnPageChanging, this);
	tree_->Bind(wxEVT_TREE_SEL_CHANGED, &CSettingsDialog::OnPageChanged, this);

	if (!LoadPages()) {
		return false;
	}

	return true;
}

void CSettingsDialog::AddPage(wxString const& name, COptionsPage* page, int nest)
{
	wxTreeItemId parent = tree_->GetRootItem();
	while (nest--) {
		parent = tree_->GetLastChild(parent);
		wxCHECK_RET(parent != wxTreeItemId(), "Nesting level too deep");
	}

	t_page p;
	p.page = page;
	p.id = tree_->AppendItem(parent, name);
	if (parent != tree_->GetRootItem()) {
		tree_->Expand(parent);
	}

	m_pages.push_back(p);
}

bool CSettingsDialog::LoadPages()
{
	// Get the tree control.

	if (!tree_) {
		return false;
	}

	tree_->AddRoot(wxString());

	// Create the instances of the page classes and fill the tree.
	AddPage(_("Connection"), new COptionsPageConnection, 0);
#if ENABLE_FTP
	AddPage(_("FTP"), new COptionsPageConnectionFTP, 1);
	AddPage(_("Active mode"), new COptionsPageConnectionActive, 2);
	AddPage(_("Passive mode"), new COptionsPageConnectionPassive, 2);
	AddPage(_("FTP Proxy"), new COptionsPageFtpProxy, 2);
#endif
#if ENABLE_SFTP
	AddPage(_("SFTP"), new COptionsPageConnectionSFTP, 1);
#endif
	AddPage(_("Generic proxy"), new COptionsPageProxy, 1);
	AddPage(_("Transfers"), new COptionsPageTransfer, 0);
#if ENABLE_FTP
	AddPage(_("FTP: File Types"), new COptionsPageFiletype, 1);
#endif
	AddPage(_("File exists action"), new COptionsPageFileExists, 1);
	AddPage(_("Interface"), new COptionsPageInterface, 0);
	AddPage(_("Passwords"), new COptionsPagePasswords(login_manager_), 1);
	AddPage(_("Themes"), new COptionsPageThemes, 1);
	AddPage(_("Date/time format"), new COptionsPageDateFormatting, 1);
	AddPage(_("Filesize format"), new COptionsPageSizeFormatting, 1);
	AddPage(_("File lists"), new COptionsPageFilelists, 1);
	AddPage(_("Language"), new COptionsPageLanguage, 0);
	AddPage(_("File editing"), new COptionsPageEdit, 0);
	AddPage(_("Filetype associations"), new COptionsPageEditAssociations, 1);
#if FZ_MANUALUPDATECHECK && FZ_AUTOUPDATECHECK
	if (!options_.get_int(OPTION_DEFAULT_DISABLEUPDATECHECK)) {
		AddPage(_("Updates"), new COptionsPageUpdateCheck, 0);
	}
#endif //FZ_MANUALUPDATECHECK && FZ_AUTOUPDATECHECK
	AddPage(_("Logging"), new COptionsPageLogging, 0);
	AddPage(_("Debug"), new COptionsPageDebug, 0);

	tree_->SetQuickBestSize(false);
	tree_->InvalidateBestSize();
	tree_->SetInitialSize();

	// Compensate for scrollbar
	wxSize size = tree_->GetBestSize();
	int scrollWidth = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, tree_);
	size.x += scrollWidth + 2;
	size.y = 400;
	tree_->SetInitialSize(size);
	Layout();

	// Before we can initialize the pages, get the target panel in the settings
	// dialog.
	if (!pagePanel_) {
		return false;
	}

	// Keep track of maximum page size
	size = wxSize();

	for (auto const& page : m_pages) {
		if (!page.page->CreatePage(options_, this, pagePanel_, size)) {
			return false;
		}
	}

	if (!LoadSettings()) {
		wxMessageBoxEx(_("Failed to load panels, invalid resource files?"));
		return false;
	}

	wxSize canvas;
	canvas.x = GetSize().x - pagePanel_->GetSize().x;
	canvas.y = GetSize().y - pagePanel_->GetSize().y;

	// Wrap pages nicely
	std::vector<wxWindow*> pages;
	for (auto const& page : m_pages) {
		pages.push_back(page.page);
	}
	wxGetApp().GetWrapEngine()->WrapRecursive(pages, 1.33, "Settings", canvas);

#ifdef __WXGTK__
	// Pre-show dialog under GTK, else panels won't get initialized properly
	Show();
#endif

	GetSizer()->Fit(this);

	// Keep track of maximum page size
	size = pagePanel_->GetClientSize();
	for (auto const& page : m_pages) {
		auto sizer = page.page->GetSizer();
		if (sizer) {
			size.IncTo(sizer->GetMinSize());
		}
	}

	wxSize panelSize = size;
#ifdef __WXGTK__
	panelSize.x += 1;
#endif
	pagePanel_->SetInitialSize(panelSize);

	// Adjust pages sizes according to maximum size
	for (auto const& page : m_pages) {
		auto sizer = page.page->GetSizer();
		if (sizer) {
			sizer->SetMinSize(size);
			sizer->Fit(page.page);
			sizer->SetSizeHints(page.page);
		}
		if (GetLayoutDirection() == wxLayout_RightToLeft) {
			page.page->Move(wxPoint(1, 0));
		}
	}

	GetSizer()->Fit(this);

	for (auto const& page : m_pages) {
		page.page->Hide();
	}

	// Select first page
	tree_->SelectItem(m_pages[0].id);
	if (!m_activePanel)	{
		m_activePanel = m_pages[0].page;
		m_activePanel->Display();
	}

	return true;
}

bool CSettingsDialog::LoadSettings()
{
	for (auto const& page : m_pages) {
		if (!page.page->LoadPage()) {
			return false;
		}
	}

	return true;
}

void CSettingsDialog::OnPageChanged(wxTreeEvent& event)
{
	if (m_activePanel) {
		m_activePanel->Hide();
	}

	wxTreeItemId item = event.GetItem();

	for (auto const& page : m_pages) {
		if (page.id == item) {
			m_activePanel = page.page;
			m_activePanel->Display();
			break;
		}
	}
}

void CSettingsDialog::OnOK(wxCommandEvent&)
{
	for (auto const& page : m_pages) {
		if (!page.page->Validate()) {
			if (m_activePanel != page.page) {
				tree_->SelectItem(page.id);
			}
			return;
		}
	}

	for (auto const& page : m_pages) {
		page.page->SavePage();
	}

	m_activePanel = nullptr;
	m_pages.clear();

	EndModal(wxID_OK);
}

void CSettingsDialog::OnCancel(wxCommandEvent&)
{
	m_activePanel = nullptr;
	m_pages.clear();

	EndModal(wxID_CANCEL);

	for (auto const& saved : m_oldValues) {
		options_.set(saved.first, saved.second);
	}
}

void CSettingsDialog::OnPageChanging(wxTreeEvent& event)
{
	if (!m_activePanel) {
		return;
	}

	if (!m_activePanel->Validate()) {
		event.Veto();
	}
}

void CSettingsDialog::RememberOldValue(interfaceOptions option)
{
	m_oldValues[option] = options_.get_string(option);
}
