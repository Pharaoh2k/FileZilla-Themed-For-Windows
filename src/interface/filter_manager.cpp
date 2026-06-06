#include "filezilla.h"
#include "filter_manager.h"
#include "filteredit.h"
#include "filezillaapp.h"
#include "inputdialog.h"
#include "Mainfrm.h"
#include "Options.h"
#include "state.h"
#include "xmlfunctions.h"

#include "../commonui/ipcmutex.h"

#include <libfilezilla/local_filesys.hpp>

#include <wx/statline.h>
#include <wx/statbox.h>

bool CFilterManager::m_loaded = false;
filter_data CFilterManager::global_filters_;
bool CFilterManager::m_filters_disabled = false;

BEGIN_EVENT_TABLE(CFilterDialog, wxDialogEx)
EVT_BUTTON(XRCID("wxID_OK"), CFilterDialog::OnOkOrApply)
EVT_BUTTON(XRCID("wxID_CANCEL"), CFilterDialog::OnCancel)
EVT_BUTTON(XRCID("wxID_APPLY"), CFilterDialog::OnOkOrApply)
EVT_BUTTON(XRCID("ID_EDIT"), CFilterDialog::OnEdit)
EVT_CHECKLISTBOX(wxID_ANY, CFilterDialog::OnFilterSelect)
EVT_BUTTON(XRCID("ID_SAVESET"), CFilterDialog::OnSaveAs)
EVT_BUTTON(XRCID("ID_RENAMESET"), CFilterDialog::OnRename)
EVT_BUTTON(XRCID("ID_DELETESET"), CFilterDialog::OnDeleteSet)
EVT_CHOICE(XRCID("ID_SETS"), CFilterDialog::OnSetSelect)

EVT_BUTTON(XRCID("ID_LOCAL_ENABLEALL"), CFilterDialog::OnChangeAll)
EVT_BUTTON(XRCID("ID_LOCAL_DISABLEALL"), CFilterDialog::OnChangeAll)
EVT_BUTTON(XRCID("ID_REMOTE_ENABLEALL"), CFilterDialog::OnChangeAll)
EVT_BUTTON(XRCID("ID_REMOTE_DISABLEALL"), CFilterDialog::OnChangeAll)
END_EVENT_TABLE()


CFilterDialog::CFilterDialog()
	: m_filters(global_filters_.filters)
	, m_filterSets(global_filters_.filter_sets)
	, m_currentFilterSet(global_filters_.current_filter_set)
{
}

