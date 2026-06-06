#include "filezilla.h"
#include "inputdialog.h"
#include "textctrlex.h"

bool CInputDialog::Create(wxWindow* parent, wxString const& title, wxString const& text, int max_len, bool password)
{
	SetParent(parent);

	if (!wxDialogEx::Create(parent, -1, title)) {
		return false;
	}

	auto& lay = layout();
	auto * main = lay.createMain(this, 1);

	main->Add(new wxStaticText(this, -1, text));

	textCtrl_ = new wxTextCtrlEx(this, -1, wxString(), wxDefaultPosition, wxDefaultSize, password ? wxTE_PASSWORD : 0);

	main->Add(textCtrl_, lay.grow)->SetMinSize(lay.dlgUnits(150), -1);
	if (max_len != -1) {
		textCtrl_->SetMaxLength(max_len);
	}

	auto * buttons = lay.createButtonSizer(this, main, true);

	auto ok = new wxButton(this, wxID_OK, _("&OK"));
	ok->SetDefault();
	buttons->AddButton(ok);

	auto cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
	buttons->AddButton(cancel);
	buttons->Realize();

	auto onButton = [this](wxEvent& evt) {EndModal(evt.GetId()); };
	ok->Bind(wxEVT_BUTTON, onButton);
	cancel->Bind(wxEVT_BUTTON, onButton);

	GetSizer()->Fit(this);

	WrapRecursive(this, 2.0);

	textCtrl_->SetFocus();
	ok->Disable();

	textCtrl_->Bind(wxEVT_TEXT, [this, ok](wxEvent const&){
		ok->Enable(allowEmpty_ || !textCtrl_->GetValue().empty());
	});

	return true;
}

void CInputDialog::AllowEmpty(bool allowEmpty)
{
	allowEmpty_ = allowEmpty;
	XRCCTRL(*this, "wxID_OK", wxButton)->Enable(allowEmpty_ ? true : (!textCtrl_->GetValue().empty()));
}

void CInputDialog::SetValue(wxString const& value)
{
	textCtrl_->SetValue(value);
}

wxString CInputDialog::GetValue() const
{
	return textCtrl_->GetValue();
}

bool CInputDialog::SelectText(int start, int end)
{
#ifdef __WXGTK__
	Show();
#endif
	textCtrl_->SetFocus();
	textCtrl_->SetSelection(start, end);
	return true;
}
