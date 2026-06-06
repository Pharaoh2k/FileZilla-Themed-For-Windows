#include "filezilla.h"

#include "update_dialog.h"

#if FZ_MANUALUPDATECHECK

#include "buildinfo.h"
#include "filezillaapp.h"
#include "file_utils.h"
#include "textctrlex.h"
#include "themeprovider.h"
#include "xrc_helper.h"
#include "Options.h"

#include <libfilezilla/process.hpp>

#include <wx/animate.h>
#include <wx/display.h>
#include <wx/hyperlink.h>
#include <wx/statline.h>

BEGIN_EVENT_TABLE(CUpdateDialog, wxDialogEx)
EVT_BUTTON(XRCID("ID_INSTALL"), CUpdateDialog::OnInstall)
EVT_TIMER(wxID_ANY, CUpdateDialog::OnTimer)
EVT_HYPERLINK(XRCID("ID_SHOW_DETAILS"), CUpdateDialog::ShowDetails)
EVT_HYPERLINK(XRCID("ID_SHOW_DETAILS_DL"), CUpdateDialog::ShowDetailsDl)
EVT_HYPERLINK(XRCID("ID_RETRY"), CUpdateDialog::Retry)
EVT_HYPERLINK(XRCID("ID_DOWNLOAD_RETRY"), CUpdateDialog::Retry)
EVT_BUTTON(XRCID("ID_DEBUGLOG"), CUpdateDialog::OnDebugLog)
END_EVENT_TABLE()

namespace pagenames {
enum type {
	checking,
	failed,
	newversion,
	latest
};
}

static int refcount = 0;

CUpdateDialog::CUpdateDialog(wxWindow* parent, CUpdater& updater, COptionsBase & options)
	: parent_(parent)
	, updater_(updater)
	, options_(options)
{
	timer_.SetOwner(this);
	++refcount;
}

CUpdateDialog::~CUpdateDialog()
{
	--refcount;
}

bool CUpdateDialog::IsRunning()
{
	return refcount != 0;
}

