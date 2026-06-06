#include "filezilla.h"
#include "filezillaapp.h"
#include "import.h"
#include "sitemanager.h"
#include "xmlfunctions.h"
#include "Options.h"
#include "queue.h"

#include "../commonui/ipcmutex.h"

#include <wx/filedlg.h>

CImportDialog::CImportDialog(wxWindow* parent, CQueueView* pQueueView)
	: m_parent(parent), m_pQueueView(pQueueView)
{
}

void CImportDialog::Run(XmlOptions & options, login_manager & lim)
{
	wxFileDialog dlg(m_parent, _("Select file to import settings from"), wxString(),
					_T("FileZilla.xml"), _T("XML files (*.xml)|*.xml"),
					wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	dlg.CenterOnParent();

	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	wxFileName fn(dlg.GetPath());
	wxString const path = fn.GetPath();
	wxString const settingsDir(options.get_string(OPTION_DEFAULT_SETTINGSDIR));
	if (path == settingsDir) {
		wxMessageBoxEx(_("You cannot import settings from FileZilla's own settings directory."), _("Error importing"), wxICON_ERROR, m_parent);
		return;
	}

	CXmlFile fz3(dlg.GetPath().ToStdWstring());
	auto fz3Root = fz3.Load();
	if (fz3Root) {
		bool sites = fz3Root.child("Servers") != 0;
		bool settings = fz3Root.child("Settings") != 0;
		bool queue = fz3Root.child("Queue") != 0;
		bool filters = fz3Root.child("Filters") != 0;

		if (settings || queue || sites || filters) {
			if (!Create(m_parent, nullID, _("Import Settings"))) {
				return;
			}

			auto & lay = layout();
			auto main = lay.createMain(this, 1);

			main->Add(new wxStaticText(this, nullID, _("The selected file contains importable data for the following categories:")));

			wxCheckBox *cbsites{};
			if (sites) {
				cbsites = new wxCheckBox(this, nullID, _("Site &Manager entries"));
				cbsites->SetValue(1);
				main->Add(cbsites);
			}
			wxCheckBox *cbsettings{};
			if (settings) {
				cbsettings = new wxCheckBox(this, nullID, _("&Settings"));
				cbsettings->SetValue(1);
				main->Add(cbsettings);
			}
			wxCheckBox *cbqueue{};
			if (queue) {
				cbqueue = new wxCheckBox(this, nullID, _("&Queue"));
				cbqueue->SetValue(1);
				main->Add(cbqueue);
			}
			wxCheckBox *cbfilters{};
			if (filters) {
				cbfilters = new wxCheckBox(this, nullID, _("&Filters"));
				cbfilters->SetValue(1);
				main->Add(cbfilters);
			}

			main->Add(new wxStaticText(this, nullID, _("Please select the categories you would like to import.")));

			lay.createButtonSizerButtons(this, main, false);

			GetSizer()->Fit(this);

			if (ShowModal() != wxID_OK) {
				return;
			}

			if (fz3.IsFromFutureVersion()) {
				wxString msg = wxString::Format(_("The file '%s' has been created by a more recent version of FileZilla.\nLoading files created by newer versions can result in loss of data.\nDo you want to continue?"), fz3.GetFileName());
				if (wxMessageBoxEx(msg, _("Detected newer version of FileZilla"), wxICON_QUESTION | wxYES_NO) != wxYES) {
					return;
				}
			}

			if (cbqueue && cbqueue->IsChecked()) {
				m_pQueueView->ImportQueue(fz3Root.child("Queue"), true);
			}

			if (cbsites && cbsites->IsChecked()) {
				CSiteManager::ImportSites(fz3Root.child("Servers"), options, lim);
			}

			if (cbsettings && cbsettings->IsChecked()) {
				auto settings = fz3Root.child("Settings");
				options.Import(settings);
				wxMessageBoxEx(_("The settings have been imported. You have to restart FileZilla for all settings to have effect."), _("Import successful"), wxOK, this);
			}

			if (cbfilters && cbfilters->IsChecked()) {
				CFilterManager::Import(fz3Root);
			}

			wxMessageBoxEx(_("The selected categories have been imported."), _("Import successful"), wxOK, this);
			return;
		}
	}

	wxMessageBoxEx(_("File does not contain any importable data."), _("Error importing"), wxICON_ERROR, m_parent);
}
