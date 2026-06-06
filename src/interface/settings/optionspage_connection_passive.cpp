#include "../filezilla.h"
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_connection_passive.h"

#include <wx/statbox.h>

struct COptionsPageConnectionPassive::impl final
{
	wxRadioButton* fallback_external_{};
	wxRadioButton* fallback_active_{};
};

COptionsPageConnectionPassive::COptionsPageConnectionPassive()
	: impl_(std::make_unique<impl>())
{
}

COptionsPageConnectionPassive::~COptionsPageConnectionPassive() = default;

bool COptionsPageConnectionPassive::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	SetSizer(main);

	{
		auto [box, inner] = lay.createStatBox(main, _("Passive mode"), 1);

		inner->Add(new wxStaticText(box, nullID, _("Some misconfigured remote servers which are behind a router, may reply with their local IP address.")));

		impl_->fallback_external_ = new wxRadioButton(box, nullID, _("&Use the server's external IP address instead"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		inner->Add(impl_->fallback_external_);

		impl_->fallback_active_ = new wxRadioButton(box, nullID, _("&Fall back to active mode"));
		inner->Add(impl_->fallback_active_);
	}

	return true;
}

bool COptionsPageConnectionPassive::LoadPage()
{
	const int pasv_fallback = m_pOptions->get_int(OPTION_PASVREPLYFALLBACKMODE);
	impl_->fallback_external_->SetValue(pasv_fallback == 0);
	impl_->fallback_active_->SetValue(pasv_fallback == 1);
	return true;
}

bool COptionsPageConnectionPassive::SavePage()
{
	m_pOptions->set(OPTION_PASVREPLYFALLBACKMODE, impl_->fallback_external_->GetValue() ? 0 : 1);

	return true;
}