int CUpdateDialog::ShowModal()
{
	wxString version(PACKAGE_VERSION, wxConvLocal);
	if (version.empty() || version[0] < '0' || version[0] > '9') {
		wxMessageBoxEx(_("Executable contains no version info, cannot check for updates."), _("Check for updates failed"), wxICON_ERROR, parent_);
		return wxID_CANCEL;
	}

	if (!Create(parent_, wxID_ANY, _("Check for Updates"))) {
		return wxID_CANCEL;
	}


	auto const& lay = layout();
	auto outer = new wxBoxSizer(wxVERTICAL);
	SetSizer(outer);

	auto main = lay.createFlex(1);
	outer->Add(main, 1, wxALL|wxGROW, lay.border);

	main->AddGrowableCol(0);
	main->AddGrowableRow(0);

	content_ = new wxPanel(this);
	main->Add(content_, 1, wxGROW);
	main->Add(new wxStaticLine(this), lay.grow);

	auto buttons = new wxBoxSizer(wxHORIZONTAL);
	main->Add(buttons, 0, wxALIGN_RIGHT);

	bool const debug = GetEnv("FZDEBUG") == L"1";
	buttons->Add(new wxButton(this, XRCID("ID_DEBUGLOG"), L"show log"))->Show(debug);
	buttons->Add(new wxButton(this, wxID_CANCEL, _("&Close")));

	wxAnimation throbber = CThemeProvider::Get()->CreateAnimation(_T("ART_THROBBER"), wxSize(16, 16));

	{
		auto p = new wxPanel(content_, XRCID("ID_CHECKING_PANEL"));
		panels_.push_back(p);

		auto s = lay.createMain(p, 1);

		s->AddGrowableCol(0);
		s->AddGrowableRow(0);
		s->AddGrowableRow(2);

		s->AddStretchSpacer();

		auto row = lay.createFlex(0, 1);
		s->Add(row, 0, wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL);
		
		row->Add(new wxStaticText(p, -1, _("Checking for updates...")), lay.valign);

		if (throbber.IsOk()) {
			auto anim = new wxAnimationCtrl(p, -1, throbber);
			anim->SetMinSize(throbber.GetSize());
			anim->Play();
			row->Add(anim, lay.valign);
		}

		s->AddStretchSpacer();
	}

	{
		auto p = new wxPanel(content_, XRCID("ID_FAILURE_PANEL"));
		panels_.push_back(p);

		auto s = lay.createMain(p, 1);
		s->AddGrowableCol(0);
		s->AddGrowableRow(2);
		
		s->Add(new wxStaticText(p, -1, _("Information about the latest version of FileZilla could not be retrieved. Please try again later.")));
		s->Add(new wxHyperlinkCtrl(p, XRCID("ID_RETRY"), _("Try again"), wxString()));

		s->Add(new wxTextCtrlEx(p, XRCID("ID_DETAILS"), wxString(), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL | wxTE_DONTWRAP), 1, wxGROW)->SetMinSize(wxSize(-1, 200));
		s->Add(new wxHyperlinkCtrl(p, XRCID("ID_SHOW_DETAILS"), _("Show details"), wxString()), lay.valign);

		s->AddSpacer(lay.dlgUnits(5));

		s->Add(new wxStaticText(p, XRCID("ID_WEBSITE_TEXT"), _("You can download the latest version from the FileZilla website:")));
		s->Add(new wxHyperlinkCtrl(p, XRCID("ID_WEBSITE_LINK"), L"https://filezilla-project.org/", L"https://filezilla-project.org/"));

	}

	{
		auto p = new wxPanel(content_, XRCID("ID_NEWVERSION_PANEL"));
		panels_.push_back(p);

		auto s = lay.createMain(p, 1);
		s->AddGrowableRow(4);
		s->AddGrowableCol(0);
		s->Add(new wxStaticText(p, -1, _("A new version of FileZilla is available:")));
		s->Add(new wxStaticText(p, XRCID("ID_VERSION"), L"1.2.3.4"));
		s->AddSpacer(0);
		s->Add(new wxStaticText(p, XRCID("ID_NEWS_LABEL"), _("What's new:")));
		s->Add(new wxTextCtrlEx(p, XRCID("ID_NEWS"), wxString(), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY), 1, wxGROW)->SetMinSize(400, 150);

		auto dl = lay.createFlex(0, 1);
		s->Add(dl, lay.halign);
		dl->Add(new wxStaticText(p, XRCID("ID_DOWNLOAD_LABEL"), _("Downloading update...")), lay.valign);

		if (throbber.IsOk()) {
			auto anim = new wxAnimationCtrl(p, XRCID("ID_WAIT_DOWNLOAD"), throbber);
			anim->SetMinSize(throbber.GetSize());
			anim->Play();
			dl->Add(anim, lay.valign);
		}

		dl->Add(new wxStaticText(p, XRCID("ID_DOWNLOAD_PROGRESS"), L"12% downloaded"), lay.valign);

		s->Add(new wxStaticText(p, XRCID("ID_DOWNLOADED"), _("The new version has been saved in your Downloads directory.")));
		auto install = new wxButton(p, XRCID("ID_INSTALL"), _("&Install new version"));
		install->SetDefault();
		s->Add(install, lay.halign);
		s->Add(new wxStaticText(p, XRCID("ID_OUTDATED"), _("Unfortunately information about the new update could not be retrieved.")));
		s->Add(new wxStaticText(p, XRCID("ID_DISABLED_CHECK"), _("Either you or your system administrator has disabled checking for updates. Please re-enable checking for updates to obtain more information.")));
		s->Add(new wxStaticText(p, XRCID("ID_DOWNLOAD_FAIL"), _("The new version could not be downloaded, please retry later.")));

		s->Add(new wxTextCtrlEx(p, XRCID("ID_DETAILS_DL"), wxString(), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL | wxTE_DONTWRAP), 1, wxGROW)->SetMinSize(wxSize(-1, 0));
		auto retry = lay.createFlex(0, 1);
		s->Add(retry);
		retry->Add(new wxHyperlinkCtrl(p, XRCID("ID_DOWNLOAD_RETRY"), _("Try again"), wxString()), lay.valign);
		retry->Add(new wxHyperlinkCtrl(p, XRCID("ID_SHOW_DETAILS_DL"), _("Show details"), wxString()), lay.valign);

		s->Add(new wxStaticText(p, XRCID("ID_NEWVERSION_WEBSITE_TEXT_DLFAIL"), _("Alternatively, you can also download the latest version from the FileZilla website:")));
		s->Add(new wxStaticText(p, XRCID("ID_NEWVERSION_WEBSITE_TEXT"), _("You can download the latest version from the FileZilla website:")));
		s->Add(new wxHyperlinkCtrl(p, XRCID("ID_NEWVERSION_WEBSITE_LINK"), L"https://filezilla-project.org/", L"https://filezilla-project.org/"));
	}

	{
		auto p = new wxPanel(content_, XRCID("ID_LATEST_PANEL"));
		panels_.push_back(p);

		auto s = lay.createMain(p, 1);

		s->AddGrowableCol(0);
		s->AddGrowableRow(0);
		s->AddGrowableRow(2);

		s->AddStretchSpacer();
		s->Add(new wxStaticText(p, -1, _("You are using the latest version of FileZilla."), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL), lay.grow);
		s->AddStretchSpacer();
	}

	InitFooter();

	Wrap();

	xrc_call(*this, "ID_DETAILS", &wxTextCtrl::Hide);
	xrc_call(*this, "ID_DETAILS_DL", &wxTextCtrl::Hide);

	UpdaterState s = updater_.GetState();
	UpdaterStateChanged(s, updater_.AvailableBuild());

	updater_.AddHandler(*this);

	int ret = wxDialogEx::ShowModal();
	updater_.RemoveHandler(*this);

	return ret;
}

