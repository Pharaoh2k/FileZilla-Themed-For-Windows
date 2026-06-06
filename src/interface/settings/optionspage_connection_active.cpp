#include "../filezilla.h"
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_connection_active.h"
#include "../textctrlex.h"

#include <wx/statbox.h>

#include <libfilezilla/iputils.hpp>

struct COptionsPageConnectionActive::impl
{
	wxCheckBox* limit_{};

	wxTextCtrlEx* low_{};
	wxTextCtrlEx* high_{};

	wxRadioButton* rb_system_{};
	wxRadioButton* rb_fixed_{};
	wxRadioButton* rb_resolver_{};

	wxTextCtrlEx* ip_{};
	wxTextCtrlEx* resolver_{};

	wxCheckBox* no_external_on_local_{};
};

COptionsPageConnectionActive::COptionsPageConnectionActive()
: impl_(std::make_unique<impl>())
{
}

COptionsPageConnectionActive::~COptionsPageConnectionActive()
{
}

bool COptionsPageConnectionActive::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	SetSizer(main);

	auto onChange = [this](wxCommandEvent&){ SetCtrlState(); };

	{
		auto [box, inner] = lay.createStatBox(main, _("Limit local ports"), 1);

		impl_->limit_ = new wxCheckBox(box, nullID, _("&Limit local ports used by FileZilla"));
		impl_->limit_->Bind(wxEVT_CHECKBOX, onChange);
		inner->Add(impl_->limit_);

		inner->Add(new wxStaticText(box, nullID, _("By default FileZilla uses any available local port to establish transfers in active mode. If you want to limit FileZilla to use only a small range of ports, please enter the port range below.")), 0, wxLEFT, lay.indent);

		auto flex = lay.createFlex(2);
		inner->Add(flex, 0, wxLEFT, lay.indent);

		flex->Add(new wxStaticText(box, nullID, _("Lo&west available port:")), lay.valign);
		impl_->low_ = new wxTextCtrlEx(box, nullID);
		impl_->low_->SetMaxLength(5);
		flex->Add(impl_->low_, lay.valign)->SetMinSize(lay.dlgUnits(30), -1);
		flex->Add(new wxStaticText(box, nullID, _("&Highest available port:")), lay.valign);
		impl_->high_ = new wxTextCtrlEx(box, nullID);
		impl_->high_->SetMaxLength(5);
		flex->Add(impl_->high_, lay.valign)->SetMinSize(lay.dlgUnits(30), -1);
	}

	{
		auto [box, inner] = lay.createStatBox(main, _("Active mode IP"), 1);

		inner->Add(new wxStaticText(box, nullID, _("In order to use active mode, FileZilla needs to know your external IP address.")));

		impl_->rb_system_ = new wxRadioButton(box, nullID, _("&Ask your operating system for the external IP address"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		inner->Add(impl_->rb_system_);
		impl_->rb_fixed_ = new wxRadioButton(box, nullID, _("&Use the following IP address:"));
		inner->Add(impl_->rb_fixed_);
		impl_->ip_ = new wxTextCtrlEx(box, nullID, wxString(), wxDefaultPosition, lay.defTextCtrlSize);
		inner->Add(impl_->ip_, 0, wxLEFT, lay.indent);
		inner->Add(new wxStaticText(box, nullID, _("Use this if you're behind a router and have a static external IP address.")), 0, wxLEFT, lay.indent);
		impl_->rb_resolver_ = new wxRadioButton(box, nullID, _("&Get external IP address from the following URL:"));
		inner->Add(impl_->rb_resolver_);
		impl_->resolver_ = new wxTextCtrlEx(box, nullID, wxString(), wxDefaultPosition, lay.defTextCtrlSize);
		inner->Add(impl_->resolver_, 0, wxLEFT, lay.indent);
		inner->Add(new wxStaticText(box, nullID, _("Default: http://ip.filezilla-project.org/ip.php")), 0, wxLEFT, lay.indent);
		impl_->no_external_on_local_ = new wxCheckBox(box, nullID, _("&Don't use external IP address on local connections."));
		inner->Add(impl_->no_external_on_local_);

		impl_->rb_system_->Bind(wxEVT_RADIOBUTTON, onChange);
		impl_->rb_fixed_->Bind(wxEVT_RADIOBUTTON, onChange);
		impl_->rb_resolver_->Bind(wxEVT_RADIOBUTTON, onChange);

	}

	return true;
}

bool COptionsPageConnectionActive::LoadPage()
{
	impl_->limit_->SetValue(m_pOptions->get_bool(OPTION_LIMITPORTS));
	impl_->low_->ChangeValue(m_pOptions->get_string(OPTION_LIMITPORTS_LOW));
	impl_->high_->ChangeValue(m_pOptions->get_string(OPTION_LIMITPORTS_HIGH));

	switch (m_pOptions->get_int(OPTION_EXTERNALIPMODE)) {
	default:
		impl_->rb_system_->SetValue(true);
		break;
	case 1:
		impl_->rb_fixed_->SetValue(true);
		break;
	case 2:
		impl_->rb_resolver_->SetValue(true);
		break;
	}

	impl_->ip_->ChangeValue(m_pOptions->get_string(OPTION_EXTERNALIP));
	impl_->resolver_->ChangeValue(m_pOptions->get_string(OPTION_EXTERNALIPRESOLVER));
	impl_->no_external_on_local_->SetValue(m_pOptions->get_bool(OPTION_NOEXTERNALONLOCAL));

	SetCtrlState();
	
	return true;
}

bool COptionsPageConnectionActive::SavePage()
{
	m_pOptions->set(OPTION_LIMITPORTS, impl_->limit_->GetValue());

	m_pOptions->set(OPTION_LIMITPORTS_LOW, impl_->low_->GetValue().ToStdWstring());
	m_pOptions->set(OPTION_LIMITPORTS_HIGH, impl_->high_->GetValue().ToStdWstring());

	int mode{};
	if (impl_->rb_fixed_->GetValue()) {
		mode = 1;
	}
	else if (impl_->rb_resolver_->GetValue()) {
		mode = 2;
	}
	m_pOptions->set(OPTION_EXTERNALIPMODE, mode);

	if (mode == 1) {
		m_pOptions->set(OPTION_EXTERNALIP, impl_->ip_->GetValue().ToStdWstring());
	}
	else if (mode == 2) {
		m_pOptions->set(OPTION_EXTERNALIPRESOLVER, impl_->resolver_->GetValue().ToStdWstring());
	}
	m_pOptions->set(OPTION_NOEXTERNALONLOCAL, impl_->no_external_on_local_->GetValue());
	return true;
}

bool COptionsPageConnectionActive::Validate()
{
	// Validate port limiting settings
	if (impl_->limit_->IsChecked()) {
		int low = fz::to_integral<int>(impl_->low_->GetValue().ToStdWstring());
		if (low < 1024 || low > 65535) {
			return DisplayError(impl_->low_, _("Lowest available port has to be a number between 1024 and 65535."));
		}

		long high = fz::to_integral<int>(impl_->high_->GetValue().ToStdWstring());
		if (high < 1024 || high > 65535) {
			return DisplayError(impl_->high_, _("Highest available port has to be a number between 1024 and 65535."));
		}

		if (low > high) {
			return DisplayError(impl_->low_, _("The lowest available port has to be less or equal than the highest available port."));
		}
	}

	if (impl_->rb_fixed_->GetValue()) {
		std::wstring ip = impl_->ip_->GetValue().ToStdWstring();
		if (fz::get_address_type(ip) != fz::address_type::ipv4) {
			return DisplayError(impl_->ip_, _("You have to enter a valid IPv4 address."));
		}
	}
	else if (impl_->rb_resolver_->GetValue()) {
		if (impl_->resolver_->GetValue().empty()) {
			return DisplayError(impl_->resolver_, _("You have to enter a resolver."));
		}
	}

	return true;
}

void COptionsPageConnectionActive::SetCtrlState()
{
	impl_->low_->Enable(impl_->limit_->GetValue());
	impl_->high_->Enable(impl_->limit_->GetValue());

	impl_->ip_->Enable(impl_->rb_fixed_->GetValue());
	impl_->resolver_->Enable(impl_->rb_resolver_->GetValue());

	impl_->no_external_on_local_->Enable(!impl_->rb_system_->GetValue());
}
