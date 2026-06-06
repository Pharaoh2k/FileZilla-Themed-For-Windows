#include "../filezilla.h"
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_connection_sftp.h"
#include "../filezillaapp.h"
#include "../inputdialog.h"
#if USE_MAC_SANDBOX
#include "../osx_sandbox_userdirs.h"
#endif

#include <fzssh/privkey.hpp>

#include <wx/filedlg.h>
#include <wx/listctrl.h>
#include <wx/statbox.h>

struct COptionsPageConnectionSFTP::impl
{
	wxListCtrl* keys_{};
	wxButton* add_{};
	wxButton* remove_{};

	wxCheckBox* compression_{};
};

COptionsPageConnectionSFTP::COptionsPageConnectionSFTP()
	: impl_(std::make_unique<impl>())
{
}

COptionsPageConnectionSFTP::~COptionsPageConnectionSFTP()
{
}

bool COptionsPageConnectionSFTP::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	main->AddGrowableRow(0);
	SetSizer(main);

	{
		auto [box, inner] = lay.createStatBox(main, _("Public Key Authentication"), 1);
		inner->AddGrowableCol(0);
		inner->AddGrowableRow(2);
		inner->Add(new wxStaticText(box, nullID, _("To support public key authentication, FileZilla needs to know the private keys to use.")));
		inner->Add(new wxStaticText(box, nullID, _("Private &keys:")));
		impl_->keys_ = new wxListCtrl(box, nullID, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxBORDER_SUNKEN);
		impl_->keys_->Bind(wxEVT_LIST_ITEM_SELECTED, &COptionsPageConnectionSFTP::OnSelChanged, this);
		impl_->keys_->Bind(wxEVT_LIST_ITEM_DESELECTED, &COptionsPageConnectionSFTP::OnSelChanged, this);
		inner->Add(impl_->keys_, lay.grow);

		auto row = lay.createGrid(2);
		inner->Add(row, lay.halign);
		impl_->add_ = new wxButton(box, nullID, _("&Add key file..."));
		impl_->add_->Bind(wxEVT_BUTTON, &COptionsPageConnectionSFTP::OnAdd, this);
		row->Add(impl_->add_, lay.valign);
		impl_->remove_ = new wxButton(box, nullID, _("&Remove key"));
		impl_->remove_->Bind(wxEVT_BUTTON, &COptionsPageConnectionSFTP::OnRemove, this);
		row->Add(impl_->remove_, lay.valign);

#ifdef __WXMSW__
		inner->Add(new wxStaticText(box, nullID, _("Alternatively you can use the Pageant tool from PuTTY to manage your keys, FileZilla does recognize Pageant.")));
#else
		inner->Add(new wxStaticText(box, nullID, _("Alternatively you can use your system's SSH agent. To do so, make sure the SSH_AUTH_SOCK environment variable is set.")));
#endif
	}
#if 0
	{
		auto [box, inner] = lay.createStatBox(main, _("Other SFTP options"), 1);

		impl_->compression_ = new wxCheckBox(box, nullID, _("&Enable compression"));
		inner->Add(impl_->compression_);
	}
#endif
	return true;
}

bool COptionsPageConnectionSFTP::LoadPage()
{
	impl_->keys_->InsertColumn(0, _("Filename"), wxLIST_FORMAT_LEFT, 150);
	impl_->keys_->InsertColumn(1, _("Comment"), wxLIST_FORMAT_LEFT, 100);
	impl_->keys_->InsertColumn(2, _("Type"), wxLIST_FORMAT_LEFT, 100);
	impl_->keys_->InsertColumn(3, _("Fingerprint"), wxLIST_FORMAT_LEFT, 350);

	// Generic wxListCtrl has gross minsize
	wxSize size = impl_->keys_->GetMinSize();
	size.x = 1;
	impl_->keys_->SetMinSize(size);

	std::wstring keyFiles = m_pOptions->get_string(OPTION_SFTP_KEYFILES);
	auto tokens = fz::strtok(keyFiles, L"\r\n");
	for (auto const& token : tokens) {
		AddKey(token, true);
	}

	bool failure = false;

	SetCtrlState();

	if (impl_->compression_) {
		impl_->compression_->SetValue(m_pOptions->get_int(OPTION_SFTP_COMPRESSION) != 0);
	}

	return !failure;
}