void MakeLinksFromTooltips(wxWindow& parent)
{
	// Iterates over all children.
	// If tooltip is a URL, make the child have the hand cursor
	// and launch URL in browser upon click.
	wxWindowList children = parent.GetChildren();
	for (auto child : children) {
		wxString const tooltip = child->GetToolTipText();
		if (tooltip.find(_T("http://")) == 0 || tooltip.find(_T("https://")) == 0) {
			child->SetCursor(wxCURSOR_HAND);
			child->Bind(wxEVT_LEFT_UP, [tooltip](wxEvent const&) { wxLaunchDefaultBrowser(tooltip); });
		}
		MakeLinksFromTooltips(*child);
	}
}

void CUpdateDialog::InitFooter()
{
	if (CBuildInfo::GetBuildType() != _T("official") || options_.get_bool(OPTION_DISABLE_UPDATE_FOOTER)) {
		return;
	}

	int d = wxDisplay::GetFromWindow(this);
	if (d != wxNOT_FOUND) {
		wxDisplay display(d);
		wxRect r = display.GetGeometry();
		if (r.GetHeight() < 960) {
			return;
		}
	}

	wxString const resources = updater_.GetResources(resource_type::update_dialog);
	if (!resources.empty()) {
		wxLogNull null;

		wxXmlResource res(wxXRC_NO_RELOADING);
		InitHandlers(res);
		if (res.Load(_T("blob64:") + resources)) {
			auto sizer = xrc_call(*this, "ID_NEWVERSION_PANEL", &wxPanel::GetSizer);
			if (sizer) {
				wxPanel* p{};
				bool top{};
				if ((p = res.LoadPanel(sizer->GetContainingWindow(), _T("ID_UPDATE_FOOTER")))) {
					top = false;
				}
				else if ((p = res.LoadPanel(sizer->GetContainingWindow(), _T("ID_UPDATE_HEADER")))) {
					top = true;
				}

				if (p) {
					MakeLinksFromTooltips(*p);
					sizer->Insert(top ? 0 : 1, p, wxSizerFlags().Align(wxALIGN_CENTER_HORIZONTAL).Border(top ? wxBOTTOM : wxTOP, 5));
				}
			}
		}
	}
}

