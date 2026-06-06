#include "filezilla.h"
#include "welcome_dialog.h"
#include "buildinfo.h"
#include "Options.h"
#include "themeprovider.h"
#include "xrc_helper.h"
#include "../commonui/updater.h"
#include "../include/version.h"
#include <wx/hyperlink.h>
#include <wx/statbmp.h>
#include <wx/statline.h>

BEGIN_EVENT_TABLE(CWelcomeDialog, wxDialogEx)
EVT_TIMER(wxID_ANY, CWelcomeDialog::OnTimer)
END_EVENT_TABLE()

CWelcomeDialog::CWelcomeDialog(COptionsBase & options, wxWindow* parent)
    : options_(options)
    , parent_(parent)
{
}

void CWelcomeDialog::RunDelayed()
{
	m_delayedShowTimer.SetOwner(this);
	m_delayedShowTimer.Start(10, true);
}

bool CWelcomeDialog::Run(bool force)
{
	auto const ownVersion = GetFileZillaVersion();
	auto const greetingVersion = options_.get_string(OPTION_GREETINGVERSION);

	auto const resources = options_.get_string(OPTION_GREETINGRESOURCES);
	options_.set(OPTION_GREETINGRESOURCES, L"");

	if (!force) {
		if (options_.get_int(OPTION_DEFAULT_KIOSKMODE) == 2) {
			return true;
		}

		if (!ownVersion.empty() && !greetingVersion.empty() &&
			ConvertToVersionNumber(ownVersion.c_str()) <= ConvertToVersionNumber(greetingVersion.c_str()))
		{
			// Been there done that
			return true;
		}
		options_.set(OPTION_GREETINGVERSION, ownVersion);

		if (greetingVersion.empty() && !options_.get_int(OPTION_DEFAULT_KIOSKMODE)) {
			options_.set(OPTION_PROMPTPASSWORDSAVE, 1);
		}
	}


	Create(parent_, -1, _("Welcome to FileZilla"));

	auto const& lay = layout();
	auto outer = new wxBoxSizer(wxVERTICAL);
	SetSizer(outer);

	auto main = lay.createFlex(1);
	outer->Add(main, 0, wxALL, lay.border);


	auto header = new wxBoxSizer(wxHORIZONTAL);
	main->Add(header, lay.grow);

	auto headerLeft = lay.createFlex(1);
	header->Add(headerLeft, lay.valign)->SetProportion(1);

	auto heading = new wxStaticText(this, -1, _T("FileZilla ") + GetFileZillaVersion());
	heading->SetFont(heading->GetFont().Bold());
	headerLeft->Add(heading);
	headerLeft->Add(new wxStaticText(this, -1, _("The free open source FTP solution")));

	header->AddSpacer(lay.dlgUnits(5));

	header->Add(CThemeProvider::Get()->createStaticBitmap(this, L"ART_FILEZILLA", iconSizeLarge));

	main->Add(new wxStaticLine(this), lay.grow);

	wxString const url = _T("https://welcome.filezilla-project.org/welcome?type=client&category=%s&version=") + ownVersion;

	main->Add(new wxPanel(this, XRCID("ID_HEADERMESSAGE_PANEL")), lay.halign)->Show(false);

	if (!greetingVersion.empty() && !ownVersion.empty()) {
		auto news = new wxStaticText(this, -1, _("What's new"));
		news->SetFont(news->GetFont().Bold());
		main->Add(news);
		main->Add(new wxHyperlinkCtrl(this, -1, wxString::Format(_("New features and improvements in %s"), ownVersion), wxString::Format(url, _T("news")) + _T("&oldversion=") + greetingVersion), 0, wxLEFT, lay.indent);
	}

	main->AddSpacer(0);

	auto helpHeading = new wxStaticText(this, -1, _("Getting help"));
	helpHeading->SetFont(helpHeading->GetFont().Bold());
	main->Add(helpHeading);

	main->Add(new wxHyperlinkCtrl(this, -1, _("Asking questions in the FileZilla Forums"), wxString::Format(url, _T("support_forum"))), 0, wxLEFT, lay.indent);
	main->Add(new wxHyperlinkCtrl(this, -1, _("Reporting bugs and feature requests"), wxString::Format(url, _T("support_more"))), 0, wxLEFT, lay.indent);

	main->AddSpacer(0);

	auto documentationHeading = new wxStaticText(this, -1, _("Documentation"));
	documentationHeading->SetFont(helpHeading->GetFont().Bold());
	main->Add(documentationHeading);

	main->Add(new wxHyperlinkCtrl(this, -1, _("Basic usage instructions"), wxString::Format(url, _T("documentation_basic"))), 0, wxLEFT, lay.indent);
	main->Add(new wxHyperlinkCtrl(this, -1, _("Configuring FileZilla and your network"), wxString::Format(url, _T("documentation_network"))), 0, wxLEFT, lay.indent);
	main->Add(new wxHyperlinkCtrl(this, -1, _("Further documentation"), wxString::Format(url, _T("documentation_more"))), 0, wxLEFT, lay.indent);

	main->Add(new wxStaticText(this, -1, _("You can always open this dialog again through the help menu.")));

	main->Add(new wxPanel(this, XRCID("ID_FOOTERMESSAGE_PANEL")), lay.halign)->Show(false);

	auto buttons = lay.createButtonSizer(this, main, true);

	auto ok = new wxButton(this, XRCID("wxID_OK"), _("OK"));
	ok->SetFocus();
	buttons->AddButton(ok);
	ok->SetDefault();

	buttons->Realize();

	InitFooter(force ? std::wstring() : resources);

	Layout();

	GetSizer()->Fit(this);

	ShowModal();

	return true;
}

void CWelcomeDialog::OnTimer(wxTimerEvent&)
{
	if (CanShowPopupDialog()) {
		Run(false);
	}
	Destroy();
}

#if FZ_MANUALUPDATECHECK
void MakeLinksFromTooltips(wxWindow& parent);

namespace {
void CreateMessagePanel(wxWindow& dlg, char const* ctrl, wxXmlResource& resource, wxString const& resourceName)
{
	wxWindow* parent = XRCCTRL(dlg, ctrl, wxPanel);
	if (parent) {
		wxPanel* p = new wxPanel();
		if (resource.LoadPanel(p, parent, resourceName)) {
			wxSize minSize = p->GetSizer()->GetMinSize();
			parent->SetInitialSize(minSize);
			MakeLinksFromTooltips(*p);
			parent->GetContainingSizer()->Show(parent);
		}
		else {
			delete p;
		}
	}
}
}
#endif

void CWelcomeDialog::InitFooter(std::wstring const& resources)
{
#if FZ_MANUALUPDATECHECK
	if (CBuildInfo::GetBuildType() != L"official" || options_.get_bool(OPTION_DISABLE_UPDATE_FOOTER)) {
		return;
	}

	if (!resources.empty()) {
		wxLogNull null;

		wxXmlResource res(wxXRC_NO_RELOADING);
		InitHandlers(res);
		if (res.Load(_T("blob64:") + resources)) {
			CreateMessagePanel(*this, "ID_HEADERMESSAGE_PANEL", res, _T("ID_WELCOME_HEADER"));
			CreateMessagePanel(*this, "ID_FOOTERMESSAGE_PANEL", res, _T("ID_WELCOME_FOOTER"));
		}
	}
#else
	(void)resources;
#endif
}
