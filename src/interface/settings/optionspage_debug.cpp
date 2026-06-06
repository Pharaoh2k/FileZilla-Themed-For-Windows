#include "../filezilla.h"
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_debug.h"

#include <wx/statbox.h>

struct COptionsPageDebug::impl final
{
	wxCheckBox* debugMenu_{};
	wxChoice* level_{};
	wxCheckBox* rawListing_{};
};

COptionsPageDebug::COptionsPageDebug()
	: impl_(std::make_unique<impl>())
{
}

COptionsPageDebug::~COptionsPageDebug()
{
}

bool COptionsPageDebug::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	main->AddGrowableRow(0);
	SetSizer(main);

	auto [box, inner] = lay.createStatBox(main, _("Debugging settings"), 1);
	
	impl_->debugMenu_ = new wxCheckBox(box, nullID, _("&Show debug menu"));
	inner->Add(impl_->debugMenu_);

	inner->AddSpacer(lay.gap);
	
	auto row = lay.createFlex(2);
	inner->Add(row);
	row->Add(new wxStaticText(box, nullID, _("&Debug information in message log:")), lay.valign);
	impl_->level_ = new wxChoice(box, nullID);
	row->Add(impl_->level_, lay.valign);

	impl_->level_->AppendString(_("0 - None"));
	impl_->level_->AppendString(_("1 - Warning"));
	impl_->level_->AppendString(_("2 - Info"));
	impl_->level_->AppendString(_("3 - Verbose"));
	impl_->level_->AppendString(_("4 - Debug"));

	inner->Add(new wxStaticText(box, nullID, _("The higher the debug level, the more information will be displayed in the message log. Displaying debug information has a negative impact on performance.")));
	inner->Add(new wxStaticText(box, nullID, _("If reporting bugs, please provide logs with \"Verbose\" logging level.")));

	inner->AddSpacer(lay.gap);

	impl_->rawListing_ = new wxCheckBox(box, nullID, _("Show &raw directory listing"));
	inner->Add(impl_->rawListing_);

	return true;
}

bool COptionsPageDebug::LoadPage()
{
	impl_->debugMenu_->SetValue(m_pOptions->get_bool(OPTION_DEBUG_MENU));
	impl_->level_->SetSelection(m_pOptions->get_int(OPTION_LOGGING_DEBUGLEVEL));
	impl_->rawListing_->SetValue(m_pOptions->get_bool(OPTION_LOGGING_RAWLISTING));
	return true;
}

bool COptionsPageDebug::SavePage()
{
	m_pOptions->set(OPTION_DEBUG_MENU, impl_->debugMenu_->GetValue());
	m_pOptions->set(OPTION_LOGGING_DEBUGLEVEL, impl_->level_->GetSelection());
	m_pOptions->set(OPTION_LOGGING_RAWLISTING, impl_->rawListing_->GetValue());

	return true;
}
