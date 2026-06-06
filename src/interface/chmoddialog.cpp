#include "filezilla.h"
#include "chmoddialog.h"
#include "textctrlex.h"

#include <wx/statbox.h>

struct CChmodDialog::impl
{
	wxCheckBox* checkBoxes[12]{};

	wxCheckBox* recursive_{};
	wxTextCtrlEx* numeric_{};

	wxRadioButton* applyAll_{};
	wxRadioButton* applyFiles_{};
	wxRadioButton* applyDirs_{};
};

CChmodDialog::CChmodDialog()
	: impl_(std::make_unique<impl>())
{
}

CChmodDialog::~CChmodDialog() = default;

bool CChmodDialog::Create(wxWindow* parent, int fileCount, int dirCount,
	std::wstring const& name, posix_chmod const& initial_chmod)
{
	SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
	SetParent(parent);

	wxString title;
	if (!dirCount) {
		if (fileCount == 1) {
			title = wxString::Format(_("Please select the new attributes for the file \"%s\"."), LabelEscape(name));
		}
		else {
			title = _("Please select the new attributes for the selected files.");
		}
	}
	else {
		if (!fileCount) {
			if (dirCount == 1) {
				title = wxString::Format(_("Please select the new attributes for the directory \"%s\"."), LabelEscape(name));
			}
			else {
				title = _("Please select the new attributes for the selected directories.");
			}
		}
		else {
			title = _("Please select the new attributes for the selected files and directories.");
		}
	}

	if (!wxDialogEx::Create(parent, nullID, _("Change file attributes"), wxDefaultPosition, wxDefaultSize)) {
		return false;
	}

	auto& lay = layout();
	auto main = lay.createMain(this, 1);

	WrapText(this, title, lay.dlgUnits(160));
	main->Add(new wxStaticText(this, nullID, title));

	{
		auto [box, inner] = lay.createStatBox(main, _("Owner permissions"), 3);
		impl_->checkBoxes[0] = new wxCheckBox(box, nullID, _("&Read"), wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
		inner->Add(impl_->checkBoxes[0]);
		impl_->checkBoxes[1] = new wxCheckBox(box, nullID, _("&Write"), wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
		inner->Add(impl_->checkBoxes[1]);
		impl_->checkBoxes[2] = new wxCheckBox(box, nullID, _("&Execute"), wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
		inner->Add(impl_->checkBoxes[2]);
	}
	{
		auto [box, inner] = lay.createStatBox(main, _("Group permissions"), 3);
		impl_->checkBoxes[3] = new wxCheckBox(box, nullID, _("Re&ad"), wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
		inner->Add(impl_->checkBoxes[3]);
		impl_->checkBoxes[4] = new wxCheckBox(box, nullID, _("W&rite"), wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
		inner->Add(impl_->checkBoxes[4]);
		impl_->checkBoxes[5] = new wxCheckBox(box, nullID, _("E&xecute"), wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
		inner->Add(impl_->checkBoxes[5]);
	}
	{
		auto [box, inner] = lay.createStatBox(main, _("Public permissions"), 3);
		impl_->checkBoxes[6] = new wxCheckBox(box, nullID, _("Rea&d"), wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
		inner->Add(impl_->checkBoxes[6]);
		impl_->checkBoxes[7] = new wxCheckBox(box, nullID, _("Wr&ite"), wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
		inner->Add(impl_->checkBoxes[7]);
		impl_->checkBoxes[8] = new wxCheckBox(box, nullID, _("Exe&cute"), wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
		inner->Add(impl_->checkBoxes[8]);
	}
	{
		auto [box, inner] = lay.createStatBox(main, _("Public permissions"), 3);
		impl_->checkBoxes[9] = new wxCheckBox(box, nullID, _("Set-user-ID"), wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
		inner->Add(impl_->checkBoxes[9]);
		impl_->checkBoxes[10] = new wxCheckBox(box, nullID, _("Set group-ID"), wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
		inner->Add(impl_->checkBoxes[10]);
		impl_->checkBoxes[11] = new wxCheckBox(box, nullID, _("Sticky bit"), wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
		inner->Add(impl_->checkBoxes[11]);
	}

	auto row = lay.createFlex(2);
	row->AddGrowableCol(1);
	main->Add(row, lay.grow);

	row->Add(new wxStaticText(this, nullID, _("&Chmod:")), lay.valign);
	impl_->numeric_ = new wxTextCtrlEx(this, nullID, wxString());
	row->Add(impl_->numeric_, lay.valigng);
	impl_->numeric_->SetFocus();
	impl_->numeric_->Bind(wxEVT_TEXT, &CChmodDialog::OnNumericChanged, this);

	wxString desc = _("You can enter a textual mode change (chmod), or the new mode bits in octal.");
	WrapText(this, desc, lay.dlgUnits(160));
	main->Add(new wxStaticText(this, nullID, desc), lay.valign);

	if (dirCount) {
		main->AddSpacer(0);

		impl_->recursive_ = new wxCheckBox(this, nullID, _("Rec&urse into subdirectories"));
		impl_->recursive_->Bind(wxEVT_CHECKBOX, &CChmodDialog::OnRecurseChanged, this);
		main->Add(impl_->recursive_);

		auto inner = lay.createFlex(1);
		main->Add(inner, 0, wxLEFT, lay.indent);

		impl_->applyAll_ = new wxRadioButton(this, nullID, _("A&pply to all files and directories"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		inner->Add(impl_->applyAll_);
		impl_->applyFiles_ = new wxRadioButton(this, nullID, _("Apply to &files only"));
		inner->Add(impl_->applyFiles_);
		impl_->applyDirs_ = new wxRadioButton(this, nullID, _("App&ly to directories only"));
		inner->Add(impl_->applyDirs_);

		impl_->applyAll_->SetValue(true);
		impl_->applyAll_->Disable();
		impl_->applyFiles_->Disable();
		impl_->applyDirs_->Disable();
	}

	for (int i = 0; i < 12; ++i) {
		impl_->checkBoxes[i]->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &CChmodDialog::OnCheckboxClick, this);
	}

	auto buttons = lay.createButtonSizer(this, main, true);

	auto ok = new wxButton(this, wxID_OK, _("&OK"));
	ok->SetDefault();
	ok->Bind(wxEVT_BUTTON, &CChmodDialog::OnOK, this);
	buttons->AddButton(ok);

	auto cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
	cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent const&) { EndModal(wxID_CANCEL); });
	buttons->AddButton(cancel);

	buttons->Realize();

	GetSizer()->Fit(this);
	GetSizer()->SetSizeHints(this);

	// Intentionally using SetValue instead of ChangeValue so that the checkboxes are updated as well.
	impl_->numeric_->SetValue(to_string(initial_chmod));

	return true;
}

void CChmodDialog::OnOK(wxCommandEvent&)
{
	auto chmod = parse_chmod(fz::to_utf8(impl_->numeric_->GetValue()));
	if (!chmod) {
		wxMessageBoxEx(_("The entered chmod is invalid"), _("Invalid permissions"), wxOK | wxICON_ERROR);
		return;
	}

	EndModal(wxID_OK);
}

namespace {
constexpr posix_permissions mapping[] = {
	posix_permissions::user_read,  posix_permissions::user_write,  posix_permissions::user_execute,
	posix_permissions::group_read, posix_permissions::group_write, posix_permissions::group_execute,
	posix_permissions::other_read, posix_permissions::other_write, posix_permissions::other_execute,

	posix_permissions::setuid, posix_permissions::setgid, posix_permissions::sticky,
};
}

posix_chmod CChmodDialog::GetChmodFromCheckboxes()
{
	posix_chmod ret{};

	for (size_t i = 0; i < 12; ++i) {
		wxCheckBoxState state = impl_->checkBoxes[i]->Get3StateValue();
		switch (state)
		{
		default:
			break;
		case wxCHK_UNCHECKED:
			ret.mask_ |= mapping[i];
			break;
		case wxCHK_CHECKED:
			ret.mask_ |= mapping[i];
			ret.perms_ |= mapping[i];
			break;
		}
	}

	return ret;
}

ChmodData CChmodDialog::GetChmodData() const
{
	ChmodData ret;
	if (impl_ && impl_->numeric_) {
		ret.chmod_ = parse_chmod(fz::to_utf8(impl_->numeric_->GetValue()));
		if (impl_->recursive_ && impl_->recursive_->GetValue()) {
			ret.recurse_ = true;
			if (impl_->applyFiles_ && impl_->applyFiles_->GetValue()) {
				ret.apply_dirs_ = false;
			}
			else if (impl_->applyDirs_ && impl_->applyDirs_->GetValue()) {
				ret.apply_files_ = false;
			}
		}
	}
	return ret;
}

void CChmodDialog::OnCheckboxClick(wxCommandEvent&)
{
	auto chmod = GetChmodFromCheckboxes();
	auto s = fz::to_wstring_from_utf8(to_string(chmod));
	impl_->numeric_->ChangeValue(s);
}

void CChmodDialog::OnNumericChanged(wxCommandEvent&)
{
	auto chmod = parse_chmod(fz::to_utf8(impl_->numeric_->GetValue()));
	if (!chmod) {
		return;
	}

	for (size_t i = 0; i < 12; ++i) {
		if ((chmod.mask_ & mapping[i]) != posix_permissions::none) {
			impl_->checkBoxes[i]->Set3StateValue((chmod.perms_ & mapping[i]) != posix_permissions::none ? wxCHK_CHECKED : wxCHK_UNCHECKED);
		}
		else {
			impl_->checkBoxes[i]->Set3StateValue(wxCHK_UNDETERMINED);
		}
	}
}

void CChmodDialog::OnRecurseChanged(wxCommandEvent&)
{
	impl_->applyAll_->Enable(impl_->recursive_->GetValue());
	impl_->applyFiles_->Enable(impl_->recursive_->GetValue());
	impl_->applyDirs_->Enable(impl_->recursive_->GetValue());
}
