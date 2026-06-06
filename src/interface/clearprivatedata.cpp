#include "filezilla.h"
#include "clearprivatedata.h"
#include "commandqueue.h"
#include "local_recursive_operation.h"
#include "Mainfrm.h"
#include "Options.h"
#include "queue.h"
#include "quickconnectbar.h"
#include "recentserverlist.h"
#include "remote_recursive_operation.h"
#include "state.h"

#include "../commonui/ipcmutex.h"

#include <libfilezilla/file.hpp>

#include <wx/statbox.h>

BEGIN_EVENT_TABLE(CClearPrivateDataDialog, wxDialogEx)
EVT_TIMER(wxID_ANY, CClearPrivateDataDialog::OnTimer)
END_EVENT_TABLE()

CClearPrivateDataDialog::CClearPrivateDataDialog(CMainFrame* pMainFrame)
	: m_pMainFrame(pMainFrame)
{
}

void CClearPrivateDataDialog::Run()
{
	if (!wxDialogEx::Create(nullptr, nullID, _("Clear private data"))) {
		return;
	}

	auto & lay = layout();
	auto main = lay.createMain(this, 1);

	main->Add(new wxStaticText(this, nullID, _("Select the private data you would like to delete.")));

	auto [box, inner] = lay.createStatBox(main, _("Categories to clear"), 1);

	auto clearQuickconnect = new wxCheckBox(box, nullID, _("&Quickconnect history"));
	inner->Add(clearQuickconnect);
	auto clearReconnect = new wxCheckBox(box, nullID, _("&Reconnect information"));
	inner->Add(clearReconnect);
	auto clearSitemanager = new wxCheckBox(box, nullID, _("&Site Manager entries"));
	inner->Add(clearSitemanager);
	auto clearQueue = new wxCheckBox(box, nullID, _("&Transfer queue"));
	inner->Add(clearQueue);

	auto buttons = lay.createButtonSizer(this, main, false);

	auto ok = new wxButton(this, wxID_OK, _("&OK"));
	ok->SetDefault();
	buttons->AddButton(ok);

	auto cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
	buttons->AddButton(cancel);

	buttons->Realize();

	if (ShowModal() != wxID_OK) {
		return;
	}

	if (clearSitemanager->GetValue() && clearQueue->GetValue()) {
		int res = wxMessageBoxEx(_("Do you really want to delete all Site Manager entries and the transfer queue?"), _("Clear private data"), wxYES | wxNO | wxICON_QUESTION);
		if (res != wxYES) {
			return;
		}
	}
	else if (clearQueue->GetValue()) {
		int res = wxMessageBoxEx(_("Do you really want to delete the transfer queue?"), _("Clear private data"), wxYES | wxNO | wxICON_QUESTION);
		if (res != wxYES) {
			return;
		}
	}
	else if (clearSitemanager->GetValue()) {
		int res = wxMessageBoxEx(_("Do you really want to delete all Site Manager entries?"), _("Clear private data"), wxYES | wxNO | wxICON_QUESTION);
		if (res != wxYES) {
			return;
		}
	}

	if (clearQuickconnect->GetValue()) {
		CRecentServerList::Clear();
		if (m_pMainFrame->GetQuickconnectBar()) {
			m_pMainFrame->GetQuickconnectBar()->ClearFields();
		}
	}

	if (clearReconnect->GetValue()) {
		bool asked = false;

		const std::vector<CState*> *states = CContextManager::Get()->GetAllStates();

		for (std::vector<CState*>::const_iterator iter = states->begin(); iter != states->end(); ++iter) {
			CState* pState = *iter;
			if (pState->IsRemoteConnected() || !pState->IsRemoteIdle()) {
				if (!asked) {
					int res = wxMessageBoxEx(_("Reconnect information cannot be cleared while connected to a server.\nIf you continue, your connection will be disconnected."), _("Clear private data"), wxOK | wxCANCEL);
					if (res != wxOK) {
						return;
					}
					asked = true;
				}

				pState->GetLocalRecursiveOperation()->StopRecursiveOperation();
				pState->GetRemoteRecursiveOperation()->StopRecursiveOperation();
				if (!pState->m_pCommandQueue->Cancel()) {
					m_timer.SetOwner(this);
					m_timer.Start(250, true);
				}
				else {
					pState->Disconnect();
				}
			}
		}

		// Doesn't harm to do it now, but has to be repeated later just to be safe
		ClearReconnect();
	}

	if (clearSitemanager->GetValue()) {
		CInterProcessMutex sitemanagerMutex(MUTEX_SITEMANAGERGLOBAL, false);
		while (sitemanagerMutex.TryLock() == 0) {
			int res = wxMessageBoxEx(_("The Site Manager is opened in another instance of FileZilla 3.\nPlease close it or the data cannot be deleted."), _("Clear private data"), wxOK | wxCANCEL);
			if (res != wxYES) {
				return;
			}
		}
		CInterProcessMutex mutex(MUTEX_SITEMANAGER);
		RemoveXmlFile(L"sitemanager");
	}

	if (clearQueue->GetValue()) {
		m_pMainFrame->GetQueue()->SetActive(false);
		m_pMainFrame->GetQueue()->RemoveAll();
	}
}

void CClearPrivateDataDialog::OnTimer(wxTimerEvent&)
{
	const std::vector<CState*> *states = CContextManager::Get()->GetAllStates();

	for (std::vector<CState*>::const_iterator iter = states->begin(); iter != states->end(); ++iter) {
		CState* pState = *iter;

		if (pState->IsRemoteConnected() || !pState->IsRemoteIdle()) {
			if (!pState->m_pCommandQueue->Cancel()) {
				return;
			}

			pState->Disconnect();
		}

		if (pState->IsRemoteConnected() || !pState->IsRemoteIdle()) {
			return;
		}
	}

	m_timer.Stop();
	ClearReconnect();
	Delete();
}

void CClearPrivateDataDialog::Delete()
{
	if (m_timer.IsRunning()) {
		return;
	}

	Destroy();
}

bool CClearPrivateDataDialog::ClearReconnect()
{
	m_pMainFrame->GetOptions().Cleanup();
	m_pMainFrame->GetOptions().Save();

	const std::vector<CState*> *states = CContextManager::Get()->GetAllStates();
	for (std::vector<CState*>::const_iterator iter = states->begin(); iter != states->end(); ++iter) {
		CState* pState = *iter;
		if (pState) {
			pState->SetLastSite(Site(), CServerPath());
		}
	}

	return true;
}

void CClearPrivateDataDialog::RemoveXmlFile(std::wstring const& name)
{
	std::wstring const path = m_pMainFrame->GetOptions().get_string(OPTION_DEFAULT_SETTINGSDIR);
	if (!name.empty() && !path.empty()) {
		fz::remove_file(fz::to_native(path + name + L".xml"), false);
		fz::remove_file(fz::to_native(path + name + L".xml~"), false);
	}
}
