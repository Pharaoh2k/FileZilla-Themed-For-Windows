#include "../filezilla.h"
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_fileexists.h"

#include <wx/statbox.h>

struct COptionsPageFileExists::impl final
{
	wxChoice* download_{};
	wxChoice* upload_{};
	wxCheckBox* ascii_resume_{};
};

COptionsPageFileExists::COptionsPageFileExists()
	: impl_(std::make_unique<impl>())
{
}

COptionsPageFileExists::~COptionsPageFileExists()
{
}

bool COptionsPageFileExists::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	SetSizer(main);

	main->Add(new wxStaticText(this, nullID, _("Select default action to perform if target file of a transfer already exists.")));

	{
		auto [box, inner] = lay.createStatBox(main, _("Default file exists action"), 2);
		inner->Add(new wxStaticText(box, nullID, _("&Downloads:")), lay.valign);
		impl_->download_ = new wxChoice(box, nullID);
		inner->Add(impl_->download_, lay.valign);
		inner->Add(new wxStaticText(box, nullID, _("&Uploads:")), lay.valign);
		impl_->upload_ = new wxChoice(box, nullID);
		inner->Add(impl_->upload_, lay.valign);

		auto actions = [](wxChoice* c) {
			c->AppendString(_("Ask for action"));
			c->AppendString(_("Overwrite file"));
			c->AppendString(_("Overwrite file if source file newer"));
			c->AppendString(_("Overwrite file if size differs"));
			c->AppendString(_("Overwrite file if size differs or source file is newer"));
			c->AppendString(_("Resume file transfer"));
			c->AppendString(_("Rename file"));
			c->AppendString(_("Skip file"));
		};
		actions(impl_->download_);
		actions(impl_->upload_);
	}
	
	main->Add(new wxStaticText(this, nullID, _("If using 'overwrite if newer', your system time has to be synchronized with the server. If the time differs (e.g. different timezone), specify a time offset in the site manager.")));

	impl_->ascii_resume_ = new wxCheckBox(this, nullID, _("A&llow resuming of ASCII files"));
	main->Add(impl_->ascii_resume_);
	main->Add(new wxStaticText(this, nullID, _("Resuming ASCII files can cause problems if server uses a different line ending format than the client.")), 0, lay.indent, wxLEFT);

	return true;
}

bool COptionsPageFileExists::LoadPage()
{
	impl_->download_->SetSelection(m_pOptions->get_int(OPTION_FILEEXISTS_DOWNLOAD));
	impl_->upload_->SetSelection(m_pOptions->get_int(OPTION_FILEEXISTS_UPLOAD));
	impl_->ascii_resume_->SetValue(m_pOptions->get_bool(OPTION_ASCIIRESUME));
	return true;
}

bool COptionsPageFileExists::SavePage()
{
	m_pOptions->set(OPTION_FILEEXISTS_DOWNLOAD, impl_->download_->GetSelection());
	m_pOptions->set(OPTION_FILEEXISTS_UPLOAD, impl_->upload_->GetSelection());
	m_pOptions->set(OPTION_ASCIIRESUME, impl_->ascii_resume_->GetValue());
	return true;
}