bool CFilterDialog::Create(CMainFrame* parent)
{
	m_pMainFrame = parent;

	wxDialogEx::Create(parent, -1, _("Directory listing filters"));

	auto & lay = layout();

	auto main = lay.createMain(this, 1);
	main->AddGrowableCol(0);

	{
		auto row = lay.createFlex(0, 1);
		main->Add(row);

		row->Add(new wxStaticText(this, -1, _("&Filter sets:")), lay.valign);
		auto choice = new wxChoice(this, XRCID("ID_SETS"));
		choice->SetFocus();
		row->Add(choice, lay.valign);
		row->Add(new wxButton(this, XRCID("ID_SAVESET"), _("&Save as...")), lay.valign);
		row->Add(new wxButton(this, XRCID("ID_RENAMESET"), _("&Rename...")), lay.valign);
		row->Add(new wxButton(this, XRCID("ID_DELETESET"), _("&Delete...")), lay.valign);

		wxString name = _("Custom filter set");
		choice->Append(_T("<") + name + _T(">"));
		for (size_t i = 1; i < m_filterSets.size(); ++i) {
			choice->Append(m_filterSets[i].name);
		}
		choice->SetSelection(m_currentFilterSet);
	}

	auto sides = lay.createGrid(2);
	main->Add(sides, lay.grow);

	{
		auto [box, inner] = lay.createStatBox(sides, _("Local filters:"), 1);
		inner->AddGrowableCol(0);
		auto filters = new wxCheckListBox(box, XRCID("ID_LOCALFILTERS"), wxDefaultPosition, wxSize(-1, lay.dlgUnits(100)));
		inner->Add(filters, 1, wxGROW);
		auto row = lay.createFlex(0, 1);
		inner->Add(row, 0, wxALIGN_CENTER_HORIZONTAL);
		row->Add(new wxButton(box, XRCID("ID_LOCAL_ENABLEALL"), _("E&nable all")), lay.valign);
		row->Add(new wxButton(box, XRCID("ID_LOCAL_DISABLEALL"), _("D&isable all")), lay.valign);

		filters->Connect(wxID_ANY, wxEVT_LEFT_DOWN, wxMouseEventHandler(CFilterDialog::OnMouseEvent), 0, this);
		filters->Connect(wxID_ANY, wxEVT_KEY_DOWN, wxKeyEventHandler(CFilterDialog::OnKeyEvent), 0, this);

	}
	{
		auto [box, inner] = lay.createStatBox(sides, _("Remote filters:"), 1);
		inner->AddGrowableCol(0);
		auto filters = new wxCheckListBox(box, XRCID("ID_REMOTEFILTERS"), wxDefaultPosition, wxSize(-1, lay.dlgUnits(100)));
		inner->Add(filters, 1, wxGROW);
		auto row = lay.createFlex(0, 1);
		inner->Add(row, 0, wxALIGN_CENTER_HORIZONTAL);
		row->Add(new wxButton(box, XRCID("ID_REMOTE_ENABLEALL"), _("En&able all")), lay.valign);
		row->Add(new wxButton(box, XRCID("ID_REMOTE_DISABLEALL"), _("Disa&ble all")), lay.valign);

		filters->Connect(wxID_ANY, wxEVT_LEFT_DOWN, wxMouseEventHandler(CFilterDialog::OnMouseEvent), 0, this);
		filters->Connect(wxID_ANY, wxEVT_KEY_DOWN, wxKeyEventHandler(CFilterDialog::OnKeyEvent), 0, this);
	}


	main->Add(new wxStaticText(this, -1, _("Hold the shift key to toggle the filter state on both sides simultaneously.")));

	main->Add(new wxStaticLine(this), lay.grow);

	{
		auto row = lay.createFlex(0, 1);
		row->AddGrowableCol(0);
		main->Add(row, lay.grow);
		row->Add(new wxButton(this, XRCID("ID_EDIT"), _("&Edit filter rules...")), lay.valign);
		row->AddStretchSpacer();

		auto buttons = lay.createGrid(0, 1);
		row->Add(buttons, lay.valign);
		auto ok = new wxButton(this, wxID_OK, _("OK"));
		ok->SetDefault();
		buttons->Add(ok, lay.valigng);
		buttons->Add(new wxButton(this, wxID_CANCEL, _("Cancel")), lay.valigng);
		buttons->Add(new wxButton(this, wxID_APPLY, _("Apply")), lay.valigng);
	}

	DisplayFilters();

	SetCtrlState();

	GetSizer()->Fit(this);

	return true;
}

void CFilterDialog::OnOkOrApply(wxCommandEvent& event)
{
	global_filters_.filters = m_filters;
	global_filters_.filter_sets = m_filterSets;
	global_filters_.current_filter_set = m_currentFilterSet;

	SaveFilters();
	m_filters_disabled = false;

	CContextManager::Get()->NotifyAllHandlers(STATECHANGE_APPLYFILTER);

	if (event.GetId() == wxID_OK) {
		EndModal(wxID_OK);
	}
}

void CFilterDialog::OnCancel(wxCommandEvent&)
{
	EndModal(wxID_CANCEL);
}

void CFilterDialog::OnEdit(wxCommandEvent&)
{
	CFilterEditDialog dlg;
	if (!dlg.Create(this, m_filters, m_filterSets)) {
		return;
	}

	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	m_filters = dlg.GetFilters();
	m_filterSets = dlg.GetFilterSets();

	DisplayFilters();
}

void CFilterDialog::DisplayFilters()
{
	wxCheckListBox* pLocalFilters = XRCCTRL(*this, "ID_LOCALFILTERS", wxCheckListBox);
	wxCheckListBox* pRemoteFilters = XRCCTRL(*this, "ID_REMOTEFILTERS", wxCheckListBox);

	pLocalFilters->Clear();
	pRemoteFilters->Clear();

	for (unsigned int i = 0; i < m_filters.size(); ++i) {
		const CFilter& filter = m_filters[i];

		const bool localOnly = filter.IsLocalFilter();

		pLocalFilters->Append(filter.name);
		pRemoteFilters->Append(filter.name);

		pLocalFilters->Check(i, m_filterSets[m_currentFilterSet].local[i]);
		pRemoteFilters->Check(i, localOnly ? false : m_filterSets[m_currentFilterSet].remote[i]);
	}
}

