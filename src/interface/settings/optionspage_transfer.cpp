#include "../filezilla.h"
#include "../Options.h"
#include "../../include/sizeformatting.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_transfer.h"
#include "../textctrlex.h"
#include "../wxext/spinctrlex.h"

#include <wx/statbox.h>

struct COptionsPageTransfer::impl final
{
	wxSpinCtrlEx* transfers_{};
	wxSpinCtrlEx* downloads_{};
	wxSpinCtrlEx* uploads_{};

	wxChoice* burst_tolerance_{};

	wxCheckBox* limit_{};
	wxTextCtrlEx* dllimit_{};
	wxTextCtrlEx* ullimit_{};

	wxTextCtrlEx* replace_{};

	wxCheckBox* preallocate_{};
};

COptionsPageTransfer::COptionsPageTransfer()
	: impl_(std::make_unique<impl>())
{
}

COptionsPageTransfer::~COptionsPageTransfer()
{
}

bool COptionsPageTransfer::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	SetSizer(main);

	{
		auto [box, inner] = lay.createStatBox(main, _("Concurrent transfers"), 3);
		inner->Add(new wxStaticText(box, nullID, _("Maximum simultaneous &transfers:")), lay.valign);
		impl_->transfers_ = new wxSpinCtrlEx(box, nullID, wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(26), -1));
		impl_->transfers_->SetRange(1, 10);
		impl_->transfers_->SetMaxLength(2);
		inner->Add(impl_->transfers_, lay.valign);
		inner->Add(new wxStaticText(box, nullID, _("(1-10)")), lay.valign);
		inner->Add(new wxStaticText(box, nullID, _("Limit for concurrent &downloads:")), lay.valign);
		impl_->downloads_ = new wxSpinCtrlEx(box, nullID, wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(26), -1));
		impl_->downloads_->SetRange(0, 10);
		impl_->downloads_->SetMaxLength(2);
		inner->Add(impl_->downloads_, lay.valign);
		inner->Add(new wxStaticText(box, nullID, _("(0 for no limit)")), lay.valign);
		inner->Add(new wxStaticText(box, nullID, _("Limit for concurrent &uploads:")), lay.valign);
		impl_->uploads_ = new wxSpinCtrlEx(box, nullID, wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(26), -1));
		impl_->uploads_->SetRange(0, 10);
		impl_->uploads_->SetMaxLength(2);
		inner->Add(impl_->uploads_, lay.valign);
		inner->Add(new wxStaticText(box, nullID, _("(0 for no limit)")), lay.valign);
	}

	{
		auto [box, inner] = lay.createStatBox(main, _("Speed limits"), 1);

		impl_->limit_ = new wxCheckBox(box, nullID, _("&Enable speed limits"));
		inner->Add(impl_->limit_);

		auto innermost = lay.createFlex(2);
		inner->Add(innermost);
		innermost->Add(new wxStaticText(box, nullID, _("Download &limit:")), lay.valign);
		auto row = lay.createFlex(2);
		innermost->Add(row, lay.valign);
		impl_->dllimit_ = new wxTextCtrlEx(box, nullID, wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(40), -1));
		impl_->dllimit_->SetMaxLength(9);
		row->Add(impl_->dllimit_, lay.valign);
		row->Add(new wxStaticText(box, nullID, wxString::Format(_("(in %s/s)"), SizeFormatter(*m_pOptions).GetUnitSymbol(UnitPrefix::kilo, 1024))), lay.valign);

		innermost->Add(new wxStaticText(box, nullID, _("Upload &limit:")), lay.valign);
		row = lay.createFlex(2);
		innermost->Add(row, lay.valign);
		impl_->ullimit_ = new wxTextCtrlEx(box, nullID, wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(40), -1));
		impl_->ullimit_->SetMaxLength(9);
		row->Add(impl_->ullimit_, lay.valign);
		row->Add(new wxStaticText(box, nullID, wxString::Format(_("(in %s/s)"), SizeFormatter(*m_pOptions).GetUnitSymbol(UnitPrefix::kilo, 1024))), lay.valign);

		innermost->Add(new wxStaticText(box, nullID, _("&Burst tolerance:")), lay.valign);
		impl_->burst_tolerance_ = new wxChoice(box, nullID);
		impl_->burst_tolerance_->AppendString(_("Normal"));
		impl_->burst_tolerance_->AppendString(_("High"));
		impl_->burst_tolerance_->AppendString(_("Very high"));
		impl_->burst_tolerance_->Select(0);
		innermost->Add(impl_->burst_tolerance_, lay.valign);

		impl_->limit_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent const& ev) {
			impl_->dllimit_->Enable(ev.IsChecked());
			impl_->ullimit_->Enable(ev.IsChecked());
			impl_->burst_tolerance_->Enable(ev.IsChecked());
		});
	}

	{
		auto [box, inner] = lay.createStatBox(main, _("Filter invalid characters in filenames"), 1);
		inner->Add(new wxStaticText(box, nullID, _("Characters that are not supported by the local operating system in filenames are replaced if downloading such a file.")));
		auto innermost = lay.createFlex(2);
		inner->Add(innermost);
		innermost->Add(new wxStaticText(box, nullID, _("&Replace invalid characters with:")), lay.valign);
		impl_->replace_ = new wxTextCtrlEx(box, nullID, wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(10), -1));
		impl_->replace_->SetMaxLength(1);
		innermost->Add(impl_->replace_, lay.valign);
