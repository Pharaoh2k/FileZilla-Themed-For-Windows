#include "../filezilla.h"

#include "optionspage_updatecheck.h"

#if FZ_MANUALUPDATECHECK && FZ_AUTOUPDATECHECK

#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "../update_dialog.h"

#include <wx/hyperlink.h>
#include <wx/statbox.h>

struct COptionsPageUpdateCheck::impl final
{
	wxChoice* interval_{};
	wxChoice* type_{};
};

COptionsPageUpdateCheck::COptionsPageUpdateCheck()
	: impl_(std::make_unique<impl>())
{
}

COptionsPageUpdateCheck::~COptionsPageUpdateCheck()
{
}

bool COptionsPageUpdateCheck::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	main->AddGrowableRow(0);
	SetSizer(main);

	auto [box, inner] = lay.createStatBox(main, _("FileZilla updates"), 1);
	inner->AddGrowableRow(6);

	inner->Add(new wxStaticText(box, nullID, _("Check for FileZilla &updates automatically:")));
	impl_->interval_ = new wxChoice(box, nullID);
	inner->Add(impl_->interval_);

	impl_->interval_->AppendString(_("Never"));
	impl_->interval_->AppendString(_("Once a day"));
	impl_->interval_->AppendString(_("Once a week"));

	inner->AddSpacer(lay.gap);

	inner->Add(new wxStaticText(box, nullID, _("&When checking for updates, check for:")));
	impl_->type_ = new wxChoice(box, nullID);
	inner->Add(impl_->type_);

	impl_->type_->AppendString(_("Stable versions only"));
	impl_->type_->AppendString(_("Stable and beta versions"));
	impl_->type_->AppendString(_("Stable, beta and nightly versions"));

	inner->Add(new wxStaticText(box, nullID, _("Advice: Unless you want to test new features, please keep using stable versions only. Beta versions and nightly builds are development versions meant for testing purposes. Nightly builds of FileZilla may not work as expected and might even damage your system. Use beta versions and nightly builds at your own risk.")));
	inner->AddStretchSpacer();
	auto run = new wxButton(box, nullID, _("&Run update check now..."));
	inner->Add(run, lay.halign);
	run->Bind(wxEVT_BUTTON, &COptionsPageUpdateCheck::OnRunUpdateCheck, this);

	inner->AddSpacer(0);

	inner->Add(new wxStaticText(box, nullID, _("To check for updates, information such as the version of FileZilla, your operating system and your CPU architecture needs to be submitted. Only necessary data is submitted. It is collected in anonymized, aggregate form only.")));
	inner->Add(new wxHyperlinkCtrl(box, nullID, _("Privacy policy"), L"https://filezilla-project.org/privacy.php"));

	return true;
}

bool COptionsPageUpdateCheck::LoadPage()
{
	int sel{};
	if (m_pOptions->get_int(OPTION_UPDATECHECK)) {
		int days = m_pOptions->get_int(OPTION_UPDATECHECK_INTERVAL);
		if (days < 7) {
			sel = 1;
		}
		else {
			sel = 2;
		}
	}
	impl_->interval_->SetSelection(sel);

	int type = m_pOptions->get_int(OPTION_UPDATECHECK_CHECKBETA);
	if (type < 0 || type > 2) {
		type = 1;
	}
	impl_->type_->SetSelection(type);

	return true;
}

bool COptionsPageUpdateCheck::Validate()
{
	int type = impl_->type_->GetSelection();
	if (type == 2 && m_pOptions->get_int(OPTION_UPDATECHECK_CHECKBETA) != 2) {
		if (wxMessageBoxEx(_("Warning, use nightly builds at your own risk.\nNo support is given for nightly builds.\nNightly builds may not work as expected and might even damage your system.\n\nDo you really want to check for nightly builds?"), _("Updates"), wxICON_EXCLAMATION | wxYES_NO, this) != wxYES) {
			impl_->type_->SetSelection(m_pOptions->get_int(OPTION_UPDATECHECK_CHECKBETA));
		}
	}
	return true;
}

bool COptionsPageUpdateCheck::SavePage()
{
	int sel = impl_->interval_->GetSelection();
	m_pOptions->set(OPTION_UPDATECHECK, (sel > 0) ? 1 : 0);
	int days = 0;
	switch (sel)
	{
	case 1:
		days = 1;
		break;
	case 2:
		days = 7;
		break;
	default:
		days = 0;
		break;
	}
	m_pOptions->set(OPTION_UPDATECHECK_INTERVAL, days);

	int type = impl_->type_->GetSelection();
	if (type < 0 || type > 2) {
		type = 1;
	}
	m_pOptions->set(OPTION_UPDATECHECK_CHECKBETA, type);

	return true;
}

void COptionsPageUpdateCheck::OnRunUpdateCheck(wxCommandEvent &)
{
	if (!Validate() || !SavePage()) {
		return;
	}

	CUpdater* updater = CUpdater::GetInstance();
	if (updater) {
		updater->Run(true);
		CUpdateDialog dlg(this, *updater, *m_pOptions);
		dlg.ShowModal();
	}
}

#endif
