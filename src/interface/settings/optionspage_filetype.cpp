#include "../filezilla.h"
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_filetype.h"

#include "../textctrlex.h"

#include <wx/listctrl.h>
#include <wx/statbox.h>

struct COptionsPageFiletype::impl
{
	wxRadioButton* rbAuto_{};
	wxRadioButton* rbAscii_{};
	wxRadioButton* rbBinary_{};

	wxListCtrl* types_{};
	wxTextCtrlEx* extension_{};
	wxButton* add_{};
	wxButton* remove_{};

	wxCheckBox* noext_{};
	wxCheckBox* dotfile_{};
};

COptionsPageFiletype::COptionsPageFiletype()
	: impl_(std::make_unique<impl>())
{
}

COptionsPageFiletype::~COptionsPageFiletype()
{
}

bool COptionsPageFiletype::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	main->AddGrowableRow(1);
	SetSizer(main);

	{
		auto [box, inner] = lay.createStatBox(main, _("Default transfer type:"), 1);
		impl_->rbAuto_ = new wxRadioButton(box, nullID, _("&Auto"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		inner->Add(impl_->rbAuto_);
		impl_->rbAscii_ = new wxRadioButton(box, nullID, _("A&SCII"));
		inner->Add(impl_->rbAscii_);
		impl_->rbBinary_ = new wxRadioButton(box, nullID, _("&Binary"));
		inner->Add(impl_->rbBinary_);
	}

	{
		auto [box, inner] = lay.createStatBox(main, _("Automatic file type classification"), 1);

		inner->Add(new wxStaticText(box, nullID, _("Treat the &following filetypes as ASCII files:")));
		auto row = lay.createFlex(3);
		inner->Add(row);

		impl_->types_ = new wxListCtrl(box, nullID, wxDefaultPosition, wxDefaultSize, wxLC_SORT_ASCENDING | wxLC_REPORT | wxLC_NO_HEADER | wxBORDER_SUNKEN);
		impl_->types_->SetMinSize(wxSize(100, 150));
		impl_->types_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](auto const&) { SetCtrlState(); });
		impl_->types_->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](auto const&) { SetCtrlState(); });
		row->Add(impl_->types_);

		auto col = lay.createGrid(1);
		row->Add(col);
		impl_->extension_ = new wxTextCtrlEx(box, nullID);
		impl_->extension_->Bind(wxEVT_TEXT, [this](auto const&) { SetCtrlState(); });
		col->Add(impl_->extension_, lay.grow)->SetMinSize(wxSize(lay.dlgUnits(20), -1));
		impl_->add_ = new wxButton(box, nullID, _("A&dd"));
		impl_->add_->Bind(wxEVT_BUTTON, [this](auto const&) { OnAdd(); });
		col->Add(impl_->add_, lay.grow);
		impl_->remove_ = new wxButton(box, nullID, _("&Remove"));
		impl_->remove_->Bind(wxEVT_BUTTON, [this](auto const&) { OnRemove(); });
		col->Add(impl_->remove_, lay.grow);

		row->Add(new wxStaticText(box, nullID, _("If you enter the wrong filetypes, those files may get corrupted when transferred.")));

		impl_->noext_ = new wxCheckBox(box, nullID, _("Treat files &without extension as ASCII file"));
		inner->Add(impl_->noext_);
		impl_->dotfile_ = new wxCheckBox(box, nullID, _("&Treat dotfiles as ASCII files"));
		inner->Add(impl_->dotfile_);

		inner->Add(new wxStaticText(box, nullID, _("Dotfiles are filenames starting with a dot, e.g. .htaccess")));
	}

	return true;
}