void CUpdateDialog::Wrap()
{
	if (!content_) {
		return;
	}

	GetSizer()->Fit(this);

	wxSize canvas;
	canvas.x = GetSize().x - content_->GetSize().x;
	canvas.y = GetSize().y - content_->GetSize().y;

	// Wrap pages nicely
	std::vector<wxWindow*> pages;
	for (auto const& panel : panels_) {
		pages.push_back(panel);
	}
	wxGetApp().GetWrapEngine()->WrapRecursive(pages, 1.33, "Update", canvas);

	// Keep track of maximum page size
	wxSize size = GetSizer()->GetMinSize();
	for (auto const& panel : panels_) {
		if (panel->GetSizer()) {
			size.IncTo(panel->GetSizer()->GetMinSize());
		}
	}

	wxSize panelSize = size;
#ifdef __WXGTK__
	panelSize.x += 1;
#endif
	content_->SetInitialSize(panelSize);

	// Adjust pages sizes according to maximum size
	for (auto const& panel : panels_) {
		if (panel->GetSizer()) {
			panel->GetSizer()->SetMinSize(size);
			panel->GetSizer()->Fit(panel);
			panel->GetSizer()->SetSizeHints(panel);
		}
		if (GetLayoutDirection() == wxLayout_RightToLeft) {
			panel->Move(wxPoint(0, 0));
		}
	}

	GetSizer()->Fit(this);
	GetSizer()->SetSizeHints(this);

#ifdef __WXGTK__
	// Pre-show dialog under GTK, else panels won't get initialized properly
	Show();
#endif

	for (auto const& panel : panels_) {
		panel->Hide();
	}
	panels_[0]->Show();
}

void CUpdateDialog::UpdaterStateChanged(UpdaterState s, build const& v)
{
	CallAfter([this, s, v]() {
		timer_.Stop();
		for (auto const& panel : panels_) {
			panel->Hide();
		}
		if (s == UpdaterState::idle) {
			panels_[pagenames::latest]->Show();
		}
		else if (s == UpdaterState::failed) {
			xrc_call(*this, "ID_DETAILS", &wxTextCtrl::ChangeValue, updater_.GetLog());
			panels_[pagenames::failed]->Show();
		}
		else if (s == UpdaterState::checking) {
			panels_[pagenames::checking]->Show();
		}
		else if (s == UpdaterState::newversion || s == UpdaterState::newversion_ready || s == UpdaterState::newversion_downloading || s == UpdaterState::newversion_stale) {
			xrc_call(*this, "ID_VERSION", &wxStaticText::SetLabel, v.version_);

			wxString news = updater_.GetChangelog();

			auto pos = news.find(v.version_ + _T(" (2"));
			if (pos != wxString::npos) {
				news = news.Mid(pos);
			}

			XRCCTRL(*this, "ID_NEWS_LABEL", wxStaticText)->Show(!news.empty());
			XRCCTRL(*this, "ID_NEWS", wxTextCtrl)->Show(!news.empty());
			if (news != XRCCTRL(*this, "ID_NEWS", wxTextCtrl)->GetValue()) {
				XRCCTRL(*this, "ID_NEWS", wxTextCtrl)->ChangeValue(news);
			}
			bool downloading = s == UpdaterState::newversion_downloading;
			XRCCTRL(*this, "ID_DOWNLOAD_LABEL", wxStaticText)->Show(downloading);
			auto anim = FindWindow(XRCID("ID_WAIT_DOWNLOAD"));
			if (anim) {
				anim->Show(downloading);
			}
			XRCCTRL(*this, "ID_DOWNLOAD_PROGRESS", wxStaticText)->Show(downloading);
			if (downloading) {
				timer_.Start(500);
				UpdateProgress();
			}

			bool ready = s == UpdaterState::newversion_ready;
			XRCCTRL(*this, "ID_DOWNLOADED", wxStaticText)->Show(ready);
			XRCCTRL(*this, "ID_INSTALL", wxButton)->Show(ready);

			bool const outdated = s == UpdaterState::newversion_stale;
			bool const manual = s == UpdaterState::newversion || outdated;
			bool const dlfail = s == UpdaterState::newversion && !v.url_.empty();
			bool const disabled = options_.get_int(OPTION_DEFAULT_DISABLEUPDATECHECK) != 0 || !options_.get_int(OPTION_UPDATECHECK);

			XRCCTRL(*this, "ID_OUTDATED", wxStaticText)->Show(outdated);
			XRCCTRL(*this, "ID_DISABLED_CHECK", wxStaticText)->Show(outdated && disabled);
			XRCCTRL(*this, "ID_DOWNLOAD_FAIL", wxStaticText)->Show(dlfail);
			XRCCTRL(*this, "ID_DOWNLOAD_RETRY", wxHyperlinkCtrl)->Show(dlfail);
			XRCCTRL(*this, "ID_SHOW_DETAILS_DL", wxHyperlinkCtrl)->Show(dlfail);

			XRCCTRL(*this, "ID_DETAILS_DL", wxTextCtrl)->ChangeValue(updater_.GetLog());

			XRCCTRL(*this, "ID_NEWVERSION_WEBSITE_TEXT", wxStaticText)->Show(manual && !dlfail);
			XRCCTRL(*this, "ID_NEWVERSION_WEBSITE_TEXT_DLFAIL", wxStaticText)->Show(manual && dlfail);
			XRCCTRL(*this, "ID_NEWVERSION_WEBSITE_LINK", wxHyperlinkCtrl)->Show(manual);

			panels_[pagenames::newversion]->Show();
			panels_[pagenames::newversion]->Layout();
		}
	});
}

