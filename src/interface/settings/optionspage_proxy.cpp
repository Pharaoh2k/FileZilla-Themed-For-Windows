#include "../filezilla.h"

#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_proxy.h"

#include "../textctrlex.h"

#include <wx/statbox.h>

struct COptionsPageProxy::impl final
{
	wxRadioButton* none_{};
	wxRadioButton* http_{};
	wxRadioButton* socks4_{};
	wxRadioButton* socks5_{};

	wxTextCtrlEx* host_{};
	wxTextCtrlEx* port_{};
	wxTextCtrlEx* user_{};
	wxTextCtrlEx* pass_{};
};

COptionsPageProxy::COptionsPageProxy()
	: impl_(std::make_unique<impl>())
{
}

COptionsPageProxy::~COptionsPageProxy() = default;

bool COptionsPageProxy::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);

	SetSizer(main);

	{
		auto [box, inner] = lay.createStatBox(main, _("Generic proxy"), 1);
		inner->AddGrowableCol(0);

		inner->Add(new wxStaticText(box, nullID, _("Type of generic proxy:")));

		impl_->none_ = new wxRadioButton(box, nullID, _("&None"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		inner->Add(impl_->none_);
		impl_->http_ = new wxRadioButton(box, nullID, _("&HTTP/1.1 using CONNECT method"));
		inner->Add(impl_->http_);
		impl_->socks4_ = new wxRadioButton(box, nullID, L"SOC&KS 4");
		inner->Add(impl_->socks4_);
		impl_->socks5_ = new wxRadioButton(box, nullID, L"&SOCKS 5");
		inner->Add(impl_->socks5_);

		auto rows = lay.createFlex(2);
		rows->AddGrowableCol(1);
		inner->Add(rows, lay.grow);

		rows->Add(new wxStaticText(box, nullID, _("P&roxy host:")), lay.valign);
		impl_->host_ = new wxTextCtrlEx(box, nullID);
		rows->Add(impl_->host_, lay.grow);
		rows->Add(new wxStaticText(box, nullID, _("Proxy &port:")), lay.valign);
		impl_->port_ = new wxTextCtrlEx(box, nullID, wxString(), wxDefaultPosition, lay.defTextCtrlSize);
		impl_->port_->SetMaxLength(5);
		rows->Add(impl_->port_);
		rows->Add(new wxStaticText(box, nullID, _("Proxy &user:")), lay.valign);
		impl_->user_ = new wxTextCtrlEx(box, nullID);
		rows->Add(impl_->user_, lay.grow);
		rows->Add(new wxStaticText(box, nullID, _("Pro&xy password:")), lay.valign);
		impl_->pass_ = new wxTextCtrlEx(box, nullID, wxString(), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
		rows->Add(impl_->pass_, lay.grow);

		inner->Add(new wxStaticText(box, nullID, _("Note: Using a generic proxy forces passive mode on FTP connections.")));
	}

	impl_->none_->Bind(wxEVT_RADIOBUTTON, &COptionsPageProxy::OnProxyTypeChanged, this);
	impl_->http_->Bind(wxEVT_RADIOBUTTON, &COptionsPageProxy::OnProxyTypeChanged, this);
	impl_->socks4_->Bind(wxEVT_RADIOBUTTON, &COptionsPageProxy::OnProxyTypeChanged, this);
	impl_->socks5_->Bind(wxEVT_RADIOBUTTON, &COptionsPageProxy::OnProxyTypeChanged, this);

	return true;
}

bool COptionsPageProxy::LoadPage()
{
	impl_->host_->ChangeValue(m_pOptions->get_string(OPTION_PROXY_HOST));
	impl_->port_->ChangeValue(m_pOptions->get_string(OPTION_PROXY_PORT));
	impl_->user_->ChangeValue(m_pOptions->get_string(OPTION_PROXY_USER));
	impl_->pass_->ChangeValue(m_pOptions->get_string(OPTION_PROXY_PASS));

	int type = m_pOptions->get_int(OPTION_PROXY_TYPE);
	switch (type)
	{
	default:
	case 0:
		impl_->none_->SetValue(true);
		break;
	case 1:
		impl_->http_->SetValue(true);
		break;
	case 2:
		impl_->socks5_->SetValue(true);
		break;
	case 3:
		impl_->socks4_->SetValue(true);
		break;
	}

	SetCtrlState();
	return true;
}

bool COptionsPageProxy::SavePage()
{
	m_pOptions->set(OPTION_PROXY_HOST, impl_->host_->GetValue().ToStdWstring());
	m_pOptions->set(OPTION_PROXY_PORT, impl_->port_->GetValue().ToStdWstring());
	m_pOptions->set(OPTION_PROXY_USER, impl_->user_->GetValue().ToStdWstring());
	m_pOptions->set(OPTION_PROXY_PASS, impl_->pass_->GetValue().ToStdWstring());

	int type;
	if (impl_->http_->GetValue()) {
		type = 1;
	}
	else if (impl_->socks5_->GetValue()) {
		type = 2;
	}
	else if (impl_->socks4_->GetValue()) {
		type = 3;
	}
	else {
		type = 0;
	}
	m_pOptions->set(OPTION_PROXY_TYPE, type);

	return true;
}

bool COptionsPageProxy::Validate()
{
	if (impl_->none_->GetValue()) {
		return true;
	}

	std::wstring host = impl_->host_->GetValue().ToStdWstring();
	fz::trim(host);
	if (host.empty()) {
		return DisplayError(impl_->host_, _("You need to enter a proxy host."));
	}
	else {
		impl_->host_->ChangeValue(host);
	}

	int port = fz::to_integral<int>(impl_->port_->GetValue().ToStdWstring());
	if (port < 1 || port > 65535) {
		return DisplayError(impl_->port_, _("You need to enter a proxy port in the range from 1 to 65535"));
	}

	return true;
}

void COptionsPageProxy::SetCtrlState()
{
	bool const enabled = !impl_->none_->GetValue();
	bool enabled_auth = !impl_->socks4_->GetValue();

	impl_->host_->Enable(enabled);
	impl_->port_->Enable(enabled);
	impl_->user_->Enable(enabled && enabled_auth);
	impl_->pass_->Enable(enabled && enabled_auth);
}

void COptionsPageProxy::OnProxyTypeChanged(wxCommandEvent&)
{
	SetCtrlState();
}
