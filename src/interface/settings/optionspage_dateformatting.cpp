#include "../filezilla.h"

#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_dateformatting.h"

#include "../Options.h"
#include "../textctrlex.h"

#include <wx/hyperlink.h>
#include <wx/statbox.h>

struct COptionsPageDateFormatting::impl final
{
	wxRadioButton* date_system_{};
	wxRadioButton* date_iso_{};
	wxRadioButton* date_custom_{};
	wxTextCtrlEx* date_format_{};
	
	wxRadioButton* time_system_{};
	wxRadioButton* time_iso_{};
	wxRadioButton* time_custom_{};
	wxTextCtrlEx* time_format_{};
};

COptionsPageDateFormatting::COptionsPageDateFormatting()
	: impl_(std::make_unique<impl>())
{
}

COptionsPageDateFormatting::~COptionsPageDateFormatting()
{
}


bool COptionsPageDateFormatting::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	SetSizer(main);

	{
		auto [box, inner] = lay.createStatBox(main, _("Date formatting"), 1);
		impl_->date_system_ = new wxRadioButton(box, nullID, _("Use system &defaults for current language"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		inner->Add(impl_->date_system_);
		impl_->date_iso_ = new wxRadioButton(box, nullID, _("&ISO 8601 (example: 2007-09-15)"));
		inner->Add(impl_->date_iso_);
		impl_->date_custom_ = new wxRadioButton(box, nullID, _("C&ustom"));
		inner->Add(impl_->date_custom_);

		auto row = lay.createFlex(2);
		inner->Add(row, 0, wxLEFT, lay.indent);
		impl_->date_format_ = new wxTextCtrlEx(box, nullID, wxString(), wxDefaultPosition, lay.defTextCtrlSize);
		row->Add(impl_->date_format_, lay.valign);
		row->Add(new wxStaticText(box, nullID, _("(example: %Y-%m-%d)")), lay.valign);
	}
	{
		auto [box, inner] = lay.createStatBox(main, _("Time formatting"), 1);
		impl_->time_system_ = new wxRadioButton(box, nullID, _("Us&e system defaults for current language"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		inner->Add(impl_->time_system_);
		impl_->time_iso_ = new wxRadioButton(box, nullID, _("I&SO 8601 (example: 15:47)"));
		inner->Add(impl_->time_iso_);
		impl_->time_custom_ = new wxRadioButton(box, nullID, _("Cus&tom"));
		inner->Add(impl_->time_custom_);

		auto row = lay.createFlex(2);
		inner->Add(row, 0, wxLEFT, lay.indent);
		impl_->time_format_ = new wxTextCtrlEx(box, nullID, wxString(), wxDefaultPosition, lay.defTextCtrlSize);
		row->Add(impl_->time_format_, lay.valign);
		row->Add(new wxStaticText(box, nullID, _("(example: %H-%M-%S)")), lay.valign);
	}

	auto link = new wxHyperlinkCtrl(this, nullID, _("Show details about custom date and time formats"), L"https://wiki.filezilla-project.org/Date_and_Time_formatting");
	link->SetToolTip(L"https://wiki.filezilla-project.org/Date_and_Time_formatting");
	main->Add(link);

	auto cb = [this](wxCommandEvent const&) { SetCtrlState(); };
	impl_->date_system_->Bind(wxEVT_RADIOBUTTON, cb);
	impl_->date_iso_->Bind(wxEVT_RADIOBUTTON, cb);
	impl_->date_custom_->Bind(wxEVT_RADIOBUTTON, cb);
	impl_->time_system_->Bind(wxEVT_RADIOBUTTON, cb);
	impl_->time_iso_->Bind(wxEVT_RADIOBUTTON, cb);
	impl_->time_custom_->Bind(wxEVT_RADIOBUTTON, cb);

	return true;
}

bool COptionsPageDateFormatting::LoadPage()
{
	std::wstring const dateFormat = m_pOptions->get_string(OPTION_DATE_FORMAT);
	if (dateFormat == _T("1")) {
		impl_->date_iso_->SetValue(true);
	}
	else if (!dateFormat.empty() && dateFormat[0] == '2') {
		impl_->date_custom_->SetValue(true);
		impl_->date_format_->ChangeValue(dateFormat.substr(1));
	}
	else {
		impl_->date_system_->SetValue(true);
	}

	std::wstring const timeFormat = m_pOptions->get_string(OPTION_TIME_FORMAT);
	if (timeFormat == _T("1")) {
		impl_->time_iso_->SetValue(true);
	}
	else if (!timeFormat.empty() && timeFormat[0] == '2') {
		impl_->time_custom_->SetValue(true);
		impl_->time_format_->ChangeValue(timeFormat.substr(1));
	}
	else {
		impl_->time_system_->SetValue(true);
	}

	SetCtrlState();

	return true;
}

bool COptionsPageDateFormatting::SavePage()
{
	std::wstring dateFormat;
	if (impl_->date_custom_->GetValue()) {
		dateFormat = L"2" + impl_->date_format_->GetValue().ToStdWstring();
	}
	else if (impl_->date_iso_->GetValue()) {
		dateFormat = L"1";
	}
	else {
		dateFormat = L"0";
	}
	m_pOptions->set(OPTION_DATE_FORMAT, dateFormat);

	std::wstring timeFormat;
	if (impl_->time_custom_->GetValue()) {
		timeFormat = L"2" + impl_->time_format_->GetValue().ToStdWstring();
	}
	else if (impl_->time_iso_->GetValue()) {
		timeFormat = L"1";
	}
	else {
		timeFormat = L"0";
	}
	m_pOptions->set(OPTION_TIME_FORMAT, timeFormat);

	return true;
}

bool COptionsPageDateFormatting::Validate()
{
	if (impl_->date_custom_->GetValue()) {
		std::wstring const dateformat = impl_->date_format_->GetValue().ToStdWstring();
		if (dateformat.empty()) {
			return DisplayError(impl_->date_format_, _("Please enter a custom date format."));
		}
		if (!fz::datetime::verify_format(dateformat)) {
			return DisplayError(impl_->date_format_, _("The custom date format is invalid or contains unsupported format specifiers."));
		}
	}

	if (impl_->time_custom_->GetValue()) {
		std::wstring const timeformat = impl_->time_format_->GetValue().ToStdWstring();
		if (timeformat.empty()) {
			return DisplayError(impl_->time_format_, _("Please enter a custom time format."));
		}
		if (!fz::datetime::verify_format(timeformat)) {
			return DisplayError(impl_->time_format_, _("The custom time format is invalid or contains unsupported format specifiers."));
		}
	}

	return true;
}

void COptionsPageDateFormatting::SetCtrlState()
{
	impl_->date_format_->Enable(impl_->date_custom_->GetValue());
	impl_->time_format_->Enable(impl_->time_custom_->GetValue());
}
