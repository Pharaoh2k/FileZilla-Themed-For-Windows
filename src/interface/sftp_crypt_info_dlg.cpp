#include "filezilla.h"
#include "filezillaapp.h"
#include "sftp_crypt_info_dlg.h"
#include "dialogex.h"

#include <fzssh/ssh.hpp>

#include <wx/statbox.h>

void CSftpEncryptioInfoDialog::ShowDialog(fz::ssh::algorithm_info const& algs, std::string const& fingerprint)
{
	wxDialogEx dlg;
	if (!dlg.Create(wxGetApp().GetTopWindow(), nullID, _("Encryption details"))) {
		return;
	}

	auto & lay = dlg.layout();
	auto main = lay.createMain(&dlg, 1);

	{
		auto [box, inner] = lay.createStatBox(main, _("Key exchange"), 2);
		inner->Add(new wxStaticText(box, nullID, _("Algorithm:")));
		inner->Add(new wxStaticText(box, nullID, LabelEscape(fz::to_wstring_from_utf8(algs.kex_))));
	}
	{
		auto [box, inner] = lay.createStatBox(main, _("Server host key"), 2);
		inner->Add(new wxStaticText(box, nullID, _("Signature algorithm:")));
		inner->Add(new wxStaticText(box, nullID, LabelEscape(fz::to_wstring_from_utf8(algs.hostkey_signature_))));
		inner->Add(new wxStaticText(box, nullID, _("Fingerprint:")));
		inner->Add(new wxStaticText(box, nullID, LabelEscape(fz::to_wstring_from_utf8(fingerprint))));
	}
	{
		auto [box, inner] = lay.createStatBox(main, _("Encryption"), 2);
		inner->Add(new wxStaticText(box, nullID, _("Client to server cipher:")));
		inner->Add(new wxStaticText(box, nullID, LabelEscape(fz::to_wstring_from_utf8(algs.cipher_c2s_))));
		inner->Add(new wxStaticText(box, nullID, _("Client to server MAC:")));
		inner->Add(new wxStaticText(box, nullID, LabelEscape(fz::to_wstring_from_utf8(algs.mac_c2s_))));
		inner->Add(new wxStaticText(box, nullID, _("Server to client cipher:")));
		inner->Add(new wxStaticText(box, nullID, LabelEscape(fz::to_wstring_from_utf8(algs.cipher_s2c_))));
		inner->Add(new wxStaticText(box, nullID, _("Server to client MAC:")));
		inner->Add(new wxStaticText(box, nullID, LabelEscape(fz::to_wstring_from_utf8(algs.mac_s2c_))));
	}

	auto buttons = lay.createButtonSizer(&dlg, main, false);
	auto ok = new wxButton(&dlg, wxID_OK, _("OK"));
	ok->SetDefault();
	buttons->AddButton(ok);
	buttons->Realize();

	dlg.GetSizer()->Fit(&dlg);
	dlg.GetSizer()->SetSizeHints(&dlg);

	dlg.ShowModal();
}
