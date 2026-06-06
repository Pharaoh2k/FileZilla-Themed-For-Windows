#include "filezilla.h"
#include "defaultfileexistsdlg.h"

#include <wx/statbox.h>

CFileExistsNotification::OverwriteAction CDefaultFileExistsDlg::m_defaults[2] = {CFileExistsNotification::unknown, CFileExistsNotification::unknown};

struct CDefaultFileExistsDlg::impl final
{
	wxChoice* downloadAction_{};
	wxChoice* uploadAction_{};
};

CDefaultFileExistsDlg::CDefaultFileExistsDlg()
	: impl_(std::make_unique<impl>())
{
}

CDefaultFileExistsDlg::~CDefaultFileExistsDlg()
{
}

bool CDefaultFileExistsDlg::Load(wxWindow *parent, bool fromQueue, bool local, bool remote)
{
	if (!Create(parent, nullID, _("Default file exists action"))) {
		return false;
	}

	auto & lay = layout();
	auto main = lay.createMain(this, 1);

	if (fromQueue) {
		main->Add(new wxStaticText(this, nullID, _("Select default file exists action only for the currently selected files in the queue.")));
	}
	else {
		main->Add(new wxStaticText(this, nullID, _("Select default file exists action if the target file already exists. This selection is valid only for the current session.")));
	}

	{
		auto [box, inner] = lay.createStatBox(main, _("Default file exists action"), 2);
		inner->AddGrowableCol(1);

		auto actions = [](wxChoice* c) {
			c->AppendString(_("Use default action"));
			c->AppendString(_("Ask for action"));
			c->AppendString(_("Overwrite file"));
			c->AppendString(_("Overwrite file if source file newer"));
			c->AppendString(_("Overwrite file if size differs"));
			c->AppendString(_("Overwrite file if size differs or source file is newer"));
			c->AppendString(_("Resume file transfer"));
			c->AppendString(_("Rename file"));
			c->AppendString(_("Skip file"));
			c->Select(0);
		};
		if (local) {
			inner->Add(new wxStaticText(box, nullID, _("&Downloads:")), lay.valign);
			impl_->downloadAction_ = new wxChoice(box, nullID);
			inner->Add(impl_->downloadAction_, lay.valigng);
			actions(impl_->downloadAction_);
		}
		if (remote) {
			inner->Add(new wxStaticText(box, nullID, _("&Uploads:")), lay.valign);
			impl_->uploadAction_ = new wxChoice(box, nullID);
			inner->Add(impl_->uploadAction_, lay.valigng);
			actions(impl_->uploadAction_);
		}
	}

	main->Add(new wxStaticText(this, nullID, _("If using 'overwrite if newer', your system time has to be synchronized with the server. If the time differs (e.g. different timezone), specify a time offset in the site manager.")));

	auto buttons = lay.createButtonSizer(this, main, true);

	auto ok = new wxButton(this, wxID_OK, _("&OK"));
	ok->SetDefault();
	buttons->AddButton(ok);

	auto cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
	buttons->AddButton(cancel);

	buttons->Realize();

	std::string name = "DEFAULTFILEEXISTS";
	name += fromQueue ? '1' : '0';
	name += (local && remote) ? '1' : '0';
	WrapRecursive(this, 1.8, name.c_str());
	GetSizer()->Fit(this);
	GetSizer()->SetSizeHints(this);

	if (fromQueue) {
		return true;
	}

	return true;
}

void CDefaultFileExistsDlg::SelectDefaults(CFileExistsNotification::OverwriteAction* downloadAction, CFileExistsNotification::OverwriteAction* uploadAction)
{
	if (impl_->downloadAction_) {
		impl_->downloadAction_->SetSelection((downloadAction ? *downloadAction : m_defaults[0]) + 1);
	}
	if (impl_->uploadAction_) {
		impl_->uploadAction_->SetSelection((uploadAction ? *uploadAction : m_defaults[1]) + 1);
	}
}

CFileExistsNotification::OverwriteAction CDefaultFileExistsDlg::GetDefault(bool download)
{
	return m_defaults[download ? 0 : 1];
}

bool CDefaultFileExistsDlg::Run(wxWindow* parent, bool fromQueue, CFileExistsNotification::OverwriteAction *downloadAction, CFileExistsNotification::OverwriteAction *uploadAction)
{
	if (!Load(parent, fromQueue, downloadAction || !uploadAction, uploadAction || !downloadAction)) {
		return false;
	}
	SelectDefaults(downloadAction, uploadAction);

	Layout();
	GetSizer()->Fit(this);

	if (ShowModal() != wxID_OK) {
		return false;
	}

	if (impl_->downloadAction_) {
		int dl = impl_->downloadAction_->GetSelection();
		if (dl >= 0) {
			--dl;
		}
		CFileExistsNotification::OverwriteAction action = static_cast<CFileExistsNotification::OverwriteAction>(dl);

		if (downloadAction) {
			*downloadAction = action;
		}
		else {
			m_defaults[0] = action;
		}
	}

	if (impl_->uploadAction_) {
		int ul = impl_->uploadAction_->GetSelection();
		if (ul >= 0) {
			--ul;
		}
		CFileExistsNotification::OverwriteAction action = static_cast<CFileExistsNotification::OverwriteAction>(ul);

		if (uploadAction) {
			*uploadAction = action;
		}
		else {
			m_defaults[1] = action;
		}
	}

	return true;
}

void CDefaultFileExistsDlg::SetDefault(bool download, CFileExistsNotification::OverwriteAction action)
{
	m_defaults[download ? 0 : 1] = action;
}
