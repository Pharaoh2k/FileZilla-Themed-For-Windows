#include "filezilla.h"
#include "speedlimits_dialog.h"
#include "Options.h"
#include "textctrlex.h"
#include "themeprovider.h"

#include "../include/sizeformatting.h"

struct CSpeedLimitsDialog::impl final
{
	impl(COptionsBase & options)
		: options_(options)
	{}

	wxCheckBox* enable_{};
	wxTextCtrlEx* download_{};
	wxTextCtrlEx* upload_{};

	COptionsBase & options_;
};

CSpeedLimitsDialog::CSpeedLimitsDialog(COptionsBase & options)
	: impl_(std::make_unique<impl>(options))
{
}

CSpeedLimitsDialog::~CSpeedLimitsDialog()
{
}


void CSpeedLimitsDialog::Run(wxWindow* parent)
{
	int downloadlimit = impl_->options_.get_int(OPTION_SPEEDLIMIT_INBOUND);
	int uploadlimit = impl_->options_.get_int(OPTION_SPEEDLIMIT_OUTBOUND);
	bool enable = impl_->options_.get_int(OPTION_SPEEDLIMIT_ENABLE) != 0;
	if (!downloadlimit && !uploadlimit) {
		enable = false;
	}

	if (!Create(parent, nullID, _("Speed limits"))) {
		return;
	}

	auto& lay = layout();
	auto main = lay.createMain(this, 1);

	auto split = lay.createFlex(2);
	main->Add(split);

	split->Add(CThemeProvider::Get()->createStaticBitmap(this, L"ART_SPEEDLIMITS", iconSizeLarge));

	auto right = lay.createFlex(1);
	split->Add(right);

	impl_->enable_ = new wxCheckBox(this, nullID, _("&Enable speed limits"));
	impl_->enable_->SetFocus();
	right->Add(impl_->enable_);

	auto inner = lay.createFlex(3);
	right->Add(inner);

	wxString const unit = SizeFormatter(impl_->options_).GetUnitSymbol(UnitPrefix::kilo, 1024);

	inner->Add(new wxStaticText(this, nullID, _("Download &limit:")), lay.valign);
	impl_->download_ = new wxTextCtrlEx(this, nullID);
	inner->Add(impl_->download_, lay.valign)->SetMinSize(wxSize(lay.dlgUnits(35), -1));
	inner->Add(new wxStaticText(this, nullID, wxString::Format(_("(in %s/s)"), unit)), lay.valign);
	inner->Add(new wxStaticText(this, nullID, _("U&pload limit:")), lay.valign);
	impl_->upload_ = new wxTextCtrlEx(this, -1);
	inner->Add(impl_->upload_, lay.valign)->SetMinSize(wxSize(lay.dlgUnits(35), -1));
	inner->Add(new wxStaticText(this, nullID, wxString::Format(_("(in %s/s)"), unit)), lay.valign);
	
	right->Add(new wxStaticText(this, nullID, _("Enter 0 for unlimited speed.")));

	auto buttons = lay.createButtonSizer(this, main, true);

	auto ok = new wxButton(this, wxID_OK, _("&OK"));
	ok->SetDefault();
	buttons->AddButton(ok);
	ok->Bind(wxEVT_BUTTON, &CSpeedLimitsDialog::OnOK, this);

	auto cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
	buttons->AddButton(cancel);

	buttons->Realize();

	GetSizer()->Fit(this);

	impl_->enable_->SetValue(enable);

	impl_->download_->SetMaxLength(9);
	impl_->download_->ChangeValue(fz::to_wstring(downloadlimit));
	impl_->download_->Enable(enable);

	impl_->upload_->SetMaxLength(9);
	impl_->upload_->ChangeValue(fz::to_wstring(uploadlimit));
	impl_->upload_->Enable(enable);

	impl_->enable_->Bind(wxEVT_CHECKBOX, &CSpeedLimitsDialog::OnToggleEnable, this);

	ShowModal();
}

void CSpeedLimitsDialog::OnOK(wxCommandEvent&)
{
	long download, upload;
	if (!impl_->download_->GetValue().ToLong(&download) || (download < 0)) {
		const wxString unit = SizeFormatter(impl_->options_).GetUnitSymbol(UnitPrefix::kilo, 1024);
		wxMessageBoxEx(wxString::Format(_("Please enter a download speed limit greater or equal to 0 %s/s."), unit), _("Speed Limits"), wxOK, this);
		return;
	}

	if (!impl_->upload_->GetValue().ToLong(&upload) || (upload < 0)) {
		const wxString unit = SizeFormatter(impl_->options_).GetUnitSymbol(UnitPrefix::kilo, 1024);
		wxMessageBoxEx(wxString::Format(_("Please enter an upload speed limit greater or equal to 0 %s/s."), unit), _("Speed Limits"), wxOK, this);
		return;
	}

	impl_->options_.set(OPTION_SPEEDLIMIT_INBOUND, download);
	impl_->options_.set(OPTION_SPEEDLIMIT_OUTBOUND, upload);

	bool enable = impl_->enable_->GetValue() ? 1 : 0;
	impl_->options_.set(OPTION_SPEEDLIMIT_ENABLE, enable && (download || upload));

	EndDialog(wxID_OK);
}

void CSpeedLimitsDialog::OnToggleEnable(wxCommandEvent& event)
{
	impl_->download_->Enable(event.IsChecked());
	impl_->upload_->Enable(event.IsChecked());
}