void CFilterDialog::OnMouseEvent(wxMouseEvent& event)
{
	m_shiftClick = event.ShiftDown();
	event.Skip();
}

void CFilterDialog::OnKeyEvent(wxKeyEvent& event)
{
	m_shiftClick = event.ShiftDown();
	event.Skip();
}

void CFilterDialog::OnFilterSelect(wxCommandEvent& event)
{
	wxCheckListBox* pLocal = XRCCTRL(*this, "ID_LOCALFILTERS", wxCheckListBox);
	wxCheckListBox* pRemote = XRCCTRL(*this, "ID_REMOTEFILTERS", wxCheckListBox);

	int item = event.GetSelection();

	const CFilter& filter = m_filters[item];
	const bool localOnly = filter.IsLocalFilter();
	if (localOnly && event.GetEventObject() != pLocal) {
		pRemote->Check(item, false);
		wxMessageBoxEx(_("Selected filter only works for local files."), _("Cannot select filter"), wxICON_INFORMATION);
		return;
	}


	if (m_shiftClick) {
		if (event.GetEventObject() == pLocal) {
			if (!localOnly) {
				pRemote->Check(item, pLocal->IsChecked(event.GetSelection()));
			}
		}
		else {
			pLocal->Check(item, pRemote->IsChecked(event.GetSelection()));
		}
	}

	if (m_currentFilterSet) {
		m_filterSets[0] = m_filterSets[m_currentFilterSet];
		m_currentFilterSet = 0;
		wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);
		pChoice->SetSelection(0);
	}

	bool localChecked = pLocal->IsChecked(event.GetSelection());
	bool remoteChecked = pRemote->IsChecked(event.GetSelection());
	m_filterSets[0].local[item] = localChecked;
	m_filterSets[0].remote[item] = remoteChecked;
}

void CFilterDialog::OnSaveAs(wxCommandEvent&)
{
	CInputDialog dlg;
	dlg.Create(this, _("Enter name for filterset"), _("Please enter a unique name for this filter set"), 255);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	std::wstring name = dlg.GetValue().ToStdWstring();
	if (name.empty()) {
		wxMessageBoxEx(_("No name for the filterset given."), _("Cannot save filterset"), wxICON_INFORMATION);
		return;
	}
	wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);

	CFilterSet set;
	int old_pos = pChoice->GetSelection();
	if (old_pos > 0) {
		set = m_filterSets[old_pos];
	}
	else {
		set = m_filterSets[0];
	}

	int pos = pChoice->FindString(name);
	if (pos != wxNOT_FOUND) {
		if (wxMessageBoxEx(_("Given filterset name already exists, overwrite filter set?"), _("Filter set already exists"), wxICON_QUESTION | wxYES_NO) != wxYES) {
			return;
		}
	}

	if (pos == wxNOT_FOUND) {
		pos = m_filterSets.size();
		m_filterSets.push_back(set);
		pChoice->Append(name);
	}
	else {
		m_filterSets[pos] = set;
	}

	m_filterSets[pos].name = name;

	pChoice->SetSelection(pos);
	m_currentFilterSet = pos;

	SetCtrlState();

	GetSizer()->Fit(this);
}

