#include "filezilla.h"
#include "filteredit.h"
#include "customheightlistctrl.h"
#include "window_state_manager.h"
#include "Options.h"
#include "textctrlex.h"

#include <libfilezilla/translate.hpp>

BEGIN_EVENT_TABLE(CFilterEditDialog, CFilterConditionsDialog)
EVT_BUTTON(XRCID("wxID_OK"), CFilterEditDialog::OnOK)
EVT_BUTTON(XRCID("wxID_CANCEL"), CFilterEditDialog::OnCancel)
EVT_BUTTON(XRCID("ID_NEW"), CFilterEditDialog::OnNew)
EVT_BUTTON(XRCID("ID_DELETE"), CFilterEditDialog::OnDelete)
EVT_BUTTON(XRCID("ID_RENAME"), CFilterEditDialog::OnRename)
EVT_BUTTON(XRCID("ID_COPY"), CFilterEditDialog::OnCopy)
EVT_LISTBOX(XRCID("ID_FILTERS"), CFilterEditDialog::OnFilterSelect)
END_EVENT_TABLE()

CFilterEditDialog::CFilterEditDialog()
{
}

CFilterEditDialog::~CFilterEditDialog()
{
	if (m_pWindowStateManager) {
		m_pWindowStateManager->Remember(OPTION_FILTEREDIT_SIZE);
		delete m_pWindowStateManager;
	}
}

void CFilterEditDialog::OnOK(wxCommandEvent&)
{
	if (!Validate()) {
		return;
	}

	if (m_currentSelection != -1) {
		wxASSERT((unsigned int)m_currentSelection < m_filters.size());
		SaveFilter(m_filters[m_currentSelection]);
	}
	for (unsigned int i = 0; i < m_filters.size(); ++i) {
		if (!m_filters[i].HasConditionOfType(filter_permissions) && !m_filters[i].HasConditionOfType(filter_attributes)) {
			continue;
		}

		for (unsigned int j = 0; j < m_filterSets.size(); ++j) {
			m_filterSets[j].remote[i] = false;
		}
	}

	EndModal(wxID_OK);
}

void CFilterEditDialog::OnCancel(wxCommandEvent&)
{
	EndModal(wxID_CANCEL);
}

