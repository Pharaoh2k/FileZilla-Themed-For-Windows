#include "filezilla.h"
#include "export.h"
#include "filezillaapp.h"
#include "xmlfunctions.h"
#include "queue.h"

#include "../commonui/ipcmutex.h"

#include <wx/filedlg.h>

CExportDialog::CExportDialog(wxWindow* parent)
	: m_parent(parent)
{
}

void CExportDialog::Run(COptions& options, CQueueView const* pQueueView)
{
	if (!Create(m_parent, nullID, _("Export settings"))) {
		return;
	}

	auto & lay = layout();
	auto main = lay.createMain(this, 1);

	main->Add(new wxStaticText(this, nullID, _("Select the categories to export:")));
	auto cbsitemanager = new wxCheckBox(this, nullID, _("&Export Site Manager entries"));
	main->Add(cbsitemanager);
	auto cbsettings = new wxCheckBox(this, nullID, _("Export &Settings"));
	main->Add(cbsettings);
	wxCheckBox *cbqueue{};
	if (pQueueView) {
		cbqueue = new wxCheckBox(this, nullID, _("Export &Queue"));
		main->Add(cbqueue);
	}
	auto cbfilters = new wxCheckBox(this, nullID, _("Export &Filters"));
	main->Add(cbfilters);

	lay.createButtonSizerButtons(this, main, false);

	GetSizer()->Fit(this);

	if (ShowModal() != wxID_OK) {
		return;
	}

	bool sitemanager = cbsitemanager->GetValue();
	bool settings = cbsettings->GetValue();
	bool queue = cbqueue && cbqueue->GetValue();
	bool filters = cbfilters->GetValue();

	if (!sitemanager && !settings && !queue && !filters) {
		wxMessageBoxEx(_("No category to export selected"), _("Error exporting settings"), wxICON_ERROR, m_parent);
		return;
	}

	wxString str;
	if (sitemanager && !queue && !settings && !filters) {
		str = _("Select file for exported sites");
	}
	else if (!sitemanager && queue && !settings && !filters) {
		str = _("Select file for exported queue");
	}
	else if (!sitemanager && !queue && settings && !filters) {
		str = _("Select file for exported settings");
	}
	else if (!sitemanager && !queue && !settings && filters) {
		str = _("Select file for exported filters");
	}
	else {
		str = _("Select file for exported data");
	}

	wxFileDialog dlg(m_parent, str, wxString(),
					_T("FileZilla.xml"), _T("XML files (*.xml)|*.xml"),
					wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	CXmlFile xml(dlg.GetPath().ToStdWstring());

	auto exportRoot = xml.CreateEmpty();

	if (sitemanager) {
		CInterProcessMutex mutex(MUTEX_SITEMANAGER);

		CXmlFile file(wxGetApp().GetSettingsFile(_T("sitemanager")));
		auto document = file.Load();
		if (document) {
			auto element = document.child("Servers");
			if (element) {
				exportRoot.append_copy(element);
			}
		}
	}
	if (settings) {
		options.Save();
		CInterProcessMutex mutex(MUTEX_OPTIONS);
		CXmlFile file(wxGetApp().GetSettingsFile(_T("filezilla")));
		auto document = file.Load();
		if (document) {
			auto element = document.child("Settings");
			if (element) {
				exportRoot.append_copy(element);
			}
		}
	}

	if (queue && pQueueView) {
		pQueueView->WriteToFile(exportRoot);
	}

	if (filters) {
		CInterProcessMutex mutex(MUTEX_FILTERS);
		CXmlFile file(wxGetApp().GetSettingsFile(_T("filters")));
		auto document = file.Load();
		if (document) {
			auto element = document.child("Filters");
			if (element) {
				exportRoot.append_copy(element);
			}
			element = document.child("Sets");
			if (element) {
				exportRoot.append_copy(element);
			}
		}
	}

	SaveWithErrorDialog(xml);
}