void CFilterDialog::OnRename(wxCommandEvent&)
{
	wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);
	int old_pos = pChoice->GetSelection();
	if (old_pos == -1) {
		return;
	}

	if (!old_pos) {
		wxMessageBoxEx(_("This filter set cannot be renamed."));
		return;
	}

	CInputDialog dlg;

	wxString msg = wxString::Format(_("Please enter a new name for the filter set \"%s\""), pChoice->GetStringSelection());

	dlg.Create(this, _("Enter new name for filterset"), msg, 255);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	std::wstring name = dlg.GetValue().ToStdWstring();

	if (name == pChoice->GetStringSelection()) {
		// Nothing changed
		return;
	}

	if (name.empty()) {
		wxMessageBoxEx(_("No name for the filterset given."), _("Cannot save filterset"), wxICON_INFORMATION);
		return;
	}

	int pos = pChoice->FindString(name);
	if (pos != wxNOT_FOUND) {
		if (wxMessageBoxEx(_("Given filterset name already exists, overwrite filter set?"), _("Filter set already exists"), wxICON_QUESTION | wxYES_NO) != wxYES) {
			return;
		}
	}

	// Remove old entry
	pChoice->Delete(old_pos);
	CFilterSet set = m_filterSets[old_pos];
	m_filterSets.erase(m_filterSets.begin() + old_pos);

	pos = pChoice->FindString(name);
	if (pos == wxNOT_FOUND) {
		pos = m_filterSets.size();
		m_filterSets.push_back(set);
		pChoice->Append(name);
	}
	else {
		m_filterSets[pos] = set;
	}

	m_filterSets[pos].name = name;

	pChoice->SetSelection(pos);
	m_currentFilterSet = pos;

	GetSizer()->Fit(this);
}

void CFilterDialog::OnDeleteSet(wxCommandEvent&)
{
	wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);
	int pos = pChoice->GetSelection();
	if (pos < 0) {
		return;
	}

	if (!pos || static_cast<size_t>(pos) >= m_filterSets.size()) {
		wxMessageBoxEx(_("This filter set cannot be removed."));
		return;
	}

	m_filterSets[0] = m_filterSets[pos];

	pChoice->Delete(pos);
	m_filterSets.erase(m_filterSets.begin() + pos);

	pChoice->SetSelection(0);
	m_currentFilterSet = 0;

	SetCtrlState();
}

void CFilterDialog::OnSetSelect(wxCommandEvent& event)
{
	m_currentFilterSet = event.GetSelection();
	DisplayFilters();
	SetCtrlState();
}

void CFilterDialog::OnChangeAll(wxCommandEvent& event)
{
	bool check = true;
	if (event.GetId() == XRCID("ID_LOCAL_DISABLEALL") || event.GetId() == XRCID("ID_REMOTE_DISABLEALL")) {
		check = false;
	}

	bool local;
	std::vector<unsigned char>* pValues;
	wxCheckListBox* pListBox;
	if (event.GetId() == XRCID("ID_LOCAL_ENABLEALL") || event.GetId() == XRCID("ID_LOCAL_DISABLEALL")) {
		pListBox = XRCCTRL(*this, "ID_LOCALFILTERS", wxCheckListBox);
		pValues = &m_filterSets[0].local;
		local = true;
	}
	else {
		pListBox = XRCCTRL(*this, "ID_REMOTEFILTERS", wxCheckListBox);
		pValues = &m_filterSets[0].remote;
		local = false;
	}

	if (m_currentFilterSet) {
		m_filterSets[0] = m_filterSets[m_currentFilterSet];
		m_currentFilterSet = 0;
		wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);
		pChoice->SetSelection(0);
	}

	for (size_t i = 0; i < pListBox->GetCount(); ++i) {
		if (!local && (m_filters[i].IsLocalFilter())) {
			pListBox->Check(i, false);
			(*pValues)[i] = false;
		}
		else {
			pListBox->Check(i, check);
			(*pValues)[i] = check;
		}
	}
}

void CFilterDialog::SetCtrlState()
{
	wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);

	int sel = pChoice->GetSelection();
	XRCCTRL(*this, "ID_RENAMESET", wxButton)->Enable(sel > 0);
	XRCCTRL(*this, "ID_DELETESET", wxButton)->Enable(sel > 0);
}

CFilterManager::CFilterManager()
{
	LoadFilters();
}

bool CFilterManager::HasActiveFilters(bool ignore_disabled)
{
	if (!m_loaded) {
		LoadFilters();
	}

	if (global_filters_.filter_sets.empty()) {
		return false;
	}

	if (m_filters_disabled && !ignore_disabled) {
		return false;
	}

	const CFilterSet& set = global_filters_.filter_sets[global_filters_.current_filter_set];
	for (unsigned int i = 0; i < global_filters_.filters.size(); ++i) {
		if (set.local[i]) {
			return true;
		}

		if (set.remote[i]) {
			return true;
		}
	}

	return false;
}

bool CFilterManager::HasSameLocalAndRemoteFilters() const
{
	CFilterSet const& set = global_filters_.filter_sets[global_filters_.current_filter_set];
	return set.local == set.remote;

	return true;
}