void CUpdateDialog::OnInstall(wxCommandEvent&)
{
	std::wstring f = updater_.DownloadedFile();
	if (f.empty()) {
		return;
	}
	options_.set(OPTION_GREETINGRESOURCES, updater_.GetResources(resource_type::update_dialog));
#ifdef __WXMSW__
	std::vector<std::wstring> cmd_with_args;
	cmd_with_args.push_back(f);
	cmd_with_args.push_back(L"/update");
	cmd_with_args.push_back(L"/NCRC");
	fz::spawn_detached_process(cmd_with_args);
#else
	auto cmd_with_args = GetSystemAssociation(f);
	if (!cmd_with_args.empty()) {
		if (fz::spawn_detached_process(AssociationToCommand(cmd_with_args, f))) {
			return;
		}
	}

	wxFileName fn(f);
	OpenInFileManager(fn.GetPath().ToStdWstring());
#endif

	wxWindow* p = parent_;
	while (p->GetParent()) {
		p = p->GetParent();
	}
	p->Close();
}

void CUpdateDialog::OnTimer(wxTimerEvent&)
{
	UpdateProgress();
}

void CUpdateDialog::UpdateProgress()
{
	int64_t size = updater_.AvailableBuild().size_;
	int64_t downloaded = updater_.BytesDownloaded();

	unsigned int percent = 0;
	if (size > 0 && downloaded >= 0) {
		percent = static_cast<unsigned int>((downloaded * 100) / size);
	}

	XRCCTRL(*this, "ID_DOWNLOAD_PROGRESS", wxStaticText)->SetLabel(wxString::Format(_("(%u%% downloaded)"), percent));
}

void CUpdateDialog::ShowDetails(wxHyperlinkEvent&)
{
	XRCCTRL(*this, "ID_SHOW_DETAILS", wxHyperlinkCtrl)->Hide();
	XRCCTRL(*this, "ID_DETAILS", wxTextCtrl)->Show();

	panels_[pagenames::failed]->Layout();
}

void CUpdateDialog::ShowDetailsDl(wxHyperlinkEvent&)
{
	XRCCTRL(*this, "ID_SHOW_DETAILS_DL", wxHyperlinkCtrl)->Hide();
	XRCCTRL(*this, "ID_DETAILS_DL", wxTextCtrl)->Show();
	XRCCTRL(*this, "ID_DETAILS_DL", wxTextCtrl)->SetMinSize(wxSize(-1, 200));

	panels_[pagenames::newversion]->Layout();
}

void CUpdateDialog::Retry(wxHyperlinkEvent&)
{
	updater_.Run(true);
}

void CUpdateDialog::OnDebugLog(wxCommandEvent&)
{
	wxMessageBoxEx(updater_.GetLog());
}

#endif
