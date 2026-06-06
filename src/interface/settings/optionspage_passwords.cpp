#include "../filezilla.h"
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage_passwords.h"
#include "../loginmanager.h"
#include "../recentserverlist.h"
#include "../state.h"
#include "../textctrlex.h"

#include "../../commonui/protect.h"

#include <libfilezilla/util.hpp>

#include <wx/statbox.h>

struct COptionsPagePasswords::impl final
{
	impl(login_manager & lim)
		: login_manager_(lim)
	{}

	wxRadioButton* save_{};
	wxRadioButton* nosave_{};
	wxRadioButton* usemaster_{};

	wxTextCtrlEx* masterpw_{};
	wxTextCtrlEx* masterrepeat_{};

	login_manager& login_manager_;
};

COptionsPagePasswords::COptionsPagePasswords(login_manager & lim)
	: impl_(std::make_unique<impl>(lim))
{
}

COptionsPagePasswords::~COptionsPagePasswords() = default;


bool COptionsPagePasswords::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	SetSizer(main);

	auto [box, inner] = lay.createStatBox(main, _("Passwords"), 1);

	impl_->save_ = new wxRadioButton(box, nullID, _("Sav&e passwords"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	inner->Add(impl_->save_);
	impl_->nosave_ = new wxRadioButton(box, nullID, _("D&o not save passwords"));
	inner->Add(impl_->nosave_);
	impl_->usemaster_ = new wxRadioButton(box, nullID, _("Sa&ve passwords protected by a master password"));
	inner->Add(impl_->usemaster_);

	auto changeSizer = lay.createFlex(2);
	changeSizer->AddGrowableCol(1);
	changeSizer->Add(new wxStaticText(box, nullID, _("Master password:")), lay.valign);
	impl_->masterpw_ = new wxTextCtrlEx(box, nullID, wxString(), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	changeSizer->Add(impl_->masterpw_, lay.valigng);
	changeSizer->Add(new wxStaticText(box, nullID, _("Repeat password:")), lay.valign);
	impl_->masterrepeat_ = new wxTextCtrlEx(box, nullID, wxString(), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	changeSizer->Add(impl_->masterrepeat_, lay.valigng);

	inner->Add(changeSizer, 0, wxGROW | wxLEFT, lay.indent);
	inner->Add(new wxStaticText(box, nullID, _("A lost master password cannot be recovered! Please thoroughly memorize your password.")), 0, wxLEFT, lay.indent);

	return true;
}

bool COptionsPagePasswords::LoadPage()
{
	bool failure = false;

	auto onChange = [this](wxEvent const&) {
		bool checked = impl_->usemaster_->GetValue();
		impl_->masterpw_->Enable(checked);
		impl_->masterrepeat_->Enable(checked);
	};
	impl_->save_->Bind(wxEVT_RADIOBUTTON, onChange);
	impl_->nosave_->Bind(wxEVT_RADIOBUTTON, onChange);
	impl_->usemaster_->Bind(wxEVT_RADIOBUTTON, onChange);

	bool const disabledByDefault = m_pOptions->get_int(OPTION_DEFAULT_KIOSKMODE) != 0 && m_pOptions->predefined(OPTION_DEFAULT_KIOSKMODE);
	if (disabledByDefault || m_pOptions->get_int(OPTION_DEFAULT_KIOSKMODE) == 2) {
		impl_->nosave_->SetValue(true);
		impl_->save_->Disable();
		impl_->nosave_->Disable();
		impl_->usemaster_->Disable();
	}
	else {
		if (m_pOptions->get_int(OPTION_DEFAULT_KIOSKMODE) != 0) {
			impl_->nosave_->SetValue(true);
		}
		else {
			auto key = fz::public_key::from_base64(fz::to_utf8(m_pOptions->get_string(OPTION_MASTERPASSWORDENCRYPTOR)));
			if (key) {
				impl_->usemaster_->SetValue(true);

				// @translator: Keep this string as short as possible
				impl_->masterpw_->SetHint(_("Leave empty to keep existing password."));
			}
			else {
				impl_->save_->SetValue(true);
			}
		}
	}
	onChange(wxCommandEvent());

	return !failure;
}

bool COptionsPagePasswords::SavePage()
{
	int const old_kiosk_mode = m_pOptions->get_int(OPTION_DEFAULT_KIOSKMODE);
	auto const oldPub = fz::public_key::from_base64(fz::to_utf8(m_pOptions->get_string(OPTION_MASTERPASSWORDENCRYPTOR)));

	bool const disabledByDefault = old_kiosk_mode != 0 && m_pOptions->predefined(OPTION_DEFAULT_KIOSKMODE);
	if (disabledByDefault || m_pOptions->get_int(OPTION_DEFAULT_KIOSKMODE) == 2) {
		return true;
	}

	std::wstring const newPw = impl_->masterpw_->GetValue().ToStdWstring();

	bool const save = impl_->save_->GetValue();
	bool const useMaster = impl_->usemaster_->GetValue();
	bool const forget = !save && !useMaster;

	if (save && !old_kiosk_mode && !oldPub) {
		// Not changing mode
		return true;
	}
	else if (forget && old_kiosk_mode) {
		// Not changing mode
		return true;
	}
	else if (useMaster && newPw.empty()) {
		// Keeping existing master password
		return true;
	}

	// Something is being changed

	CLoginManager loginManager; // Intentially use a separate instance
	if (oldPub && !forget) {
		if (!loginManager.AskDecryptor(oldPub, true, true)) {
			return true;
		}
	}

	if (useMaster) {
		auto priv = fz::private_key::from_password(fz::to_utf8(newPw), fz::random_bytes(fz::private_key::salt_size));
		auto pub = priv.pubkey();
		if (!pub) {
			wxMessageBoxEx(_("Could not generate key"), _("Error"));
		}
		else {
			m_pOptions->set(OPTION_DEFAULT_KIOSKMODE, 0);
			m_pOptions->set(OPTION_MASTERPASSWORDENCRYPTOR, fz::to_wstring_from_utf8(pub.to_base64()));
		}
	}
	else {
		m_pOptions->set(OPTION_DEFAULT_KIOSKMODE, save ? 0 : 1);
		m_pOptions->set(OPTION_MASTERPASSWORDENCRYPTOR, std::wstring());
	}

	// Now actually change stored passwords
	{
		auto recentServers = CRecentServerList::GetMostRecentServers();
		for (auto& site : recentServers) {
			if (!forget) {
				loginManager.AskDecryptor(site.credentials.encrypted_, true, false);
				unprotect(site.credentials, loginManager.GetDecryptor(site.credentials.encrypted_), true);
			}
			protect(site.credentials, impl_->login_manager_, *m_pOptions);
		}
		CRecentServerList::SetMostRecentServers(recentServers, impl_->login_manager_, *m_pOptions);
	}

	for (auto state : *CContextManager::Get()->GetAllStates()) {
		auto site = state->GetLastSite();
		auto path = state->GetLastServerPath();
		if (!forget) {
			loginManager.AskDecryptor(site.credentials.encrypted_, true, false);
			unprotect(site.credentials, loginManager.GetDecryptor(site.credentials.encrypted_), true);
		}
		protect(site.credentials, impl_->login_manager_, *m_pOptions);
		state->SetLastSite(site, path);
	}

	m_pOptions->Cleanup();

	CSiteManager::Rewrite(loginManager, *m_pOptions, true);

	CContextManager::Get()->NotifyGlobalHandlers(STATECHANGE_REWRITE_CREDENTIALS, std::wstring(), &loginManager);

	return true;
}

bool COptionsPagePasswords::Validate()
{
	if (impl_->usemaster_->GetValue()) {
		wxString const pw = impl_->masterpw_->GetValue();
		wxString const repeat = impl_->masterrepeat_->GetValue();
		if (pw != repeat) {
			return DisplayError(impl_->masterpw_, _("The entered passwords are not the same."));
		}

		auto key = fz::public_key::from_base64(fz::to_utf8(m_pOptions->get_string(OPTION_MASTERPASSWORDENCRYPTOR)));
		if (!key && pw.empty()) {
			return DisplayError(impl_->masterpw_, _("You need to enter a master password."));
		}

		if (!pw.empty() && pw.size() < 8) {
			return DisplayError(impl_->masterpw_, _("The master password needs to be at least 8 characters long."));
		}
	}
	return true;
}
