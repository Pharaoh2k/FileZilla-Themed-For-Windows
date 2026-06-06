#include "filezilla.h"
#include "loginmanager.h"
#include "dialogex.h"
#include "filezillaapp.h"
#include "Options.h"
#include "textctrlex.h"

bool CLoginManager::query_unprotect_site(Site & site)
{
	assert(site.credentials.encrypted_);

	wxDialogEx pwdDlg;
	if (!pwdDlg.Create(wxGetApp().GetTopWindow(), nullID, _("Enter master password"))) {
		return false;
	}
	auto & lay = pwdDlg.layout();
	auto * main = lay.createMain(&pwdDlg, 1);

	main->Add(new wxStaticText(&pwdDlg, nullID, _("Please enter your master password to decrypt the password for this server:")));

	auto* inner = lay.createFlex(2);
	main->Add(inner);

	std::wstring const& name = site.GetName();
	if (!name.empty()) {
		inner->Add(new wxStaticText(&pwdDlg, nullID, _("Name:")));
		inner->Add(new wxStaticText(&pwdDlg, nullID, LabelEscape(name)));
	}

	if (site.server.GetProtocol() == STORJ || site.server.GetProtocol() == STORJ_GRANT) {
		inner->Add(new wxStaticText(&pwdDlg, nullID, _("Satellite:")));
	}
	else {
		inner->Add(new wxStaticText(&pwdDlg, nullID, _("Host:")));
	}
	inner->Add(new wxStaticText(&pwdDlg, nullID, LabelEscape(site.Format(ServerFormat::with_optional_port))));

	if (!site.server.GetUser().empty()) {
		if (site.server.GetProtocol() == STORJ) {
			inner->Add(new wxStaticText(&pwdDlg, nullID, _("API Key:")));
		}
		else {
			inner->Add(new wxStaticText(&pwdDlg, nullID, _("User:")));
		}
		inner->Add(new wxStaticText(&pwdDlg, nullID, LabelEscape(site.server.GetUser())));
	}

	inner = lay.createFlex(2);
	main->Add(inner);

	inner->Add(new wxStaticText(&pwdDlg, nullID, _("Key identifier:")));
	inner->Add(new wxStaticText(&pwdDlg, nullID, fz::to_wstring(site.credentials.encrypted_.to_base64().substr(0, 8))));

	inner->Add(new wxStaticText(&pwdDlg, nullID, _("Master &Password:")), lay.valign);

	auto* password = new wxTextCtrlEx(&pwdDlg, nullID, wxString(), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	password->SetMinSize(wxSize(150, -1));
	password->SetFocus();
	inner->Add(password, lay.valign);

	main->AddSpacer(0);
	auto* remember = new wxCheckBox(&pwdDlg, nullID, _("&Remember master password until FileZilla is closed"));
	remember->SetValue(true);
	main->Add(remember);

	auto* buttons = lay.createButtonSizer(&pwdDlg, main, true);

	auto ok = new wxButton(&pwdDlg, wxID_OK, _("&OK"));
	ok->SetDefault();
	buttons->AddButton(ok);

	auto cancel = new wxButton(&pwdDlg, wxID_CANCEL, _("Cancel"));
	buttons->AddButton(cancel);

	buttons->Realize();

	pwdDlg.GetSizer()->Fit(&pwdDlg);
	pwdDlg.GetSizer()->SetSizeHints(&pwdDlg);

	while (true) {
		if (pwdDlg.ShowModal() != wxID_OK) {
			return false;
		}

		auto pass = fz::to_utf8(password->GetValue().ToStdWstring());
		auto key = fz::private_key::from_password(pass, site.credentials.encrypted_.salt_);

		if (key.pubkey() != site.credentials.encrypted_) {
			wxMessageBoxEx(_("Wrong master password entered, it cannot be used to decrypt this item."), _("Invalid input"), wxICON_EXCLAMATION);
			continue;
		}

		if (!unprotect(site.credentials, key)) {
			wxMessageBoxEx(_("Failed to decrypt server password."), _("Invalid input"), wxICON_EXCLAMATION);
			continue;
		}

		if (remember->IsChecked()) {
			Remember(key);
		}
		break;
	}

	return true;
}

namespace {
void AddSite(wxWindow* parent, wxSizer* main, Site const& site, DialogLayout const& lay)
{
	auto heading = new wxStaticText(parent, nullID, _("Server"));
	heading->SetFont(heading->GetFont().Bold());
	main->Add(heading);

	auto inner = lay.createFlex(2);
	main->Add(inner, 0, wxLEFT, lay.indent);

	std::wstring const& name = site.GetName();
	if (!name.empty()) {
		inner->Add(new wxStaticText(parent, nullID, _("Name:")));
		inner->Add(new wxStaticText(parent, nullID, LabelEscape(name)));
	}

	inner->Add(new wxStaticText(parent, nullID, _("Host:")));
	inner->Add(new wxStaticText(parent, nullID, LabelEscape(site.Format(ServerFormat::with_optional_port))));

	if (!site.server.GetUser().empty()) {
		inner->Add(new wxStaticText(parent, nullID, _("User:")));
		inner->Add(new wxStaticText(parent, nullID, LabelEscape(site.server.GetUser())));
	}
	main->AddSpacer(0);
}
}

std::string CLoginManager::get_topt(Site const& site)
{
	wxDialogEx pwdDlg;
	if (!pwdDlg.Create(wxGetApp().GetTopWindow(), nullID, _("Enter the 2FA code"))) {
		return {};
	}
	auto& lay = pwdDlg.layout();
	auto* main = lay.createMain(&pwdDlg, 1);

	main->Add(new wxStaticText(&pwdDlg, nullID, _("Please enter the 2FA code for this server:")));

	AddSite(&pwdDlg, main, site, lay);

	auto inner = lay.createFlex(2);
	main->Add(inner);

	inner->Add(new wxStaticText(&pwdDlg, nullID, _("&Token code:")), lay.valign);
	auto otpCode = new wxTextCtrlEx(&pwdDlg, nullID, wxString());
	otpCode->SetMinSize(wxSize(150, -1));
	inner->Add(otpCode, lay.valign);

	auto* buttons = lay.createButtonSizer(&pwdDlg, main, true);

	auto ok = new wxButton(&pwdDlg, wxID_OK, _("&OK"));
	ok->SetDefault();
	buttons->AddButton(ok);

	auto cancel = new wxButton(&pwdDlg, wxID_CANCEL, _("Cancel"));
	buttons->AddButton(cancel);

	buttons->Realize();

	pwdDlg.GetSizer()->Fit(&pwdDlg);
	pwdDlg.GetSizer()->SetSizeHints(&pwdDlg);

	while (true) {
		if (pwdDlg.ShowModal() != wxID_OK) {
			return {};
		}

		auto otp = fz::to_utf8(otpCode->GetValue());
		if (otp.empty()) {
			wxMessageBoxEx(_("No code given."), _("Invalid input"), wxICON_EXCLAMATION);
			continue;
		}

		return otp;
	}

	return {};
}

std::optional<std::vector<std::string>> CLoginManager::interactive_prompt(Site const& site, std::string const& name, std::string const& instruction, fz::ssh::keyboard_interactive_prompts const& prompts)
{
	wxDialogEx pwdDlg;
	if (!pwdDlg.Create(wxGetApp().GetTopWindow(), nullID, _("Authentication required"))) {
		return std::nullopt;
	}
	auto& lay = pwdDlg.layout();
	auto* main = lay.createMain(&pwdDlg, 1);

	main->Add(new wxStaticText(&pwdDlg, nullID, _("Answer the challenge received by the server to authenticate.")));

	AddSite(&pwdDlg, main, site, lay);

	std::vector<wxTextCtrlEx*> responses;

	{
		auto heading = new wxStaticText(&pwdDlg, nullID, _("Challenge"));
		heading->SetFont(heading->GetFont().Bold());
		main->Add(heading);

		auto inner = lay.createFlex(2);
		main->Add(inner, 0, wxLEFT, lay.indent);

		auto add_prompt = [&](wxString const& label, std::string const& prompt) {
			inner->Add(new wxStaticText(&pwdDlg, nullID, label), lay.valigng);

			std::wstring msg = fz::to_wstring_from_utf8(fz::trimmed(prompt));
	#ifdef FZ_WINDOWS
			fz::replace_substrings(msg, L"\n", L"\r\n");
	#endif
			auto* challengeText = new wxTextCtrlEx(&pwdDlg, nullID, msg, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);

			auto large = msg.find('\n') != std::wstring::npos || challengeText->GetTextExtent(msg).x > lay.dlgUnits(230);

			challengeText->SetMinSize(wxSize(lay.dlgUnits(240), lay.dlgUnits(large ? 28 : 10)));
			challengeText->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
			inner->Add(challengeText, lay.valigng);
		};

		if (!name.empty()) {
			add_prompt(_("Name:"), name);
		}

		if (!instruction.empty()) {
			add_prompt(_("Instructions:"), instruction);
		}

		for (auto const& p : prompts) {
			inner->AddSpacer(0);
			inner->AddSpacer(0);

			add_prompt(_("Prompt:"), p.prompt_);

			inner->Add(new wxStaticText(&pwdDlg, nullID, _("Response:")), lay.valign);
			auto response = new wxTextCtrlEx(&pwdDlg, nullID, wxString(), wxDefaultPosition, wxDefaultSize, p.echo_ ? 0 : wxTE_PASSWORD);
			response->SetMinSize(wxSize(150, -1));
			inner->Add(response, lay.valigng);
			responses.push_back(response);
		}
	}

	main->AddSpacer(0);

	auto inner = lay.createFlex(2);
	main->Add(inner);

	if (!responses.empty()) {
		responses.front()->SetFocus();
	}
	auto* buttons = lay.createButtonSizer(&pwdDlg, main, true);

	auto ok = new wxButton(&pwdDlg, wxID_OK, _("&OK"));
	ok->SetDefault();
	buttons->AddButton(ok);

	auto cancel = new wxButton(&pwdDlg, wxID_CANCEL, _("Cancel"));
	buttons->AddButton(cancel);

	buttons->Realize();

	pwdDlg.GetSizer()->Fit(&pwdDlg);
	pwdDlg.GetSizer()->SetSizeHints(&pwdDlg);

	if (pwdDlg.ShowModal() != wxID_OK) {
		return std::nullopt;
	}

	std::vector<std::string> ret;
	for (auto const& r : responses) {
		ret.emplace_back(fz::to_utf8(r->GetValue()));
	}

	return std::move(ret);
}

bool CLoginManager::query_credentials(Site & site, bool canRemember)
{
	if (site.credentials.encrypted_) {
		return false;
	}

	bool needs_user{};
	bool needs_pass{};

	wxString title;
	wxString header;
	if (site.server.GetUser().empty() && ProtocolHasUser(site.server.GetProtocol())) {
		needs_user = true;
		if (site.credentials.logonType_ == LogonType::interactive) {
			title = _("Enter username");
			header = _("Please enter a username for this server:");
			canRemember = false;
		}
		else {
			title = _("Enter username and password");
			header = _("Please enter username and password for this server:");
			needs_pass = true;
		}
	}
	else {
		needs_pass = true;
		title = _("Enter password");
		header = _("Please enter a password for this server:");
	}

	wxDialogEx pwdDlg;
	if (!pwdDlg.Create(wxGetApp().GetTopWindow(), nullID, title)) {
		return false;
	}
	auto& lay = pwdDlg.layout();
	auto* main = lay.createMain(&pwdDlg, 1);

	main->Add(new wxStaticText(&pwdDlg, nullID, header));

	auto* inner = lay.createFlex(2);
	main->Add(inner);

	std::wstring const& name = site.GetName();
	if (!name.empty()) {
		inner->Add(new wxStaticText(&pwdDlg, nullID, _("Name:")));
		inner->Add(new wxStaticText(&pwdDlg, nullID, LabelEscape(name)));
	}

	inner->Add(new wxStaticText(&pwdDlg, nullID, _("Host:")));
	inner->Add(new wxStaticText(&pwdDlg, nullID, LabelEscape(site.Format(ServerFormat::with_optional_port))));

	if (!site.server.GetUser().empty()) {
		inner->Add(new wxStaticText(&pwdDlg, nullID, _("User:")));
		inner->Add(new wxStaticText(&pwdDlg, nullID, LabelEscape(site.server.GetUser())));
	}

	main->AddSpacer(0);

	inner = lay.createFlex(2);
	main->Add(inner);

	wxTextCtrl* newUser{};
	if (needs_user) {
		inner->Add(new wxStaticText(&pwdDlg, nullID, _("&User:")), lay.valign);
		newUser = new wxTextCtrlEx(&pwdDlg, nullID, wxString());
		newUser->SetMinSize(wxSize(150, -1));
		newUser->SetFocus();
		inner->Add(newUser, lay.valign);
	}

	wxTextCtrl* password{};
	wxTextCtrl* key{}; // for storj
	if (needs_pass) {
		inner->Add(new wxStaticText(&pwdDlg, nullID, _("&Password:")), lay.valign);
		password = new wxTextCtrlEx(&pwdDlg, nullID, wxString(), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
		password->SetMinSize(wxSize(150, -1));
		if (!newUser) {
			password->SetFocus();
		}
		inner->Add(password, lay.valign);

		if (site.server.GetProtocol() == STORJ) {
			inner->Add(new wxStaticText(&pwdDlg, nullID, _("Encryption &key:")), lay.valign);
			key = new wxTextCtrlEx(&pwdDlg, nullID);
			key->SetMinSize(wxSize(150, -1));
			inner->Add(key, lay.valign);
		}
	}

	wxCheckBox* remember{};
	if (canRemember && needs_pass) {
		main->AddSpacer(0);
		remember = new wxCheckBox(&pwdDlg, nullID, _("&Remember password until FileZilla is closed"));
		remember->SetValue(true);
		main->Add(remember);
	}

	auto* buttons = lay.createButtonSizer(&pwdDlg, main, true);

	auto ok = new wxButton(&pwdDlg, wxID_OK, _("&OK"));
	ok->SetDefault();
	buttons->AddButton(ok);

	auto cancel = new wxButton(&pwdDlg, wxID_CANCEL, _("Cancel"));
	buttons->AddButton(cancel);

	buttons->Realize();

	pwdDlg.GetSizer()->Fit(&pwdDlg);
	pwdDlg.GetSizer()->SetSizeHints(&pwdDlg);

	while (true) {
		if (pwdDlg.ShowModal() != wxID_OK) {
			return false;
		}

		if (newUser) {
			auto user = newUser->GetValue().ToStdWstring();
			if (user.empty()) {
				wxMessageBoxEx(_("No username given."), _("Invalid input"), wxICON_EXCLAMATION);
				continue;
			}
			site.server.SetUser(user);
		}

		if (password) {
			std::wstring pass = password->GetValue().ToStdWstring();
			if (site.server.GetProtocol() == STORJ) {
				std::wstring encryptionKey = key->GetValue().ToStdWstring();
				pass += L"|" + encryptionKey;
			}
			site.credentials.SetPass(pass);
		}

		if (remember && remember->IsChecked()) {
			RememberPassword(site);
		}

		break;
	}

	return true;
}


bool CLoginManager::AskDecryptor(fz::public_key const& pub, bool allowForgotten, bool allowCancel)
{
	if (!pub) {
		return false;
	}

	bool forgotten{};
	auto priv = GetDecryptor(pub, &forgotten);
	if (priv || (allowForgotten && forgotten)) {
		return true;
	}

	wxDialogEx pwdDlg;
	if (!pwdDlg.Create(wxGetApp().GetTopWindow(), nullID, _("Enter master password"))) {
		return false;
	}
	auto& lay = pwdDlg.layout();
	auto* main = lay.createMain(&pwdDlg, 1);

	main->Add(new wxStaticText(&pwdDlg, nullID, _("Please enter your current master password to change the password settings.")));

	auto* inner = lay.createFlex(2);
	main->Add(inner);

	inner->Add(new wxStaticText(&pwdDlg, nullID, _("Key identifier:")));
	inner->Add(new wxStaticText(&pwdDlg, nullID, fz::to_wstring(pub.to_base64().substr(0, 8))));

	inner->Add(new wxStaticText(&pwdDlg, nullID, _("Master &Password:")), lay.valign);

	auto* password = new wxTextCtrlEx(&pwdDlg, nullID, wxString(), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	password->SetMinSize(wxSize(150, -1));
	password->SetFocus();
	inner->Add(password, lay.valign);

	wxCheckBox* forgot{};
	if (allowForgotten) {
		main->AddSpacer(0);
		forgot = new wxCheckBox(&pwdDlg, nullID, _("I &forgot my master password. Delete all passwords stored with this key."));
		main->Add(forgot);
	}

	auto* buttons = lay.createButtonSizer(&pwdDlg, main, true);

	auto ok = new wxButton(&pwdDlg, wxID_OK, _("&OK"));
	ok->SetDefault();
	buttons->AddButton(ok);

	if (allowCancel) {
		auto cancel = new wxButton(&pwdDlg, wxID_CANCEL, _("Cancel"));
		buttons->AddButton(cancel);
	}

	buttons->Realize();

	pwdDlg.GetSizer()->Fit(&pwdDlg);
	pwdDlg.GetSizer()->SetSizeHints(&pwdDlg);

	while (true) {
		if (pwdDlg.ShowModal() != wxID_OK) {
			if (allowCancel) {
				return false;
			}
			continue;
		}

		if (allowForgotten && forgot->GetValue()) {
			decryptors_[pub] = fz::private_key();
		}
		else {
			auto pass = fz::to_utf8(password->GetValue().ToStdWstring());
			auto key = fz::private_key::from_password(pass, pub.salt_);

			if (key.pubkey() != pub) {
				wxMessageBoxEx(_("Wrong master password entered, it cannot be used to decrypt the stored passwords."), _("Invalid input"), wxICON_EXCLAMATION);
				continue;
			}
			decryptors_[pub] = key;
			decryptorPasswords_.emplace_back(std::move(pass));
		}
		break;
	}

	return true;
}

std::optional<std::string> KeyfilePasswordManager::query_password(Site const& site, std::string const& file, std::string const& fingerprint, std::string const& comment, bool & remember)
{
	wxDialogEx pwdDlg;
	if (!pwdDlg.Create(wxGetApp().GetTopWindow(), nullID, _("Enter SSH keyfile password"))) {
		return {};
	}
	auto& lay = pwdDlg.layout();
	auto* main = lay.createMain(&pwdDlg, 1);

	wxString header;
	if (site) {
		header = _("Please enter the password of the SSH private key file to log into the server.");
	}
	else {
		header = _("Please enter the password of SSH private key file.");
	}

	main->Add(new wxStaticText(&pwdDlg, nullID, header));

	if (site) {
		auto heading = new wxStaticText(&pwdDlg, nullID, _("Server"));
		heading->SetFont(heading->GetFont().Bold());
		main->Add(heading);

		auto inner = lay.createFlex(2);
		main->Add(inner, 0, wxLEFT, lay.indent);

		std::wstring const& name = site.GetName();
		if (!name.empty()) {
			inner->Add(new wxStaticText(&pwdDlg, nullID, _("Name:")));
			inner->Add(new wxStaticText(&pwdDlg, nullID, LabelEscape(name)));
		}

		inner->Add(new wxStaticText(&pwdDlg, nullID, _("Host:")));
		inner->Add(new wxStaticText(&pwdDlg, nullID, LabelEscape(site.Format(ServerFormat::with_optional_port))));

		if (!site.server.GetUser().empty()) {
			inner->Add(new wxStaticText(&pwdDlg, nullID, _("User:")));
			inner->Add(new wxStaticText(&pwdDlg, nullID, LabelEscape(site.server.GetUser())));
		}
		main->AddSpacer(0);
	}


	if (!file.empty() || !fingerprint.empty() || !comment.empty()) {
		auto heading = new wxStaticText(&pwdDlg, nullID, _("Key details"));
		heading->SetFont(heading->GetFont().Bold());
		main->Add(heading);
		auto inner = lay.createFlex(2);
		main->Add(inner, 0, wxLEFT, lay.indent);

		if (!file.empty()) {
			inner->Add(new wxStaticText(&pwdDlg, nullID, _("Filename:")));
			inner->Add(new wxStaticText(&pwdDlg, nullID, LabelEscape(fz::to_wstring_from_utf8(file))));
		}
		if (!fingerprint.empty()) {
			inner->Add(new wxStaticText(&pwdDlg, nullID, _("Fingerprint:")));
			inner->Add(new wxStaticText(&pwdDlg, nullID, LabelEscape(fz::to_wstring_from_utf8(fingerprint))));
		}
		if (!comment.empty()) {
			inner->Add(new wxStaticText(&pwdDlg, nullID, _("Comment:")));
			inner->Add(new wxStaticText(&pwdDlg, nullID, LabelEscape(fz::to_wstring_from_utf8(comment))));
		}

		main->AddSpacer(0);
	}

	wxTextCtrlEx* password{};
	wxCheckBox* cbRemember{};

	{
		bool can_remember = !file.empty() || !fingerprint.empty();
		password = new wxTextCtrlEx(&pwdDlg, nullID, {}, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
		auto inner = lay.createFlex(2);
		main->Add(inner);
		inner->Add(new wxStaticText(&pwdDlg, nullID, _("&Password:")), lay.valign);
		password->SetMinSize(wxSize(150, -1));
		password->SetFocus();
		inner->Add(password, lay.valign);

		if (can_remember) {
			cbRemember = new wxCheckBox(&pwdDlg, nullID, _("&Remember password until FileZilla is closed"));
			cbRemember->SetValue(true);
			main->Add(cbRemember);
		}
	}

	auto* buttons = lay.createButtonSizer(&pwdDlg, main, true);

	auto ok = new wxButton(&pwdDlg, wxID_OK, _("&OK"));
	ok->SetDefault();
	buttons->AddButton(ok);

	auto cancel = new wxButton(&pwdDlg, wxID_CANCEL, _("Cancel"));
	buttons->AddButton(cancel);

	buttons->Realize();

	pwdDlg.GetSizer()->Fit(&pwdDlg);
	pwdDlg.GetSizer()->SetSizeHints(&pwdDlg);

	if (pwdDlg.ShowModal() != wxID_OK) {
		return {};
	}

	if (cbRemember && cbRemember->GetValue()) {
		remember = true;
	}

	return fz::to_utf8(password->GetValue());
}