bool COptionsPageConnectionSFTP::SavePage()
{
	// Don't save keys on process error
	std::wstring keyFiles;
	for (int i = 0; i < impl_->keys_->GetItemCount(); ++i) {
		if (!keyFiles.empty()) {
			keyFiles += L"\n";
		}
		keyFiles += impl_->keys_->GetItemText(i).ToStdWstring();
	}
	m_pOptions->set(OPTION_SFTP_KEYFILES, keyFiles);

	if (impl_->compression_) {
		m_pOptions->set(OPTION_SFTP_COMPRESSION, impl_->compression_->GetValue() ? 1 : 0);
	}

	return true;
}

void COptionsPageConnectionSFTP::OnAdd(wxCommandEvent&)
{
	wxFileDialog dlg(this, _("Select file containing private key"), wxString(), wxString(), wxFileSelectorDefaultWildcardStr, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	std::wstring const file = dlg.GetPath().ToStdWstring();

	if (AddKey(dlg.GetPath().ToStdWstring(), false)) {
#if USE_MAC_SANDBOX
		OSXSandboxUserdirs::Get().AddFile(file);
#endif
	}
}

void COptionsPageConnectionSFTP::OnRemove(wxCommandEvent&)
{
	int index = impl_->keys_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (index == -1) {
		return;
	}

	impl_->keys_->DeleteItem(index);
}

std::vector<fz::ssh::private_key_info> LoadKeyfile(std::wstring keyFile, bool silent)
{
	fz::buffer b;
	if (!fz::read_file(fz::to_native(keyFile), b, 128*1024)) {
		if (!silent) {
			wxMessageBoxEx(_("Could not read file"), _("Cannot load key file"), wxICON_INFORMATION);
			return {};
		}
	}

	auto infos = fz::ssh::load_private_key_infos(b.to_view(), fz::get_null_logger(), {});

	if (infos.empty()) {
		if (!silent) {
			wxMessageBoxEx(_("The file does not contain a valid or recognized private key"), _("Cannot load key file"), wxICON_INFORMATION);
			return {};
		}
	}
	return infos;
}

bool COptionsPageConnectionSFTP::AddKey(std::wstring keyFile, bool silent)
{
	if (KeyFileExists(keyFile)) {
		if (!silent) {
			wxMessageBoxEx(_("Selected file is already loaded"), _("Cannot load key file"), wxICON_INFORMATION);
		}
		return false;
	}

	auto infos = LoadKeyfile(keyFile, silent);
	if (infos.empty()) {
		return false;
	}

	bool added_encrypted{};
	for (auto const& info : infos) {
		if (info.pubkey_) {
			int index = impl_->keys_->InsertItem(impl_->keys_->GetItemCount(), keyFile);
			std::string fingerprint = info.pubkey_->fingerprint();

			impl_->keys_->SetItem(index, 1, fz::to_wstring_from_utf8(info.pubkey_->comment_));
			impl_->keys_->SetItem(index, 2, fz::to_wstring_from_utf8(info.pubkey_->name()));
			impl_->keys_->SetItem(index, 3, fz::to_wstring_from_utf8(fingerprint));
		}
		else if (!added_encrypted) {
			added_encrypted = true;
			int index = impl_->keys_->InsertItem(impl_->keys_->GetItemCount(), keyFile);

			impl_->keys_->SetItem(index, 2, _("Encrypted"));
			impl_->keys_->SetItem(index, 3, _("Not available"));
		}
	}

	return true;
}

bool COptionsPageConnectionSFTP::KeyFileExists(std::wstring const& keyFile)
{
	for (int i = 0; i < impl_->keys_->GetItemCount(); ++i) {
		if (impl_->keys_->GetItemText(i) == keyFile) {
			return true;
		}
	}
	return false;
}

void COptionsPageConnectionSFTP::SetCtrlState()
{
	int index = impl_->keys_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	impl_->remove_->Enable(index != -1);
}

void COptionsPageConnectionSFTP::OnSelChanged(wxListEvent&)
{
	SetCtrlState();
}
