#include "../filezilla.h"

#include "../file_utils.h"
#include "../Options.h"
#include "../textctrlex.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_edit_associations.h"

#include <wx/hyperlink.h>

void ShowQuotingRules(wxWindow* parent);

bool COptionsPageEditAssociations::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	main->AddGrowableRow(1);
	SetSizer(main);

	main->Add(new wxStaticText(this, nullID, _("C&ustom filetype associations:")));

	assocs_ = new wxTextCtrlEx(this, nullID, wxString(), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
	main->Add(assocs_, lay.grow);

	main->Add(new wxStaticText(this, nullID, _("Format: Extension followed by properly quoted command and arguments.")));

	main->Add(new wxStaticText(this, nullID, _("Example: png \"c:\\program files\\viewer\\viewer.exe\" -open")));

	auto rules = new wxHyperlinkCtrl(this, nullID, _("Quoting rules"), wxString());
	main->Add(rules);
	rules->Bind(wxEVT_HYPERLINK, [this](wxHyperlinkEvent const&) { ShowQuotingRules(this); });

	return true;
}

bool COptionsPageEditAssociations::LoadPage()
{
	assocs_->ChangeValue(m_pOptions->get_string(OPTION_EDIT_CUSTOMASSOCIATIONS));
	return true;
}

bool COptionsPageEditAssociations::SavePage()
{
	m_pOptions->set(OPTION_EDIT_CUSTOMASSOCIATIONS, assocs_->GetValue().ToStdWstring());
	return true;
}

bool COptionsPageEditAssociations::Validate()
{
	std::wstring const raw_assocs = assocs_->GetValue().ToStdWstring();
	auto assocs = fz::strtok_view(raw_assocs, L"\r\n", true);

	for (std::wstring_view assoc : assocs) {
		std::optional<std::wstring> aext = UnquoteFirst(assoc);
		if (!aext || aext->empty()) {
			return DisplayError(assocs_, _("Improperly quoted association."));
		}

		auto cmd_with_args = UnquoteCommand(assoc);

		if (cmd_with_args.empty() || cmd_with_args[0].empty()) {
			return DisplayError(assocs_, _("Improperly quoted association."));
		}

		if (!ProgramExists(cmd_with_args[0])) {
			wxString error = _("Associated program not found:");
			error += '\n';
			error += cmd_with_args[0];
			return DisplayError(assocs_, error);
		}
	}

	return true;
}
