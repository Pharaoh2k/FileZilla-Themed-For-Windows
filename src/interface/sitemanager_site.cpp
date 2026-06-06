#include "filezilla.h"
#include "sitemanager_site.h"

#include "filezillaapp.h"
#include "Options.h"
#include "dialogex.h"
#include "sitemanager.h"
#include "sitemanager_controls.h"
#include "textctrlex.h"

#include "../include/s3sse.h"

#include <libfilezilla/translate.hpp>

#include <wx/dcclient.h>
#include <wx/statline.h>
#include <wx/wupdlock.h>

#ifdef __WXMSW__
#include "commctrl.h"
#endif

CSiteManagerSite::CSiteManagerSite(COptionsBase & options)
	: options_(options)
{
}

bool CSiteManagerSite::Load(wxWindow* parent, bool with_comments_and_color)
{
	Create(parent, -1);

	DialogLayout lay(static_cast<wxTopLevelWindow*>(wxGetTopLevelParent(parent)));

	{
		auto onChange = [this](ServerProtocol protocol, LogonType type) {
			wxWindowUpdateLocker l(this);
			SetControlVisibility(protocol, type);
			for (auto & controls : controls_) {
				controls->SetControlState(predefined_);
			}
			GetContainingSizer()->Layout();
		};

		generalPage_ = new wxPanel(this);
		AddPage(generalPage_, _("General"));

		auto* main = lay.createMain(generalPage_, 1);
		controls_.emplace_back(std::make_unique<GeneralSiteControls>(*generalPage_, lay, *main, options_, onChange));

		if (with_comments_and_color) {
			main->Add(new wxStaticLine(generalPage_), lay.grow);
			auto row = lay.createFlex(0, 1);
			main->Add(row);
			row->Add(new wxStaticText(generalPage_, -1, _("&Background color:")), lay.valign);
			color_ = new wxChoice(generalPage_, nullID);
			row->Add(color_, lay.valign);

			main->Add(new wxStaticText(generalPage_, -1, _("Co&mments:")));
			comments_ = new wxTextCtrlEx(generalPage_, nullID, L"", wxDefaultPosition, wxSize(-1, lay.dlgUnits(43)), wxTE_MULTILINE);
			main->Add(comments_, 1, wxGROW);
			main->AddGrowableRow(main->GetEffectiveRowsCount() - 1);

			for (int i = 0; ; ++i) {
				wxString name = CSiteManager::GetColourName(i);
				if (name.empty()) {
					break;
				}
				color_->AppendString(wxGetTranslation(name));
			}
			color_->Select(0);
		}
	}

	{
		advancedPage_ = new wxPanel(this);
		AddPage(advancedPage_, _("Advanced"));
		auto * main = lay.createMain(advancedPage_, 1);
		controls_.emplace_back(std::make_unique<AdvancedSiteControls>(*advancedPage_, lay, *main));
	}

	{
		transferPage_ = new wxPanel(this);
		AddPage(transferPage_, _("Transfer Settings"));
		auto * main = lay.createMain(transferPage_, 1);
		controls_.emplace_back(std::make_unique<TransferSettingsSiteControls>(*transferPage_, lay, *main));
	}

	{
		charsetPage_ = new wxPanel(this);
		AddPage(charsetPage_, _("Charset"));
		auto * main = lay.createMain(charsetPage_, 1);
		controls_.emplace_back(std::make_unique<CharsetSiteControls>(*charsetPage_, lay, *main));

		int const charsetPageIndex = FindPage(charsetPage_);
		m_charsetPageText = GetPageText(charsetPageIndex);
		wxGetApp().GetWrapEngine()->WrapRecursive(charsetPage_, 1.3);
	}

	{
		sftpPage_ = new wxPanel(this);
		AddPage(sftpPage_, L"SFTP");
		auto * main = lay.createMain(sftpPage_, 1);
		controls_.emplace_back(std::make_unique<SftpSiteControls>(*sftpPage_, lay, *main));
	}

	{
		s3Page_ = new wxPanel(this);
		AddPage(s3Page_, L"S3");
		auto * main = lay.createMain(s3Page_, 1);
		controls_.emplace_back(std::make_unique<S3SiteControls>(*s3Page_, lay, *main));
	}

	{
		dropboxPage_ = new wxPanel(this);
		AddPage(dropboxPage_, L"Dropbox");
		auto * main = lay.createMain(dropboxPage_, 1);
		controls_.emplace_back(std::make_unique<DropboxSiteControls>(*dropboxPage_, lay, *main));
	}

	{
		swiftPage_ = new wxPanel(this);
		AddPage(swiftPage_, L"Swift");
		auto * main = lay.createMain(swiftPage_, 1);
		controls_.emplace_back(std::make_unique<SwiftSiteControls>(*swiftPage_, lay, *main));
	}

	m_totalPages = GetPageCount();

	if (generalPage_) {
		generalPage_->GetSizer()->Fit(generalPage_);
	}

#ifdef __WXMSW__
	// Make pages at least wide enough to fit all tabs
	HWND hWnd = (HWND)GetHandle();

	int width = 4;
	for (unsigned int i = 0; i < GetPageCount(); ++i) {
		RECT tab_rect{};
		if (TabCtrl_GetItemRect(hWnd, i, &tab_rect)) {
			width += tab_rect.right - tab_rect.left;
		}
	}
#else
	// Make pages at least wide enough to fit all tabs
	int width = 10; // Guessed
	wxClientDC dc(this);
	for (unsigned int i = 0; i < GetPageCount(); ++i) {
		wxCoord w, h;
		dc.GetTextExtent(GetPageText(i), &w, &h);

		width += w;
#ifdef __WXMAC__
		width += 20; // Guessed
#else
		width += 20;
#endif
	}
#endif

	for (auto & c : controls_) {
		c->UpdateWidth(width);
	}

	return true;
}

