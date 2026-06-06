#include "filezilla.h"
#include "quickconnectbar.h"
#include "recentserverlist.h"
#include "commandqueue.h"
#include "state.h"
#include "Options.h"
#include "loginmanager.h"
#include "Mainfrm.h"
#include "asksavepassworddialog.h"
#include "filezillaapp.h"
#include "textctrlex.h"
#include "themeprovider.h"

#include <wx/bmpbuttn.h>
#include <wx/menu.h>
#include <wx/statline.h>

BEGIN_EVENT_TABLE(CQuickconnectBar, wxPanel)
EVT_BUTTON(XRCID("ID_QUICKCONNECT_OK"), CQuickconnectBar::OnQuickconnect)
EVT_BUTTON(XRCID("ID_QUICKCONNECT_DROPDOWN"), CQuickconnectBar::OnQuickconnectDropdown)
EVT_MENU(wxID_ANY, CQuickconnectBar::OnMenu)
EVT_TEXT_ENTER(wxID_ANY, CQuickconnectBar::OnQuickconnect)
END_EVENT_TABLE()

CQuickconnectBar::CQuickconnectBar(CMainFrame & parent)
	: wxPanel(&parent, -1)
	, options_(parent.GetOptions())
	, mainFrame_(parent)
{
	auto sizer = new wxBoxSizer(wxVERTICAL);
	SetSizer(sizer);
#ifndef __WXMAC__
	sizer->Add(new wxStaticLine(this, -1, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL), wxSizerFlags().Expand());
#endif

	DialogLayout layout(&parent);
	auto mainSizer = layout.createFlex(0, 1);
	sizer->Add(mainSizer, wxSizerFlags().Border(wxALL, ConvertDialogToPixels(wxPoint(2, 0)).x));

	mainSizer->Add(new wxStaticText(this, nullID, _("&Host:")), wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL));
	m_pHost = new wxTextCtrlEx(this, -1, wxString(), wxDefaultPosition, ConvertDialogToPixels(wxSize(63, -1)), wxTE_PROCESS_ENTER);
	m_pHost->SetToolTip(_("Enter the address of the server. To specify the server protocol, prepend the host with the protocol identifier. If no protocol is specified, the default protocol (ftp://) will be used. You can also enter complete URLs in the form protocol://user:pass@host:port here, the values in the other fields will be overwritten then.\n\nSupported protocols are:\n- ftp:// for normal FTP with optional encryption\n- sftp:// for SSH file transfer protocol\n- ftps:// for FTP over TLS (implicit)\n- ftpes:// for FTP over TLS (explicit)"));
	m_pHost->SetMaxLength(1000);
	mainSizer->Add(m_pHost, wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));

	mainSizer->Add(new wxStaticText(this, nullID, _("&Username:")), wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL));
	m_pUser = new wxTextCtrlEx(this, -1, wxString(), wxDefaultPosition, layout.defTextCtrlSize, wxTE_PROCESS_ENTER);
	m_pUser->SetMaxLength(1000);
	mainSizer->Add(m_pUser, wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));

	mainSizer->Add(new wxStaticText(this, nullID, _("Pass&word:")), wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL));
	m_pPass = new wxTextCtrlEx(this, nullID, wxString(), wxDefaultPosition, layout.defTextCtrlSize, wxTE_PROCESS_ENTER|wxTE_PASSWORD);
	m_pPass->SetMaxLength(1000);
	mainSizer->Add(m_pPass, wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));

	mainSizer->Add(new wxStaticText(this, nullID, _("&Port:")), wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL));
	m_pPort = new wxTextCtrlEx(this, nullID, wxString(), wxDefaultPosition, ConvertDialogToPixels(wxSize(27, -1)), wxTE_PROCESS_ENTER);
	m_pPort->SetToolTip(_("Enter the port on which the server listens. The default for FTP is 21, the default for SFTP is 22."));
	m_pPort->SetMaxLength(5);
	mainSizer->Add(m_pPort, wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));

	auto connectSizer = new wxBoxSizer(wxHORIZONTAL);
	mainSizer->Add(connectSizer, wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL));
	auto connect = new wxButton(this, XRCID("ID_QUICKCONNECT_OK"), _("&Quickconnect"));
	connect->SetDefault();
	connectSizer->Add(connect, wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL));

	wxBitmap bmp = CThemeProvider::Get()->CreateBitmap(L"ART_DROPDOWN", wxART_OTHER, CThemeProvider::GetIconSize(iconSizeTiny));
	auto flags = wxSizerFlags().Expand();
#if defined(__WXMSW__)
	wxSize dropdownSize = ConvertDialogToPixels(wxSize(12, -1));
#elif defined(__WXMAC__)
	wxSize dropdownSize(20, -1);
	flags.Border(wxLEFT, 6);
#else
	wxSize dropdownSize(-1, -1);
#endif
	connectSizer->Add(new wxBitmapButton(this, XRCID("ID_QUICKCONNECT_DROPDOWN"), MakeBmpBundle(bmp), wxDefaultPosition, dropdownSize, wxBU_AUTODRAW | wxBU_EXACTFIT), flags);

#ifdef __WXMAC__
	// Under OS X default buttons are toplevel window wide, where under Windows / GTK they stop at the parent panel.
	wxTopLevelWindow *tlw = dynamic_cast<wxTopLevelWindow*>(wxGetTopLevelParent(&parent));
	if (tlw) {
		tlw->SetDefaultItem(0);
	}
#endif

	Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
		if (event.GetKeyCode() == WXK_DOWN) {
			wxCommandEvent evt(wxEVT_CHAR_HOOK);
			OnQuickconnectDropdown(evt);
		}
		else {
			event.Skip();
		}
	});

