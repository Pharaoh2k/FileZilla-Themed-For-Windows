#include "filezilla.h"
#include "filezillaapp.h"
#include "verifyhostkeydialog.h"
#include "dialogex.h"
#include "themeprovider.h"

#include <wx/statbox.h>

#include "../commonui/ipcmutex.h"

#include <fzssh/pubkey.hpp>

#include <libfilezilla/format.hpp>

bool CVerifyHostkeyDialog::AskUnknownHostkey(Site const& site, fz::ssh::public_key const& key, fz::ssh::algorithm_info const& info, verification_flags flags, bool &remember)
{
	wxDialogEx dlg;

	wxString title;
	if (flags & verification_flags::insecure) {
		title = _("Insecure SFTP server");
	}
	else if (flags & verification_flags::changed) {
		title = _("Host key mismatch");
	}
	else {
		title = _("Unknown host key");
	}

	if (!dlg.Create(wxGetApp().GetTopWindow(), nullID, title)) {
		wxBell();
		return false;
	}

	auto & lay = dlg.layout();
	auto top = lay.createMain(&dlg, 2);

	top->Add(new wxStaticBitmap(&dlg, nullID, wxArtProvider::GetBitmap((flags & (verification_flags::changed | verification_flags::insecure)) ? wxART_WARNING : wxART_INFORMATION, wxART_OTHER, wxSize(32, 32))), 0, wxALL, lay.dlgUnits(2));

	auto main = lay.createFlex(1);
	top->Add(main);

	if (flags & verification_flags::changed) {
		main->Add(new wxStaticText(&dlg, nullID, _("Warning: Potential security breach!")));
	}

	if (flags & verification_flags::insecure) {
		auto desc = new wxStaticText(&dlg, nullID, wxString());
		wxString t = _("The SFTP server you are connecting to uses insecure algorithms. If you proceed, your login credentials and any data exchanged with this server could get compromised.");
		dlg.WrapText(desc, t, lay.dlgUnits(200));
		desc->SetLabel(t);
		main->Add(desc);
	}

	if (flags & (verification_flags::unknown | verification_flags::changed)) {
		auto desc = new wxStaticText(&dlg, nullID, wxString());
		wxString t;
		if (flags & verification_flags::changed) {
			t = _("The server's host key does not match the key that has been cached. This means that either the administrator has changed the host key, or you are actually trying to connect to another computer pretending to be the server.");
		}
		else {
			t = _("The server's host key is unknown. You have no guarantee that the server is the computer you think it is.");
		}

		dlg.WrapText(desc, t, lay.dlgUnits(200));
		desc->SetLabel(t);
		main->Add(desc);

		if (flags & verification_flags::changed) {
			main->Add(new wxStaticText(&dlg, nullID, _("If the host key change was not expected, please contact the server administrator.")));
		}
	}

	std::wstring const host = site.Format(ServerFormat::with_port);

	auto [box, inner] = lay.createStatBox(main, _("Details"), 2);

	inner->Add(new wxStaticText(box, nullID, _("Host:")));
	inner->Add(new wxStaticText(box, nullID, LabelEscape(host)));
	inner->Add(new wxStaticText(box, nullID, _("Hostkey algorithm:")));
	inner->Add(new wxStaticText(box, nullID, wxString(LabelEscape(fz::to_wstring_from_utf8(key.name())))));
	inner->Add(new wxStaticText(box, nullID, _("Fingerprint:")));
	auto fp = new wxStaticText(box, nullID, LabelEscape(fz::to_wstring_from_utf8(key.fingerprint())));
	inner->Add(fp);
	if (flags & verification_flags::changed) {
		fp->SetForegroundColour(wxColour(255, 0, 0));

		inner->Add(new wxStaticText(box, nullID, L""));
		auto c = new wxStaticText(box, nullID, _("(changed)"));
		c->SetForegroundColour(wxColour(255, 0, 0));
		inner->Add(c);
	}

	if (flags & verification_flags::insecure) {
		auto [box, inner] = lay.createStatBox(main, _("Algorithms"), 2);
		auto make_cipher_label = [&](wxString const& type, std::string_view const& alg) {
			inner->Add(new wxStaticText(box, nullID, type));

			wxString t = LabelEscape(fz::to_wstring_from_utf8(alg));
			bool insecure_alg = is_insecure_algorithm(alg);
			if (insecure_alg) {
				t += ' ';
				t += _("(insecure)");
			}
			auto label = new wxStaticText(box, nullID, t);
			if (insecure_alg) {
				label->SetForegroundColour(wxColour(255, 0, 0));
			}
			inner->Add(label);
		};

		make_cipher_label(_("Key exchange:"), info.kex_);
		make_cipher_label(_("Client to server cipher:"), info.cipher_c2s_);
		make_cipher_label(_("Server to client cipher:"), info.cipher_s2c_);
		make_cipher_label(_("Client to server MAC:"), info.mac_c2s_);
		make_cipher_label(_("Server to client MAC:"), info.mac_s2c_);
		make_cipher_label(_("Host key signature:"), info.hostkey_signature_);
	}

	main->Add(new wxStaticText(&dlg, nullID, flags & verification_flags::changed ? _("Trust the new key and carry on connecting?") : _("Trust this host and carry on connecting?")));

	wxCheckBox* confirm_insecure{};
	if (flags & verification_flags::insecure) {
		confirm_insecure = new wxCheckBox(&dlg, nullID, _("Allow use of the insecure algorithms with this server."));
		main->Add(confirm_insecure);
	}

	wxCheckBox* always{};
	if (flags & (verification_flags::changed | verification_flags::unknown)) {
		always = new wxCheckBox(&dlg, nullID, flags & verification_flags::changed ? _("Update cached key for this host") : _("&Always trust this host, add this key to the cache"));
	}
	else {
		always = new wxCheckBox(&dlg, nullID, _("Permanently &remember this choice"));
	}
	main->Add(always);

	auto buttons = lay.createButtonSizer(&dlg, main, false);

	auto ok = new wxButton(&dlg, wxID_OK, _("&OK"));
	ok->SetDefault();
	buttons->AddButton(ok);

	auto cancel = new wxButton(&dlg, wxID_CANCEL, _("Cancel"));
	buttons->AddButton(cancel);

	if (confirm_insecure) {
		always->Disable();
		ok->Disable();
		confirm_insecure->Bind(wxEVT_CHECKBOX, [&](wxEvent&) {
			always->Enable(confirm_insecure->GetValue());
			ok->Enable(confirm_insecure->GetValue());
		});
	}

	buttons->Realize();

	dlg.GetSizer()->Fit(&dlg);
	dlg.GetSizer()->SetSizeHints(&dlg);

	int res = dlg.ShowModal();

	if (res == wxID_OK) {
		if (confirm_insecure && !confirm_insecure->GetValue()) {
			return false;
		}
		remember = always->GetValue();
		return true;
	}

	return false;
}

void CVerifyHostkeyDialog::OnSaveError(std::wstring const& message)
{
	wxMessageBoxEx(message, _("Error saving trusted hostkeys"), wxICON_ERROR);
}