bool CFilterManager::FilenameFiltered(std::wstring const& name, std::wstring const& path, bool dir, int64_t size, bool local, int attributes, fz::datetime const& date) const
{
	if (m_filters_disabled) {
		return false;
	}

	CFilterSet const& set = global_filters_.filter_sets[global_filters_.current_filter_set];
	auto const& active = local ? set.local : set.remote;

	// Check active filters
	for (unsigned int i = 0; i < global_filters_.filters.size(); ++i) {
		if (active[i]) {
			if (FilenameFilteredByFilter(global_filters_.filters[i], name, path, dir, size, attributes, date)) {
				return true;
			}
		}
	}

	return false;
}

void CFilterManager::LoadFilters()
{
	if (m_loaded) {
		return;
	}

	m_loaded = true;

	CReentrantInterProcessMutexLocker mutex(MUTEX_FILTERS);

	std::wstring file(wxGetApp().GetSettingsFile(L"filters"));
	if (fz::local_filesys::get_size(fz::to_native(file)) < 1) {
		file = wxGetApp().GetResourceDir().GetPath() + L"defaultfilters.xml";
	}

	CXmlFile xml(file);
	auto element = xml.Load();
	load_filters(element, global_filters_);

	if (!element) {
		wxString msg = xml.GetError() + _T("\n\n") + _("Any changes made to the filters will not be saved.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);
	}
}

void CFilterManager::Import(pugi::xml_node& element)
{
	if (!element) {
		return;
	}

	global_filters_.filters.clear();
	global_filters_.filter_sets.clear();
	global_filters_.current_filter_set = 0;
	m_filters_disabled = false;

	CReentrantInterProcessMutexLocker mutex(MUTEX_FILTERS);

	LoadFilters(element);
	SaveFilters();

	CContextManager::Get()->NotifyAllHandlers(STATECHANGE_APPLYFILTER);
}

void CFilterManager::LoadFilters(pugi::xml_node& element)
{
	load_filters(element, global_filters_);
	if (global_filters_.filter_sets.empty()) {
		CFilterSet set;
		set.local.resize(global_filters_.filters.size(), false);
		set.remote.resize(global_filters_.filters.size(), false);

		global_filters_.filter_sets.push_back(set);
	}
}

void CFilterManager::SaveFilters()
{
	CReentrantInterProcessMutexLocker mutex(MUTEX_FILTERS);

	CXmlFile xml(wxGetApp().GetSettingsFile(_T("filters")));
	auto element = xml.Load();
	if (!element) {
		wxString msg = xml.GetError() + _T("\n\n") + _("Any changes made to the filters could not be saved.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return;
	}

	save_filters(element, global_filters_);

	SaveWithErrorDialog(xml);
}

void CFilterManager::ToggleFilters()
{
	if (m_filters_disabled) {
		m_filters_disabled = false;
		return;
	}

	if (HasActiveFilters(true)) {
		m_filters_disabled = true;
	}
}

bool CFilterManager::HasActiveLocalFilters() const
{
	if (!m_filters_disabled) {
		CFilterSet const& set = global_filters_.filter_sets[global_filters_.current_filter_set];
		// Check active filters
		for (unsigned int i = 0; i < global_filters_.filters.size(); ++i) {
			if (set.local[i]) {
				return true;
			}
		}
	}

	return false;
}

bool CFilterManager::HasActiveRemoteFilters() const
{
	if (!m_filters_disabled) {
		CFilterSet const& set = global_filters_.filter_sets[global_filters_.current_filter_set];
		// Check active filters
		for (unsigned int i = 0; i < global_filters_.filters.size(); ++i) {
			if (set.remote[i]) {
				return true;
			}
		}
	}

	return false;
}

ActiveFilters CFilterManager::GetActiveFilters()
{
	ActiveFilters filters;

	if (m_filters_disabled) {
		return filters;
	}

	const CFilterSet& set = global_filters_.filter_sets[global_filters_.current_filter_set];

	// Check active filters
	for (unsigned int i = 0; i < global_filters_.filters.size(); ++i) {
		if (set.local[i]) {
			filters.first.push_back(global_filters_.filters[i]);
		}
		if (set.remote[i]) {
			filters.second.push_back(global_filters_.filters[i]);
		}
	}

	return filters;
}