#ifdef __WXMSW__
	Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& evt) {
		// Delay refresh until children had time to process change.
		CallAfter([this](){Refresh();});
		evt.Skip();
	});
#endif

	GetSizer()->Fit(this);
}

void CQuickconnectBar::OnQuickconnect(wxCommandEvent& event)
{
	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState || !pState->engine_) {
		return;
	}

	std::wstring host = m_pHost->GetValue().ToStdWstring();
	std::wstring user = m_pUser->GetValue().ToStdWstring();
	std::wstring pass = m_pPass->GetValue().ToStdWstring();
	std::wstring port = m_pPort->GetValue().ToStdWstring();

	Site site;

	std::wstring error;

	CServerPath path;
	if (!site.ParseUrl(host, port, user, pass, error, path)) {
		wxString msg = _("Could not parse server address:");
		msg += _T("\n");
		msg += error;
		wxMessageBoxEx(msg, _("Syntax error"), wxICON_EXCLAMATION);
		return;
	}

	host = site.Format(ServerFormat::host_only);
	ServerProtocol protocol = site.server.GetProtocol();
	switch (protocol)
	{
	case FTP:
	case UNKNOWN:
		if (CServer::GetProtocolFromPort(site.server.GetPort()) != FTP &&
			CServer::GetProtocolFromPort(site.server.GetPort()) != UNKNOWN)
		{
			host = _T("ftp://") + host;
		}
		break;
	default:
		{
			std::wstring const prefix = site.server.GetPrefixFromProtocol(protocol);
			if (!prefix.empty()) {
				host = prefix + _T("://") + host;
			}
		}
		break;
	}

	m_pHost->SetValue(host);
	if (site.server.GetPort() != site.server.GetDefaultPort(site.server.GetProtocol())) {
		m_pPort->SetValue(wxString::Format(_T("%d"), site.server.GetPort()));
	}
	else {
		m_pPort->ChangeValue(wxString());
	}

	m_pUser->SetValue(site.server.GetUser());
	if (site.credentials.logonType_ != LogonType::anonymous) {
		m_pPass->SetValue(site.credentials.GetPass());
	}
	else {
		m_pPass->ChangeValue(wxString());
	}

	if (protocol == HTTP || protocol == HTTPS || protocol == S3) {
		wxString protocolError = _("Invalid protocol specified. Valid protocols are:\nftp:// for normal FTP with optional encryption,\nsftp:// for SSH file transfer protocol,\nftps:// for FTP over TLS (implicit) and\nftpes:// for FTP over TLS (explicit).");
		wxMessageBoxEx(protocolError, _("Syntax error"), wxICON_EXCLAMATION);
		return;
	}

	if (event.GetId() == 1) {
		site.server.SetBypassProxy(true);
	}

	if (site.credentials.logonType_ != LogonType::anonymous && !CAskSavePasswordDialog::Run(this, options_)) {
		return;
	}

	if (options_.get_int(OPTION_DEFAULT_KIOSKMODE) && site.credentials.logonType_ == LogonType::normal) {
		site.SetLogonType(LogonType::ask);
		mainFrame_.GetLoginManager().RememberPassword(site);
	}
	Bookmark bm;
	bm.m_remoteDir = path;
	if (!mainFrame_.ConnectToSite(site, bm)) {
		return;
	}

	CRecentServerList::SetMostRecentServer(site, mainFrame_.GetLoginManager(), options_);
}

void CQuickconnectBar::OnQuickconnectDropdown(wxCommandEvent& event)
{
	wxMenu* pMenu = new wxMenu;

	// We have to start with id 1 since menu items with id 0 don't work under OS X
	if (options_.get_int(OPTION_FTP_PROXY_TYPE)) {
		pMenu->Append(1, _("Connect bypassing proxy settings"));
	}
	pMenu->Append(2, _("Clear quickconnect bar"));
	pMenu->Append(3, _("Clear history"));

	m_recentServers = CRecentServerList::GetMostRecentServers();
	if (!m_recentServers.empty()) {
		pMenu->AppendSeparator();

		unsigned int i = 0;
		for (auto iter = m_recentServers.cbegin();
			iter != m_recentServers.end();
			++iter, ++i)
		{
			std::wstring name = LabelEscape(iter->Format(ServerFormat::with_user_and_optional_port)).substr(0, 255);
			pMenu->Append(10 + i, name);
		}
	}
	else {
		pMenu->Enable(3, false);
	}

	auto * btn = XRCCTRL(*this, "ID_QUICKCONNECT_DROPDOWN", wxButton);
	auto size = btn->GetSize() / 2;
	btn->PopupMenu(pMenu, (event.GetEventType() == wxEVT_CHAR_HOOK) ? wxPoint(size.x, size.y) : wxDefaultPosition);
	delete pMenu;
	m_recentServers.clear();
}

void CQuickconnectBar::OnMenu(wxCommandEvent& event)
{
	const int id = event.GetId();
	if (id == 1) {
		OnQuickconnect(event);
	}
	else if (id == 2) {
		ClearFields();
	}
	else if (id == 3) {
		CRecentServerList::Clear();
	}

	if (id < 10) {
		return;
	}

	unsigned int index = id - 10;
	if (index >= m_recentServers.size()) {
		return;
	}

	auto iter = m_recentServers.cbegin();
	std::advance(iter, index);

	Site site = *iter;
	mainFrame_.ConnectToSite(site, Bookmark());
}

void CQuickconnectBar::ClearFields()
{
	m_pHost->ChangeValue(_T(""));
	m_pPort->SetValue(_T(""));
	m_pUser->SetValue(_T(""));
	m_pPass->SetValue(_T(""));
}
