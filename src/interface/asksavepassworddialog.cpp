#include "filezilla.h"
#include "asksavepassworddialog.h"
#include "Options.h"
#include "filezillaapp.h"
#include "sitemanager.h"
#include "textctrlex.h"
#include <libfilezilla/util.hpp>

struct CAskSavePasswordDialog::impl final
{
	impl(COptionsBase & options)
		: options_(options)
	{}

	COptionsBase & options_;
	wxRadioButton* nosave_{};
	wxRadioButton* save_{};
	wxRadioButton* usemaster_{};
	wxTextCtrlEx* pw_{};
	wxTextCtrlEx* repeat_{};
};

CAskSavePasswordDialog::CAskSavePasswordDialog(COptionsBase & options)
	: impl_(std::make_unique<impl>(options))
{
}

CAskSavePasswordDialog::~CAskSavePasswordDialog() = default;

bool CAskSavePasswordDialog::Create(wxWindow*)
{
	if (!wxDialogEx::Create(nullptr, nullID, _("Remember passwords?"))) {
		return false;
	}

	auto & lay = layout();
	auto main = lay.createMain(this, 1);

	main->Add(new wxStaticText(this, nullID, _("Would you like FileZilla to remember passwords?")));

	main->Add(new wxStaticText(this, nullID, _("When allowing FileZilla to remember passwords, you can reconnect without having to re-enter the password after restarting FileZilla.")));

	impl_->save_ = new wxRadioButton(this, nullID, _("Sav&e passwords"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	impl_->nosave_ = new wxRadioButton(this, nullID, _("D&o not save passwords"));
	impl_->usemaster_ = new wxRadioButton(this, nullID, _("Sa&ve passwords protected by a master password"));
	main->Add(impl_->save_);
	main->Add(impl_->nosave_);
	main->Add(impl_->usemaster_);

	auto inner = lay.createFlex(2);
	main->Add(inner, 0, wxLEFT|wxGROW, lay.indent);

	inner->AddGrowableCol(1);
	inner->Add(new wxStaticText(this, nullID, _("&Master password:")), lay.valign);
	impl_->pw_ = new wxTextCtrlEx(this, nullID, wxString(), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	inner->Add(impl_->pw_, lay.valigng);
	inner->Add(new wxStaticText(this, nullID, _("&Repeat password:")), lay.valign);
	impl_->repeat_ = new wxTextCtrlEx(this, nullID, wxString(), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	inner->Add(impl_->repeat_, lay.valigng);
	
	main->Add(new wxStaticText(this, nullID, _("A lost master password cannot be recovered! Please thoroughly memorize your password.")), 0, wxLEFT, lay.indent);
	
	auto buttons = lay.createButtonSizer(this, main, true);

	auto ok = new wxButton(this, wxID_OK, _("&OK"));
	ok->SetDefault();
	buttons->AddButton(ok);

	auto cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
	buttons->AddButton(cancel);

	buttons->Realize();

	WrapRecursive(this, 2.0, nullptr);

	auto onChange = [this](wxEvent const&) {
		bool const checked = impl_->usemaster_->GetValue();
		impl_->pw_->Enable(checked);
		impl_->repeat_->Enable(checked);
	};
	onChange(wxCommandEvent());

	impl_->save_->Bind(wxEVT_RADIOBUTTON, onChange);
	impl_->nosave_->Bind(wxEVT_RADIOBUTTON, onChange);
	impl_->usemaster_->Bind(wxEVT_RADIOBUTTON, onChange);

	ok->Bind(wxEVT_BUTTON, &CAskSavePasswordDialog::OnOk, this);

	return true;
}

void CAskSavePasswordDialog::OnOk(wxCommandEvent& event)
{
	bool const useMaster = impl_->usemaster_->GetValue();
	if (useMaster) {
		std::wstring pw = impl_->pw_->GetValue().ToStdWstring();
		std::wstring repeat = impl_->repeat_->GetValue().ToStdWstring();
		if (pw != repeat) {
			wxMessageBoxEx(_("The entered passwords are not the same."), _("Invalid input"));
			return;
		}

		if (pw.size() < 8) {
			wxMessageBoxEx(_("The master password needs to be at least 8 characters long."), _("Invalid input"));
			return;
		}

		auto priv = fz::private_key::from_password(fz::to_utf8(pw), fz::random_bytes(fz::private_key::salt_size));
		auto pub = priv.pubkey();
		if (!pub) {
			wxMessageBoxEx(_("Could not generate key"), _("Error"));
			return;
		}
		else {
			impl_->options_.set(OPTION_DEFAULT_KIOSKMODE, 0);
			impl_->options_.set(OPTION_MASTERPASSWORDENCRYPTOR, fz::to_wstring_from_utf8(pub.to_base64()));
		}
	}
	else {
		bool const save = impl_->save_->GetValue();
		impl_->options_.set(OPTION_DEFAULT_KIOSKMODE, save ? 0 : 1);
		impl_->options_.set(OPTION_MASTERPASSWORDENCRYPTOR, std::wstring());
	}

	event.Skip();
}

bool CAskSavePasswordDialog::Run(wxWindow* parent, COptionsBase & options)
{
	bool ret = true;

	if (options.get_int(OPTION_DEFAULT_KIOSKMODE) == 0 && options.get_int(OPTION_PROMPTPASSWORDSAVE) != 0 &&
		!CSiteManager::HasSites() && options.get_string(OPTION_MASTERPASSWORDENCRYPTOR).empty())
	{
		CAskSavePasswordDialog dlg(options);
		if (dlg.Create(parent)) {
			ret = dlg.ShowModal() == wxID_OK;
			if (ret) {
				options.set(OPTION_PROMPTPASSWORDSAVE, 0);
			}
		}
	}
	else {
		options.set(OPTION_PROMPTPASSWORDSAVE, 0);
	}

	return ret;
}
