#include "../filezilla.h"

#include "../Options.h"
#include "../file_utils.h"
#include "../textctrlex.h"

#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_edit.h"

#include <wx/filedlg.h>
#include <wx/hyperlink.h>
#include <wx/statline.h>

struct COptionsPageEdit::impl final
{
	wxRadioButton* default_none_{};
	wxRadioButton* default_system_{};
	wxRadioButton* default_custom_{};

	wxTextCtrlEx* custom_{};
	wxButton* browse_{};

	wxRadioButton* use_assoc_{};
	wxRadioButton* use_default_{};

	wxCheckBox* watch_{};
};

COptionsPageEdit::COptionsPageEdit()
	: impl_(std::make_unique<impl>())
{
}

COptionsPageEdit::~COptionsPageEdit()
{
}

void ShowQuotingRules(wxWindow * parent)
{
	wxMessageBoxEx(wxString::Format(_("- The command and each argument are separated by spaces\n- A command or argument containing whitespace or a double-quote character need to be enclosed in double-quotes\n- Double-quotes inside of a command or argument need to be doubled up\n- In arguments, %%f is a placeholder for the file to be opened. Use %%%% for literal percents")), _("Quoting rules"), wxICON_INFORMATION, parent);
}

bool COptionsPageEdit::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	SetSizer(main);

	main->Add(new wxStaticText(this, nullID, _("&Default editor:")));

	auto onRadio = [this](wxCommandEvent const&) { SetCtrlState(); };

	impl_->default_none_ = new wxRadioButton(this, nullID, _("Do &not use default editor"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	impl_->default_none_->Bind(wxEVT_RADIOBUTTON, onRadio);
	main->Add(impl_->default_none_);
	impl_->default_system_ = new wxRadioButton(this, nullID, _("&Use system's default editor for text files"));
	impl_->default_system_->Bind(wxEVT_RADIOBUTTON, onRadio);
	main->Add(impl_->default_system_);
	impl_->default_custom_ = new wxRadioButton(this, nullID, _("Use &custom editor:"));
	impl_->default_custom_->Bind(wxEVT_RADIOBUTTON, onRadio);
	main->Add(impl_->default_custom_);

	auto row = lay.createFlex(2);
	row->AddGrowableCol(0);
	impl_->custom_ = new wxTextCtrlEx(this, nullID);
	row->Add(impl_->custom_, lay.valigng);
	impl_->browse_ = new wxButton(this, nullID, _("&Browse..."));
	row->Add(impl_->browse_, lay.valign);
	main->Add(row, 0, wxLEFT|wxGROW, lay.indent);
	impl_->browse_->Bind(wxEVT_BUTTON, [this](wxCommandEvent const&) { OnBrowseEditor(); });

	row = lay.createFlex(2);
	row->Add(new wxStaticText(this, nullID, _("Command and its arguments need to be properly quoted.")), 0, wxLEFT|wxALIGN_CENTER_VERTICAL, lay.indent);
	auto rules = new wxHyperlinkCtrl(this, nullID, _("Quoting rules"), wxString());
	row->Add(rules, lay.valign);
	main->Add(row, lay.grow);
	rules->Bind(wxEVT_HYPERLINK, [this](wxHyperlinkEvent const&) { ShowQuotingRules(this); });

	main->Add(new wxStaticLine(this), lay.grow);

	impl_->use_assoc_ = new wxRadioButton(this, nullID, _("U&se filetype associations if available"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	main->Add(impl_->use_assoc_);
	impl_->use_default_ = new wxRadioButton(this, nullID, _("&Always use default editor"));
	main->Add(impl_->use_default_);

	main->Add(new wxStaticLine(this), lay.grow);

	impl_->watch_ = new wxCheckBox(this, nullID, _("&Watch locally edited files and prompt to upload modifications"));
	main->Add(impl_->watch_);

	return true;
}

bool COptionsPageEdit::LoadPage()
{
	std::wstring editor = m_pOptions->get_string(OPTION_EDIT_DEFAULTEDITOR);
	if (editor.empty() || editor[0] == '0') {
		impl_->default_none_->SetValue(true);
	}
	else if (editor[0] == '1') {
		impl_->default_system_->SetValue(true);
	}
	else {
		impl_->default_custom_->SetValue(true);
		if (editor[0] == '2') {
			editor = editor.substr(1);
		}
		impl_->custom_->ChangeValue(editor);
	}

	if (m_pOptions->get_bool(OPTION_EDIT_ALWAYSDEFAULT) && !impl_->default_none_->GetValue()) {
		impl_->use_default_->SetValue(true);
	}
	else {
		impl_->use_assoc_->SetValue(true);
	}

	impl_->watch_->SetValue(m_pOptions->get_bool(OPTION_EDIT_TRACK_LOCAL));

	SetCtrlState();

	return true;
}

bool COptionsPageEdit::SavePage()
{
	if (impl_->default_custom_->GetValue()) {
		m_pOptions->set(OPTION_EDIT_DEFAULTEDITOR, L"2" + impl_->custom_->GetValue().ToStdWstring());
	}
	else {
		m_pOptions->set(OPTION_EDIT_DEFAULTEDITOR, impl_->default_system_->GetValue() ? L"1" : L"0");
	}

	m_pOptions->set(OPTION_EDIT_ALWAYSDEFAULT, impl_->use_default_->GetValue() && !impl_->default_none_->GetValue());
	m_pOptions->set(OPTION_EDIT_TRACK_LOCAL, impl_->watch_->GetValue());

	return true;
}

bool COptionsPageEdit::Validate()
{
	bool const custom = impl_->default_custom_->GetValue();

	if (custom) {

		std::wstring editor = fz::trimmed(impl_->custom_->GetValue().ToStdWstring());

		if (editor.empty()) {
			return DisplayError(impl_->custom_, _("A default editor needs to be set."));
		}
		auto cmd_with_args = UnquoteCommand(editor);
		if (cmd_with_args.empty()) {
			return DisplayError(impl_->custom_, _("Default editor not properly quoted."));
		}

		if (!ProgramExists(cmd_with_args[0])) {
			return DisplayError(impl_->custom_, _("The file selected as default editor does not exist."));
		}

		editor = QuoteCommand(cmd_with_args);
		impl_->custom_->ChangeValue(editor);
	}

	return true;
}

void COptionsPageEdit::OnBrowseEditor()
{
	wxFileDialog dlg(this, _("Select default editor"), wxString(), wxString(),
#ifdef __WXMSW__
		_T("Executable file (*.exe)|*.exe"),
#elif __WXMAC__
		_T("Applications (*.app)|*.app"),
#else
		wxFileSelectorDefaultWildcardStr,
#endif
		wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	std::wstring editor = dlg.GetPath().ToStdWstring();
	if (editor.empty()) {
		return;
	}

	if (!ProgramExists(editor)) {
		impl_->custom_->SetFocus();
		wxMessageBoxEx(_("Selected editor does not exist."), _("File not found"), wxICON_EXCLAMATION, this);
		return;
	}

	std::vector<std::wstring> cmd;
	cmd.push_back(editor);

	impl_->custom_->ChangeValue(QuoteCommand(cmd));
}

void COptionsPageEdit::SetCtrlState()
{
	bool const custom = impl_->default_custom_->GetValue();
	impl_->custom_->Enable(custom);
	impl_->browse_->Enable(custom);

	bool canUseDefault = custom || impl_->default_system_->GetValue();
	impl_->use_default_->Enable(canUseDefault);

	if (!canUseDefault) {
		impl_->use_assoc_->SetValue(true);
	}
}