bool COptionsPageFiletype::LoadPage()
{
	impl_->noext_->SetValue(m_pOptions->get_int(OPTION_ASCIINOEXT) != 0);
	impl_->dotfile_->SetValue(m_pOptions->get_int(OPTION_ASCIIDOTFILE) != 0);

	int const mode = m_pOptions->get_int(OPTION_ASCIIBINARY);
	if (mode == 1) {
		impl_->rbAscii_->SetValue(true);
	}
	else if (mode == 2) {
		impl_->rbBinary_->SetValue(true);
	}
	else {
		impl_->rbAuto_->SetValue(true);
	}
	impl_->types_->InsertColumn(0, wxString());

	std::wstring extensions = m_pOptions->get_string(OPTION_ASCIIFILES);
	std::wstring ext;
	size_t pos = extensions.find('|');
	while (pos != std::wstring::npos) {
		if (!pos) {
			if (!ext.empty()) {
				fz::replace_substrings(ext, L"\\\\", L"\\");
				impl_->types_->InsertItem(impl_->types_->GetItemCount(), ext);
				ext.clear();
			}
		}
		else if (extensions[pos - 1] != '\\') {
			ext += extensions.substr(0, pos);
			fz::replace_substrings(ext, L"\\\\", L"\\");
			impl_->types_->InsertItem(impl_->types_->GetItemCount(), ext);
			ext.clear();
		}
		else {
			ext += extensions.substr(0, pos - 1) + '|';
		}
		extensions = extensions.substr(pos + 1);
		pos = extensions.find('|');
	}
	ext += extensions;
	fz::replace_substrings(ext, L"\\\\", L"\\");
	impl_->types_->InsertItem(impl_->types_->GetItemCount(), ext);

	SetCtrlState();

	return true;
}

bool COptionsPageFiletype::SavePage()
{
	m_pOptions->set(OPTION_ASCIINOEXT, impl_->noext_->GetValue() ? 1 : 0);
	m_pOptions->set(OPTION_ASCIIDOTFILE, impl_->dotfile_->GetValue() ? 1 : 0);

	int mode{};
	if (impl_->rbAscii_->GetValue()) {
		mode = 1;
	}
	else if (impl_->rbBinary_->GetValue()) {
		mode = 2;
	}
	m_pOptions->set(OPTION_ASCIIBINARY, mode);

	std::wstring extensions;

	for (int i = 0; i < impl_->types_->GetItemCount(); ++i) {
		std::wstring ext = impl_->types_->GetItemText(i).ToStdWstring();
		fz::replace_substrings(ext, L"\\", L"\\\\");
		fz::replace_substrings(ext, L"|", L"\\|");
		if (!extensions.empty()) {
			extensions += '|';
		}
		extensions += ext;
	}
	m_pOptions->set(OPTION_ASCIIFILES, extensions);

	return true;
}

void COptionsPageFiletype::SetCtrlState()
{
	impl_->types_->SetColumnWidth(0, wxLIST_AUTOSIZE);

	impl_->remove_->Enable(impl_->types_->GetSelectedItemCount() != 0);
	impl_->add_->Enable(!impl_->extension_->GetValue().empty());
}

void COptionsPageFiletype::OnRemove()
{
	int item = -1;
	item = impl_->types_->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	while (item != -1) {
#ifndef FZ_WINDOWS
		impl_->types_->SetItemState(item, 0, wxLIST_STATE_SELECTED);
#endif
		impl_->types_->DeleteItem(item);
		--item;
		item = impl_->types_->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	}
	SetCtrlState();
}

void COptionsPageFiletype::OnAdd()
{
	std::wstring ext = impl_->extension_->GetValue().ToStdWstring();
	if (ext.empty()) {
		wxBell();
		return;
	}

	for (int i = 0; i < impl_->types_->GetItemCount(); ++i) {
		std::wstring text = impl_->types_->GetItemText(i).ToStdWstring();
		if (text == ext) {
			DisplayError(0, wxString::Format(_("The extension '%s' does already exist in the list"), ext));
			return;
		}
	}

	impl_->types_->InsertItem(impl_->types_->GetItemCount(), ext);

	SetCtrlState();
}