void CSiteManagerSite::SetControlVisibility(ServerProtocol protocol, LogonType type)
{
	for (auto & controls : controls_) {
		controls->SetControlVisibility(protocol, type);
	}

	if (charsetPage_) {
		if (CServer::ProtocolHasFeature(protocol, ProtocolFeature::Charset)) {
			if (FindPage(charsetPage_) == wxNOT_FOUND) {
				AddPage(charsetPage_, m_charsetPageText);
			}
		}
		else {
			int const charsetPageIndex = FindPage(charsetPage_);
			if (charsetPageIndex != wxNOT_FOUND) {
				RemovePage(charsetPageIndex);
			}
		}
	}

	if (sftpPage_) {
		if (protocol == SFTP) {
			if (FindPage(sftpPage_) == wxNOT_FOUND) {
				AddPage(sftpPage_, L"SFTP");
			}
		}
		else {
			int const sftpPageIndex = FindPage(sftpPage_);
			if (sftpPageIndex != wxNOT_FOUND) {
				RemovePage(sftpPageIndex);
			}
		}
	}

	if (s3Page_) {
		if (protocol == S3 || protocol == S3_SSO) {
			if (FindPage(s3Page_) == wxNOT_FOUND) {
				AddPage(s3Page_, L"S3");
			}
		}
		else {
			int const s3PageIndex = FindPage(s3Page_);
			if (s3PageIndex != wxNOT_FOUND) {
				RemovePage(s3PageIndex);
			}
		}
	}

	if (swiftPage_) {
		if (protocol == SWIFT) {
			if (FindPage(swiftPage_) == wxNOT_FOUND) {
				AddPage(swiftPage_, L"Swift");
			}
		}
		else {
			int const swiftPageIndex = FindPage(swiftPage_);
			if (swiftPageIndex != wxNOT_FOUND) {
				RemovePage(swiftPageIndex);
			}
		}
	}

	if (dropboxPage_) {
		if (protocol == DROPBOX) {
			if (FindPage(dropboxPage_) == wxNOT_FOUND) {
				AddPage(dropboxPage_, L"Dropbox");
			}
		}
		else {
			int const dropboxPageIndex = FindPage(dropboxPage_);
			if (dropboxPageIndex != wxNOT_FOUND) {
				RemovePage(dropboxPageIndex);
			}
		}
	}

	if (generalPage_) {
		generalPage_->GetSizer()->Fit(generalPage_);
	}
}

bool CSiteManagerSite::UpdateSite(Site &site, bool silent)
{
	for (auto & controls : controls_) {
		if (!controls->UpdateSite(site, silent)) {
			return false;
		}
	}

	site.comments_ = comments_ ? comments_->GetValue().ToStdWstring() : std::wstring();
	site.m_colour = color_ ? CSiteManager::GetColourFromIndex(color_->GetSelection()) : site_colour::none;

	return true;
}

void CSiteManagerSite::SetSite(Site const& site, bool predefined)
{
	predefined_ = predefined;

	if (site) {
		SetControlVisibility(site.server.GetProtocol(), site.credentials.logonType_);
	}
	else {
		bool const kiosk_mode = options_.get_int(OPTION_DEFAULT_KIOSKMODE) != 0;
		auto const logonType = kiosk_mode ? LogonType::ask : LogonType::normal;
		SetControlVisibility(FTP, logonType);
	}

	if (color_) {
		color_->Enable(!predefined);
		color_->Select(site ? CSiteManager::GetColourIndex(site.m_colour) : 0);
	}
	if (comments_) {
		comments_->Enable(!predefined);
		comments_->ChangeValue(site ? site.comments_ : std::wstring());
	}

	for (auto & controls : controls_) {
		controls->SetSite(site, predefined_);
		controls->SetControlState(predefined_);
	}
}