#ifdef __WXMSW__
		wxString invalid = _T("\\ / : * ? \" < > |");
		wxString filtered = wxString::Format(_("The following characters will be replaced: %s"), invalid);
#else
		wxString invalid = _T("/");
		wxString filtered = wxString::Format(_("The following character will be replaced: %s"), invalid);
#endif
		inner->Add(new wxStaticText(box, nullID, filtered));
	}

	{
		auto [box, inner] = lay.createStatBox(main, _("Preallocation"), 1);
		impl_->preallocate_ = new wxCheckBox(box, nullID, _("Pre&allocate space before downloading"));
		inner->Add(impl_->preallocate_);
	}

	GetSizer()->Fit(this);

	return true;
}

bool COptionsPageTransfer::LoadPage()
{
	bool const enable_speedlimits = m_pOptions->get_int(OPTION_SPEEDLIMIT_ENABLE) != 0;
	impl_->limit_->SetValue(enable_speedlimits);

	impl_->dllimit_->ChangeValue(m_pOptions->get_string(OPTION_SPEEDLIMIT_INBOUND));
	impl_->dllimit_->Enable(enable_speedlimits);

	impl_->ullimit_->ChangeValue(m_pOptions->get_string(OPTION_SPEEDLIMIT_OUTBOUND));
	impl_->ullimit_->Enable(enable_speedlimits);

	impl_->transfers_->SetValue(m_pOptions->get_int(OPTION_NUMTRANSFERS));
	impl_->downloads_->SetValue(m_pOptions->get_int(OPTION_CONCURRENTDOWNLOADLIMIT));
	impl_->uploads_->SetValue(m_pOptions->get_int(OPTION_CONCURRENTUPLOADLIMIT));

	impl_->burst_tolerance_->SetSelection(m_pOptions->get_int(OPTION_SPEEDLIMIT_BURSTTOLERANCE));
	impl_->burst_tolerance_->Enable(enable_speedlimits);

	impl_->replace_->ChangeValue(m_pOptions->get_string(OPTION_INVALID_CHAR_REPLACE));

	impl_->preallocate_->SetValue(m_pOptions->get_bool(OPTION_PREALLOCATE_SPACE));

	return true;
}

bool COptionsPageTransfer::SavePage()
{
	m_pOptions->set(OPTION_SPEEDLIMIT_ENABLE, impl_->limit_->GetValue());

	m_pOptions->set(OPTION_NUMTRANSFERS, impl_->transfers_->GetValue());
	m_pOptions->set(OPTION_CONCURRENTDOWNLOADLIMIT,	impl_->downloads_->GetValue());
	m_pOptions->set(OPTION_CONCURRENTUPLOADLIMIT, impl_->uploads_->GetValue());

	m_pOptions->set(OPTION_SPEEDLIMIT_INBOUND, impl_->dllimit_->GetValue().ToStdWstring());
	m_pOptions->set(OPTION_SPEEDLIMIT_OUTBOUND, impl_->ullimit_->GetValue().ToStdWstring());
	m_pOptions->set(OPTION_SPEEDLIMIT_BURSTTOLERANCE, impl_->burst_tolerance_->GetSelection());
	m_pOptions->set(OPTION_INVALID_CHAR_REPLACE, impl_->replace_->GetValue().ToStdWstring());
	m_pOptions->set(OPTION_PREALLOCATE_SPACE, impl_->preallocate_->GetValue());

	return true;
}

bool COptionsPageTransfer::Validate()
{
	if (impl_->transfers_->GetValue() < 1 || impl_->transfers_->GetValue() > 10) {
		return DisplayError(impl_->transfers_, _("Please enter a number between 1 and 10 for the number of concurrent transfers."));
	}

	if (impl_->downloads_->GetValue() < 0 || impl_->downloads_->GetValue() > 10) {
		return DisplayError(impl_->downloads_, _("Please enter a number between 0 and 10 for the number of concurrent downloads."));
	}

	if (impl_->uploads_->GetValue() < 0 || impl_->uploads_->GetValue() > 10) {
		return DisplayError(impl_->uploads_, _("Please enter a number between 0 and 10 for the number of concurrent uploads."));
	}

	if (fz::to_integral<int>(impl_->dllimit_->GetValue().ToStdWstring(), -1) < 0) {
		const wxString unit = SizeFormatter(*m_pOptions).GetUnitSymbol(UnitPrefix::kilo, 1024);
		return DisplayError(impl_->dllimit_, wxString::Format(_("Please enter a download speed limit greater or equal to 0 %s/s."), unit));
	}

	if (fz::to_integral<int>(impl_->ullimit_->GetValue().ToStdWstring(), -1) < 0) {
		wxString const unit = SizeFormatter(*m_pOptions).GetUnitSymbol(UnitPrefix::kilo, 1024);
		return DisplayError(impl_->ullimit_, wxString::Format(_("Please enter an upload speed limit greater or equal to 0 %s/s."), unit));
	}

	std::wstring replace = impl_->replace_->GetValue().ToStdWstring();
#ifdef __WXMSW__
	if (replace == _T("\\") ||
		replace == _T("/") ||
		replace == _T(":") ||
		replace == _T("*") ||
		replace == _T("?") ||
		replace == _T("\"") ||
		replace == _T("<") ||
		replace == _T(">") ||
		replace == _T("|"))
#else
	if (replace == _T("/"))
#endif
	{
		return DisplayError(impl_->replace_, _("You cannot replace an invalid character with another invalid character. Please enter a character that is allowed in filenames."));
	}

	return true;
}
