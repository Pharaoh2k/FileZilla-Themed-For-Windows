#include "../filezilla.h"

#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_ftpproxy.h"
#include "../textctrlex.h"

#include <wx/statbox.h>

struct COptionsPageFtpProxy::impl final
{
	wxRadioButton* none_{};
	wxRadioButton* userhost_{};
	wxRadioButton* site_{};
	wxRadioButton* open_{};
	wxRadioButton* custom_{};

	wxTextCtrlEx* sequence_{};

	wxTextCtrlEx* host_{};
	wxTextCtrlEx* user_{};
	wxTextCtrlEx* pass_{};
};

COptionsPageFtpProxy::COptionsPageFtpProxy()
	: impl_(std::make_unique<impl>())
{
}

COptionsPageFtpProxy::~COptionsPageFtpProxy() = default;

bool COptionsPageFtpProxy::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	main->AddGrowableRow(0);
	SetSizer(main);

	{
		auto [box, inner] = lay.createStatBox(main, _("FTP Proxy"), 1);
		inner->AddGrowableCol(0);
		inner->AddGrowableRow(6);

		inner->Add(new wxStaticText(box, nullID, _("Type of FTP Proxy:")));
		impl_->none_ = new wxRadioButton(box, nullID, _("&None"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		inner->Add(impl_->none_);
		impl_->userhost_ = new wxRadioButton(box, nullID, L"USER@&HOST");
		inner->Add(impl_->userhost_);
		impl_->site_ = new wxRadioButton(box, nullID, L"&SITE");
		inner->Add(impl_->site_);
		impl_->open_ = new wxRadioButton(box, nullID, L"&OPEN");
		inner->Add(impl_->open_);
		impl_->custom_ = new wxRadioButton(box, nullID, _("Cus&tom"));
		inner->Add(impl_->custom_);

		auto flex = lay.createFlex(1);
		flex->AddGrowableCol(0);
		flex->AddGrowableRow(0);
		inner->Add(flex, 0, wxGROW|wxLEFT, lay.indent);
		impl_->sequence_ = new wxTextCtrlEx(box, nullID, wxString(), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
		flex->Add(impl_->sequence_, lay.grow);
		flex->Add(new wxStaticText(box, nullID, _("Format specifications:")));
		auto row = lay.createFlex(3);
		flex->Add(row);
		row->SetHGap(lay.gap * 3);
		row->Add(new wxStaticText(box, nullID, _("%h - Host")), lay.valign);
		row->Add(new wxStaticText(box, nullID, _("%u - Username")), lay.valign);
		row->Add(new wxStaticText(box, nullID, _("%p - Password")), lay.valign);
		flex->Add(new wxStaticText(box, nullID, _("%a - Account (Lines containing this will be omitted if not using Account logontype)")));
		row = lay.createFlex(2);
		flex->Add(row);
		row->SetHGap(lay.gap * 3);
		row->Add(new wxStaticText(box, nullID, _("%s - Proxy user")), lay.valign);
		row->Add(new wxStaticText(box, nullID, _("%w - Proxy password")), lay.valign);

		flex = lay.createFlex(4);
		inner->Add(flex);
		flex->Add(new wxStaticText(box, nullID, _("P&roxy host:")), lay.valign);
		impl_->host_ = new wxTextCtrlEx(box, nullID, wxString(), wxDefaultPosition, lay.defTextCtrlSize);
		flex->Add(impl_->host_, lay.valign);
		flex->Add(new wxStaticText(box, nullID, _("Proxy &user:")), lay.valign);
		impl_->user_ = new wxTextCtrlEx(box, nullID, wxString(), wxDefaultPosition, lay.defTextCtrlSize);
		flex->Add(impl_->user_, lay.valign);
		flex->Add(new wxStaticText(box, nullID, _("Pro&xy password:")), lay.valign);
		impl_->pass_ = new wxTextCtrlEx(box, nullID, wxString(), wxDefaultPosition, lay.defTextCtrlSize, wxTE_PASSWORD);
		flex->Add(impl_->pass_, lay.valign);
		inner->Add(new wxStaticText(box, nullID, _("Note: This only works with plain, unencrypted FTP connections.")));

		impl_->none_->Bind(wxEVT_RADIOBUTTON, &COptionsPageFtpProxy::OnProxyTypeChanged, this);
		impl_->userhost_->Bind(wxEVT_RADIOBUTTON, &COptionsPageFtpProxy::OnProxyTypeChanged, this);
		impl_->site_->Bind(wxEVT_RADIOBUTTON, &COptionsPageFtpProxy::OnProxyTypeChanged, this);
		impl_->open_->Bind(wxEVT_RADIOBUTTON, &COptionsPageFtpProxy::OnProxyTypeChanged, this);
		impl_->custom_->Bind(wxEVT_RADIOBUTTON, &COptionsPageFtpProxy::OnProxyTypeChanged, this);
		impl_->sequence_->Bind(wxEVT_TEXT, &COptionsPageFtpProxy::OnLoginSequenceChanged, this);
	}

	return true;
}

bool COptionsPageFtpProxy::LoadPage()
{
	int type = m_pOptions->get_int(OPTION_FTP_PROXY_TYPE);
	switch (type)
	{
	default:
	case 0:
		impl_->none_->SetValue(true);
		break;
	case 1:
		impl_->userhost_->SetValue(true);
		break;
	case 2:
		impl_->site_->SetValue(true);
		break;
	case 3:
		impl_->open_->SetValue(true);
		break;
	case 4:
		impl_->custom_->SetValue(true);
		impl_->sequence_->ChangeValue(m_pOptions->get_string(OPTION_FTP_PROXY_CUSTOMLOGINSEQUENCE));
		break;
	}

	impl_->host_->ChangeValue(m_pOptions->get_string(OPTION_FTP_PROXY_HOST));
	impl_->user_->ChangeValue(m_pOptions->get_string(OPTION_FTP_PROXY_USER));
	impl_->pass_->ChangeValue(m_pOptions->get_string(OPTION_FTP_PROXY_PASS));

	SetCtrlState();

	return true;
}

bool COptionsPageFtpProxy::SavePage()
{
	int type{};
	if (impl_->userhost_->GetValue()) {
		type = 1;
	}
	if (impl_->site_->GetValue()) {
		type = 2;
	}
	if (impl_->open_->GetValue()) {
		type = 3;
	}
	if (impl_->custom_->GetValue()) {
		type = 4;
		m_pOptions->set(OPTION_FTP_PROXY_CUSTOMLOGINSEQUENCE, impl_->sequence_->GetValue().ToStdWstring());
	}
	m_pOptions->set(OPTION_FTP_PROXY_TYPE, type);

	m_pOptions->set(OPTION_FTP_PROXY_HOST, impl_->host_->GetValue().ToStdWstring());
	m_pOptions->set(OPTION_FTP_PROXY_USER, impl_->user_->GetValue().ToStdWstring());
	m_pOptions->set(OPTION_FTP_PROXY_PASS, impl_->pass_->GetValue().ToStdWstring());

	return true;
}

bool COptionsPageFtpProxy::Validate()
{
	if (!impl_->none_->GetValue()) {
		if (impl_->host_->GetValue().empty()) {
			return DisplayError(impl_->host_, _("You need to enter a proxy host."));
		}
	}

	if (impl_->custom_->GetValue()) {
		if (impl_->sequence_->GetValue().empty()) {
			return DisplayError(impl_->sequence_, _("The custom login sequence cannot be empty."));
		}
	}

	return true;
}

void COptionsPageFtpProxy::SetCtrlState()
{
	bool const none = impl_->none_->GetValue();

	impl_->host_->Enable(!none);
	impl_->user_->Enable(!none);
	impl_->pass_->Enable(!none);
	impl_->sequence_->Enable(!none);
	impl_->sequence_->SetEditable(!none);

	if (none) {
		impl_->sequence_->ChangeValue(wxString());
#ifdef __WXMSW__
		impl_->sequence_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
#endif
		return;
	}

#ifdef __WXMSW__
	impl_->sequence_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif

	if (impl_->custom_->GetValue()) {
		return;
	}

	wxString loginSequence = L"USER %s\nPASS %w\n";

	if (impl_->userhost_->GetValue()) {
		loginSequence += L"USER %u@%h\n";
	}
	else {
		if (impl_->site_->GetValue()) {
			loginSequence += L"SITE %h\n";
		}
		else {
			loginSequence += L"OPEN %h\n";
		}
		loginSequence += L"USER %u\n";
	}

	loginSequence += L"PASS %p\nACCT %a";

	impl_->sequence_->ChangeValue(loginSequence);
}

void COptionsPageFtpProxy::OnProxyTypeChanged(wxCommandEvent&)
{
	SetCtrlState();
}

void COptionsPageFtpProxy::OnLoginSequenceChanged(wxCommandEvent&)
{
	impl_->custom_->SetValue(true);
}
