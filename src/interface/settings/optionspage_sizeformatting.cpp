#include "../filezilla.h"

#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_sizeformatting.h"

#include "../wxext/spinctrlex.h"

#include <wx/statbox.h>

struct COptionsPageSizeFormatting::impl
{
	wxRadioButton* bytes_{};
	wxRadioButton* iec_{};
	wxRadioButton* binary_{};
	wxRadioButton* decimal_{};

	wxCheckBox* tsep_{};

	wxSpinCtrlEx* places_{};

	wxStaticText* examples_[6]{};
};

COptionsPageSizeFormatting::COptionsPageSizeFormatting()
	: impl_(std::make_unique<impl>())
{
}

COptionsPageSizeFormatting::~COptionsPageSizeFormatting()
{
}

bool COptionsPageSizeFormatting::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	SetSizer(main);

	{
		auto [box, inner] = lay.createStatBox(main, _("Size formatting"), 1);
		impl_->bytes_ = new wxRadioButton(box, nullID, _("&Display size in bytes"));
		impl_->iec_ = new wxRadioButton(box, nullID, _("&IEC binary prefixes (e.g. 1 KiB = 1024 bytes)"));
		impl_->binary_ = new wxRadioButton(box, nullID, _("&Binary prefixes using SI symbols. (e.g. 1 KB = 1024 bytes)"));
		impl_->decimal_ = new wxRadioButton(box, nullID, _("D&ecimal prefixes using SI symbols (e.g. 1 KB = 1000 bytes)"));
		inner->Add(impl_->bytes_, wxRB_GROUP);
		inner->Add(impl_->iec_);
		inner->Add(impl_->binary_);
		inner->Add(impl_->decimal_);
		impl_->bytes_->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&){ UpdateControls(); });
		impl_->iec_->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) { UpdateControls(); });
		impl_->binary_->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) { UpdateControls(); });
		impl_->decimal_->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) { UpdateControls(); });
		impl_->tsep_ = new wxCheckBox(box, nullID, _("&Use thousands separator"));
		inner->Add(impl_->tsep_);
		impl_->tsep_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { UpdateControls(); });

		auto row = lay.createFlex(2);
		inner->Add(row);
		row->Add(new wxStaticText(box, nullID, _("Number of decimal places:")), lay.valign);
		impl_->places_ = new wxSpinCtrlEx(box, nullID, wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(30), -1));
		impl_->places_->SetRange(0, 3);
		impl_->places_->SetMaxLength(1);
		row->Add(impl_->places_, lay.valign);
		impl_->places_->Bind(wxEVT_SPINCTRL, [this](wxCommandEvent&) { UpdateControls(); });
	}

	{
		auto [box, inner] = lay.createStatBox(main, _("Examples"), 1);
		for (size_t i = 0; i < 6; ++i) {
			impl_->examples_[i] = new wxStaticText(box, nullID, wxString());
			inner->Add(impl_->examples_[i], lay.ralign);
		}
	}

	return true;
}

bool COptionsPageSizeFormatting::LoadPage()
{
	bool failure = false;

	const int format = m_pOptions->get_int(OPTION_SIZE_FORMAT);
	switch (format)
	{
	case 1:
		impl_->iec_->SetValue(true);
		break;
	case 2:
		impl_->binary_->SetValue(true);
		break;
	case 3:
		impl_->decimal_->SetValue(true);
		break;
	default:
		impl_->bytes_->SetValue(true);
		break;
	}

	impl_->tsep_->SetValue(m_pOptions->get_bool(OPTION_SIZE_USETHOUSANDSEP));
	impl_->places_->SetValue(m_pOptions->get_int(OPTION_SIZE_DECIMALPLACES));

	UpdateControls();

	return !failure;
}

bool COptionsPageSizeFormatting::SavePage()
{
	m_pOptions->set(OPTION_SIZE_FORMAT, static_cast<int>(GetFormat()));

	m_pOptions->set(OPTION_SIZE_USETHOUSANDSEP, impl_->tsep_->GetValue());
	m_pOptions->set(OPTION_SIZE_DECIMALPLACES, impl_->places_->GetValue());

	return true;
}

SizeFormat COptionsPageSizeFormatting::GetFormat() const
{
	if (impl_->iec_->GetValue()) {
		return SizeFormat::iec;
	}
	else if (impl_->binary_->GetValue()) {
		return SizeFormat::si1024;
	}
	else if (impl_->decimal_->GetValue()) {
		return SizeFormat::si1000;
	}

	return SizeFormat::bytes;
}

bool COptionsPageSizeFormatting::Validate()
{
	return true;
}

void COptionsPageSizeFormatting::UpdateControls()
{
	SizeFormat format = GetFormat();
	impl_->places_->Enable(format != SizeFormat::bytes);

	impl_->examples_[0]->SetLabel(FormatSize(12));
	impl_->examples_[1]->SetLabel(FormatSize(100));
	impl_->examples_[2]->SetLabel(FormatSize(1234));
	impl_->examples_[3]->SetLabel(FormatSize(1058817));
	impl_->examples_[4]->SetLabel(FormatSize(123456789));
	impl_->examples_[5]->SetLabel(FormatSize(0x39E94F995A72ll));

	GetSizer()->Layout();

	// Otherwise label background isn't cleared properly
	Refresh();
}

wxString COptionsPageSizeFormatting::FormatSize(int64_t size)
{
	SizeFormat format = GetFormat();
	const bool thousands_separator = impl_->tsep_->GetValue();
	const int num_decimal_places = impl_->places_->GetValue();

	return SizeFormatter::Format(size, false, format,thousands_separator, num_decimal_places);
}