bool CFilterEditDialog::Create(wxWindow* parent, const std::vector<CFilter>& filters, const std::vector<CFilterSet>& filterSets)
{
	bool has_foreign_type = false;
	for (std::vector<CFilter>::const_iterator iter = filters.begin(); iter != filters.end(); ++iter) {
		const CFilter& filter = *iter;
		if (!filter.HasConditionOfType(filter_foreign)) {
			continue;
		}

		has_foreign_type = true;
		break;
	}

	wxDialogEx::Create(parent, -1, _("Edit filters"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

	auto& lay = layout();

	auto main = lay.createMain(this, 1);
	main->AddGrowableCol(0);
	main->AddGrowableRow(0);

	auto sides = lay.createFlex(2);
	main->Add(sides, lay.grow);
	sides->AddGrowableRow(0);
	sides->AddGrowableCol(1);

	{
		auto left = lay.createFlex(1);
		sides->Add(left, lay.grow);
		left->AddGrowableRow(1);

		left->Add(new wxStaticText(this, -1, _("&Filters:")));
		filterList_ = new wxListBox(this, XRCID("ID_FILTERS"));
		filterList_->SetFocus();
		left->Add(filterList_, lay.grow);

		auto grid = lay.createGrid(2, 2);
		left->Add(grid, 0, wxALIGN_CENTER_HORIZONTAL);

		grid->Add(new wxButton(this, XRCID("ID_NEW"), _("&New")));
		grid->Add(new wxButton(this, XRCID("ID_DELETE"), _("&Delete")));
		grid->Add(new wxButton(this, XRCID("ID_RENAME"), _("&Rename")));
		grid->Add(new wxButton(this, XRCID("ID_COPY"), _("&Duplicate")));
	}
	{
		auto right = lay.createFlex(1);
		sides->Add(right, lay.grow);
		right->AddGrowableCol(0);
		right->AddGrowableRow(2);

		auto row = lay.createFlex(2);
		right->Add(row, lay.grow);
		row->AddGrowableCol(1);
		row->Add(new wxStaticText(this, -1, _("F&ilter name:")), lay.valign);
		row->Add(new wxTextCtrlEx(this, XRCID("ID_NAME"), wxString()), lay.valigng);

		row = lay.createFlex(2);
		right->Add(row, lay.grow);
		row->AddGrowableCol(1);
		row->Add(new wxStaticText(this, -1, _("&Filter conditions:")), lay.valign);
		auto choice = new wxChoice(this, XRCID("ID_MATCHTYPE"));
		row->Add(choice, lay.valigng);

		choice->AppendString(_("Filter out items matching all of the following"));
		choice->AppendString(_("Filter out items matching any of the following"));
		choice->AppendString(_("Filter out items matching none of the following"));
		choice->AppendString(_("Filter out items matching not all of the following"));

		auto conditions = new wxCustomHeightListCtrl(this, XRCID("ID_CONDITIONS"), wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxSUNKEN_BORDER | wxTAB_TRAVERSAL);
		conditions->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
		right->Add(conditions, lay.grow)->SetMinSize(wxSize(lay.dlgUnits(200), lay.dlgUnits(100)));

		right->Add(new wxCheckBox(this, XRCID("ID_CASE"), _("Conditions are c&ase sensitive")));
		row = lay.createFlex(0, 1);
		right->Add(row);
		row->Add(new wxStaticText(this, -1, _("Filter applies to:")));
		row->Add(new wxCheckBox(this, XRCID("ID_FILES"), _("Fil&es")));
		row->Add(new wxCheckBox(this, XRCID("ID_DIRS"), _("Dire&ctories")));
	}
	auto buttons = lay.createButtonSizer(this, main, false);
	auto ok = new wxButton(this, wxID_OK, _("OK"));
	ok->SetDefault();
	buttons->AddButton(ok);
	auto cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
	buttons->AddButton(cancel);
	buttons->Realize();

	Layout();
	GetSizer()->Fit(this);
	SetMinSize(GetSize());

	int conditions = filter_name | filter_size | filter_path | filter_meta | filter_date;
	if (has_foreign_type) {
		conditions |= filter_foreign;
	}
	if (!CreateListControl(conditions)) {
		return false;
	}

	m_filters = filters;
	m_filterSets = filterSets;
	for (auto const& filter: filters) {
		filterList_->Append(filter.name);
	}

	m_pWindowStateManager = new CWindowStateManager(this, *COptions::Get());
	m_pWindowStateManager->Restore(OPTION_FILTEREDIT_SIZE, wxSize(750, 500));

	Layout();

	SetCtrlState(false);

	return true;
}

void CFilterEditDialog::SaveFilter(CFilter& filter)
{
	bool const matchCase = XRCCTRL(*this, "ID_CASE", wxCheckBox)->GetValue();
	filter = GetFilter(matchCase);
	filter.matchCase = matchCase;

	filter.filterFiles = XRCCTRL(*this, "ID_FILES", wxCheckBox)->GetValue();
	filter.filterDirs = XRCCTRL(*this, "ID_DIRS", wxCheckBox)->GetValue();

	filter.name = XRCCTRL(*this, "ID_NAME", wxTextCtrl)->GetValue().ToStdWstring();
	if (filter.name != filterList_->GetString(m_currentSelection)) {
		int oldSelection = m_currentSelection;
		filterList_->Delete(oldSelection);
		filterList_->Insert(filter.name, oldSelection);
		filterList_->SetSelection(oldSelection);
	}
}

void CFilterEditDialog::OnNew(wxCommandEvent&)
{
	if (m_currentSelection != -1) {
		if (!Validate()) {
			return;
		}
		SaveFilter(m_filters[m_currentSelection]);
	}

	int index = 1;
	std::wstring name = fztranslate("New filter");
	std::wstring newName = name;
	while (filterList_->FindString(newName) != wxNOT_FOUND) {
		newName = fz::sprintf(L"%s (%d)", name, ++index);
	}

	wxTextEntryDialog dlg(this, _("Please enter a name for the new filter."), _("Enter filter name"), newName);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}
	newName = dlg.GetValue().ToStdWstring();

	if (newName.empty()) {
		wxMessageBoxEx(_("No filter name given"), _("Cannot create new filter"), wxICON_INFORMATION);
		return;
	}

	if (filterList_->FindString(newName) != wxNOT_FOUND) {
		wxMessageBoxEx(_("The entered filter name already exists, please choose a different name."), _("Filter name already exists"), wxICON_ERROR, this);
		return;
	}

	CFilter filter;
	filter.name = newName;

	m_filters.push_back(filter);

	for (auto iter = m_filterSets.begin(); iter != m_filterSets.end(); ++iter) {
		CFilterSet& set = *iter;
		set.local.push_back(false);
		set.remote.push_back(false);
	}

	int item = filterList_->Append(newName);
	filterList_->Select(item);
	wxCommandEvent evt;
	OnFilterSelect(evt);
}

void CFilterEditDialog::OnDelete(wxCommandEvent&)
{
	int item = filterList_->GetSelection();
	if (item == -1) {
		return;
	}

	m_currentSelection = -1;
	filterList_->Delete(item);
	m_filters.erase(m_filters.begin() + item);

	// Remote filter from all filter sets
	for (auto & set : m_filterSets) {
		set.local.erase(set.local.begin() + item);
		set.remote.erase(set.remote.begin() + item);
	}

	XRCCTRL(*this, "ID_NAME", wxTextCtrl)->ChangeValue(wxString());
	ClearFilter();
	SetCtrlState(false);
}

void CFilterEditDialog::OnRename(wxCommandEvent&)
{
	if (m_currentSelection == -1) {
		wxBell();
		return;
	}

	const wxString& oldName = XRCCTRL(*this, "ID_NAME", wxTextCtrl)->GetValue();
	wxTextEntryDialog *pDlg = new wxTextEntryDialog(this, _("Please enter a new name for the filter."), _("Enter filter name"), oldName);
	pDlg->SetMaxLength(255);
	if (pDlg->ShowModal() != wxID_OK) {
		delete pDlg;
		return;
	}

	const wxString& newName = pDlg->GetValue();
	delete pDlg;

	if (newName.empty()) {
		wxMessageBoxEx(_("Empty filter names are not allowed."), _("Empty name"), wxICON_ERROR, this);
		return;
	}

	if (newName == oldName) {
		return;
	}

	if (filterList_->FindString(newName) != wxNOT_FOUND) {
		wxMessageBoxEx(_("The entered filter name already exists, please choose a different name."), _("Filter name already exists"), wxICON_ERROR, this);
		return;
	}

	filterList_->Delete(m_currentSelection);
	filterList_->Insert(newName, m_currentSelection);
	filterList_->Select(m_currentSelection);
}

void CFilterEditDialog::OnCopy(wxCommandEvent&)
{
	if (m_currentSelection == -1) {
		return;
	}

	if (!Validate()) {
		return;
	}
	SaveFilter(m_filters[m_currentSelection]);

	CFilter filter = m_filters[m_currentSelection];

	int index = 1;
	std::wstring const& name = filter.name;
	std::wstring newName = name;
	while (filterList_->FindString(newName) != wxNOT_FOUND) {
		newName = fz::sprintf(L"%s (%d)", name, ++index);
	}

	wxTextEntryDialog dlg(this, _("Please enter a new name for the copied filter."), _("Enter filter name"), newName);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	newName = dlg.GetValue().ToStdWstring();
	if (newName.empty()) {
		wxMessageBoxEx(_("Empty filter names are not allowed."), _("Empty name"), wxICON_ERROR, this);
		return;
	}

	if (filterList_->FindString(newName) != wxNOT_FOUND) {
		wxMessageBoxEx(_("The entered filter name already exists, please choose a different name."), _("Filter name already exists"), wxICON_ERROR, this);
		return;
	}

	filter.name = newName;

	m_filters.push_back(filter);

	for (auto iter = m_filterSets.begin(); iter != m_filterSets.end(); ++iter) {
		CFilterSet& set = *iter;
		set.local.push_back(false);
		set.remote.push_back(false);
	}

	int item = filterList_->Append(newName);
	filterList_->Select(item);
	wxCommandEvent evt;
	OnFilterSelect(evt);
}

void CFilterEditDialog::OnFilterSelect(wxCommandEvent&)
{
	int item = filterList_->GetSelection();
	if (item == -1) {
		m_currentSelection = -1;
		SetCtrlState(false);
		return;
	}
	else {
		SetCtrlState(true);
	}

	if (item == m_currentSelection) {
		return;
	}

	if (m_currentSelection != -1) {
		wxASSERT((unsigned int)m_currentSelection < m_filters.size());

		if (!Validate()) {
			return;
		}

		SaveFilter(m_filters[m_currentSelection]);
	}

	m_currentSelection = item;
	filterList_->SetSelection(item); // In case SaveFilter has renamed an item
	CFilter filter = m_filters[item];
	EditFilter(filter);

	XRCCTRL(*this, "ID_CASE", wxCheckBox)->SetValue(filter.matchCase);

	XRCCTRL(*this, "ID_FILES", wxCheckBox)->SetValue(filter.filterFiles);
	XRCCTRL(*this, "ID_DIRS", wxCheckBox)->SetValue(filter.filterDirs);

	XRCCTRL(*this, "ID_NAME", wxTextCtrl)->SetValue(filter.name);
}

void CFilterEditDialog::SetCtrlState(bool enabled)
{
	XRCCTRL(*this, "ID_CASE", wxCheckBox)->Enable(enabled);
	XRCCTRL(*this, "ID_FILES", wxCheckBox)->Enable(enabled);
	XRCCTRL(*this, "ID_DIRS", wxCheckBox)->Enable(enabled);
}

const std::vector<CFilter>& CFilterEditDialog::GetFilters() const
{
	return m_filters;
}

const std::vector<CFilterSet>& CFilterEditDialog::GetFilterSets() const
{
	return m_filterSets;
}

bool CFilterEditDialog::Validate()
{
	if (m_currentSelection == -1) {
		return true;
	}

	wxString error;
	if (!ValidateFilter(error)) {
		filterList_->SetSelection(m_currentSelection);
		wxMessageBoxEx(error, _("Filter validation failed"), wxICON_ERROR, this);
		return false;
	}

	wxString name = XRCCTRL(*this, "ID_NAME", wxTextCtrl)->GetValue();
	if (name.empty()) {
		filterList_->SetSelection(m_currentSelection);
		XRCCTRL(*this, "ID_NAME", wxTextCtrl)->SetFocus();
		wxMessageBoxEx(_("Need to enter filter name"), _("Filter validation failed"), wxICON_ERROR, this);
		return false;
	}

	int pos = filterList_->FindString(name);
	if (pos != wxNOT_FOUND && pos != m_currentSelection) {
		filterList_->SetSelection(m_currentSelection);
		XRCCTRL(*this, "ID_NAME", wxTextCtrl)->SetFocus();
		wxMessageBoxEx(_("Filter name already exists"), _("Filter validation failed"), wxICON_ERROR, this);
		return false;
	}

	return true;
}
