#include "../filezilla.h"
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage_interface.h"
#include "../Mainfrm.h"
#include "../power_management.h"
#include <libfilezilla/util.hpp>

#include <wx/statbox.h>

struct COptionsPageInterface::impl
{
	wxChoice* filepane_layout_{};
	wxChoice* messagelog_pos_{};
	wxCheckBox* swap_{};

#ifndef __WXMAC__
	wxCheckBox* minimize_tray_{};
#endif
	wxCheckBox* no_idle_sleep_{};

	wxRadioButton* startup_normal_{};
	wxRadioButton* startup_sm_{};
	wxRadioButton* startup_restore_{};

	wxChoice* newconn_action_{};

	wxCheckBox* momentary_speed_{};
	wxCheckBox* ropListRefresh_{};
};

COptionsPageInterface::COptionsPageInterface()
	: impl_(std::make_unique<impl>())
{}

COptionsPageInterface::~COptionsPageInterface()
{
}

bool COptionsPageInterface::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	SetSizer(main);

	{
		auto [box, inner] = lay.createStatBox(main, _("Layout"), 1);

		auto rows = lay.createFlex(2);
		inner->Add(rows);
		rows->Add(new wxStaticText(box, nullID, _("&Layout of file and directory panes:")), lay.valign);
		impl_->filepane_layout_ = new wxChoice(box, nullID);
		impl_->filepane_layout_->Append(_("Classic"));
		impl_->filepane_layout_->Append(_("Explorer"));
		impl_->filepane_layout_->Append(_("Widescreen"));
		impl_->filepane_layout_->Append(_("Blackboard"));
		rows->Add(impl_->filepane_layout_, lay.valign);
		rows->Add(new wxStaticText(box, nullID, _("Message log positio&n:")), lay.valign);
		impl_->messagelog_pos_ = new wxChoice(box, nullID);
		impl_->messagelog_pos_->Append(_("Above the file lists"));
		impl_->messagelog_pos_->Append(_("Next to the transfer queue"));
		impl_->messagelog_pos_->Append(_("As tab in the transfer queue pane"));
		rows->Add(impl_->messagelog_pos_, lay.valign);
		impl_->swap_ = new wxCheckBox(box, nullID, _("&Swap local and remote panes"));
		inner->Add(impl_->swap_);

		impl_->filepane_layout_->Bind(wxEVT_CHOICE, &COptionsPageInterface::OnLayoutChange, this);
		impl_->messagelog_pos_->Bind(wxEVT_CHOICE, &COptionsPageInterface::OnLayoutChange, this);
		impl_->swap_->Bind(wxEVT_CHECKBOX, &COptionsPageInterface::OnLayoutChange, this);
	}

	{
		auto [box, inner] = lay.createStatBox(main, _("Behaviour"), 1);

#ifndef __WXMAC__
		impl_->minimize_tray_ = new wxCheckBox(box, nullID, _("&Minimize to tray"));
		inner->Add(impl_->minimize_tray_);
#endif
		if (CPowerManagement::IsSupported()) {
			impl_->no_idle_sleep_ = new wxCheckBox(box, nullID, _("P&revent system from entering idle sleep during transfers and other operations"));
			inner->Add(impl_->no_idle_sleep_);
		}
		if (inner->GetItemCount()) {
			inner->AddSpacer(0);
		}
		inner->Add(new wxStaticText(box, nullID, _("On startup of FileZilla:")));
		impl_->startup_normal_ = new wxRadioButton(box, nullID, _("S&tart normally"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		inner->Add(impl_->startup_normal_);
		impl_->startup_sm_ = new wxRadioButton(box, nullID, _("S&how the Site Manager on startup"));
		inner->Add(impl_->startup_sm_);
		impl_->startup_restore_ = new wxRadioButton(box, nullID, _("Restore ta&bs and reconnect"));
		inner->Add(impl_->startup_restore_);
		inner->AddSpacer(0);
		inner->Add(new wxStaticText(box, nullID, _("When st&arting a new connection while already connected:")));
		impl_->newconn_action_ = new wxChoice(box, nullID);
		impl_->newconn_action_->Append(_("Ask for action"));
		impl_->newconn_action_->Append(_("Connect in new tab"));
		impl_->newconn_action_->Append(_("Connect in current tab"));
		inner->Add(impl_->newconn_action_);

		inner->AddSpacer(0);
		impl_->ropListRefresh_ = new wxCheckBox(box, nullID, _("&Force refresh of directory listings during remote recursive operations"));
		inner->Add(impl_->ropListRefresh_);
	}

	{
		auto [box, inner] = lay.createStatBox(main, _("Transfer Queue"), 1);
		impl_->momentary_speed_ = new wxCheckBox(box, nullID, _("&Display momentary transfer speed instead of average speed"));
		inner->Add(impl_->momentary_speed_);
	}

	return true;
}

bool COptionsPageInterface::LoadPage()
{
	impl_->filepane_layout_->SetSelection(m_pOptions->get_int(OPTION_FILEPANE_LAYOUT));
	impl_->messagelog_pos_->SetSelection(m_pOptions->get_int(OPTION_MESSAGELOG_POSITION));
	impl_->swap_->SetValue(m_pOptions->get_bool(OPTION_FILEPANE_SWAP));

#ifndef __WXMAC__
	impl_->minimize_tray_->SetValue(m_pOptions->get_bool(OPTION_MINIMIZE_TRAY));
#endif
	if (CPowerManagement::IsSupported()) {
		impl_->no_idle_sleep_->SetValue(m_pOptions->get_bool(OPTION_PREVENT_IDLESLEEP));
	}

	impl_->momentary_speed_->SetValue(m_pOptions->get_bool(OPTION_SPEED_DISPLAY));

	int const startupAction = m_pOptions->get_int(OPTION_STARTUP_ACTION);
	switch (startupAction) {
	default:
		impl_->startup_normal_->SetValue(true);
		break;
	case 1:
		impl_->startup_sm_->SetValue(true);
		break;
	case 2:
		impl_->startup_restore_->SetValue(true);
		break;
	}

	int action = m_pOptions->get_int(OPTION_ALREADYCONNECTED_CHOICE);
	if (action & 2) {
		action = 1 + (action & 1);
	}
	else {
		action = 0;
	}
	impl_->newconn_action_->SetSelection(action);

	m_pOwner->RememberOldValue(OPTION_MESSAGELOG_POSITION);
	m_pOwner->RememberOldValue(OPTION_FILEPANE_LAYOUT);
	m_pOwner->RememberOldValue(OPTION_FILEPANE_SWAP);

	impl_->ropListRefresh_->SetValue(m_pOptions->get_bool(OPTION_REMOTE_ROP_LISTING_REFRESH));

	return true;
}

bool COptionsPageInterface::SavePage()
{
	m_pOptions->set(OPTION_FILEPANE_LAYOUT, impl_->filepane_layout_->GetSelection());
	m_pOptions->set(OPTION_MESSAGELOG_POSITION, impl_->messagelog_pos_->GetSelection());
	m_pOptions->set(OPTION_FILEPANE_SWAP, impl_->swap_->GetValue());

#ifndef __WXMAC__
	m_pOptions->set(OPTION_MINIMIZE_TRAY, impl_->minimize_tray_->GetValue());
#endif

	if (CPowerManagement::IsSupported()) {
		m_pOptions->set(OPTION_PREVENT_IDLESLEEP, impl_->no_idle_sleep_->GetValue());
	}

	m_pOptions->set(OPTION_SPEED_DISPLAY, impl_->momentary_speed_->GetValue());

	int startupAction = 0;
	if (impl_->startup_sm_->GetValue()) {
		startupAction = 1;
	}
	else if (impl_->startup_restore_->GetValue()) {
		startupAction = 2;
	}
	m_pOptions->set(OPTION_STARTUP_ACTION, startupAction);

	int action = impl_->newconn_action_->GetSelection();
	if (!action) {
		action = m_pOptions->get_int(OPTION_ALREADYCONNECTED_CHOICE) & 1;
	}
	else {
		action += 1;
	}
	m_pOptions->set(OPTION_ALREADYCONNECTED_CHOICE, action);
	m_pOptions->set(OPTION_REMOTE_ROP_LISTING_REFRESH, impl_->ropListRefresh_->GetValue());

	return true;
}

void COptionsPageInterface::OnLayoutChange(wxCommandEvent&)
{
	m_pOptions->set(OPTION_FILEPANE_LAYOUT, impl_->filepane_layout_->GetSelection());
	m_pOptions->set(OPTION_MESSAGELOG_POSITION, impl_->messagelog_pos_->GetSelection());
	m_pOptions->set(OPTION_FILEPANE_SWAP, impl_->swap_->GetValue());
}

