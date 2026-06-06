#include "filezilla.h"
#include "conditionaldialog.h"
#include "dialogex.h"
#include "edithandler.h"
#include "filezillaapp.h"
#include "file_utils.h"
#include "Options.h"
#include "queue.h"
#include "textctrlex.h"
#include "window_state_manager.h"

#include <libfilezilla/file.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/process.hpp>

#include <wx/filedlg.h>
#include <wx/hyperlink.h>
#include <wx/statline.h>

namespace {
Associations LoadAssociations(COptionsBase & options)
{
	Associations ret;

	std::wstring const raw_assocs = options.get_string(OPTION_EDIT_CUSTOMASSOCIATIONS);
	auto assocs = fz::strtok_view(raw_assocs, L"\r\n", true);

	for (std::wstring_view assoc : assocs) {
		std::optional<std::wstring> ext = UnquoteFirst(assoc);
		if (!ext || ext->empty()) {
			continue;
		}

		auto unquoted = UnquoteCommand(assoc);
		if (!unquoted.empty() && !unquoted[0].empty()) {
			ret.emplace(std::move(*ext), std::move(unquoted));
		}
	}

	return ret;

}

void SaveAssociations(Associations const& assocs, COptionsBase & options)
{
	std::wstring quoted;
	for (auto const& assoc : assocs) {
		if (!quoted.empty()) {
			quoted += '\n';
		}

		if (assoc.first.find_first_of(L" \t'\"") != std::wstring::npos) {
			quoted += '"';
			quoted += fz::replaced_substrings(assoc.first, L"\"", L"\"\"");
			quoted += '"';
		}
		else {
			quoted += assoc.first;
		}
		quoted += ' ';
		quoted += QuoteCommand(assoc.second);
	}
	options.set(OPTION_EDIT_CUSTOMASSOCIATIONS, quoted);
}

class CNewAssociationDialog final : protected wxDialogEx
{
public:
	CNewAssociationDialog(wxWindow* parent, COptionsBase & options);
	virtual ~CNewAssociationDialog();

	bool Run(std::wstring const& file);

protected:
	struct impl;
	std::unique_ptr<impl> impl_;

	void SetCtrlState();

	wxWindow* parent_{};
	std::wstring file_;
	std::wstring ext_;

	void OnOK();
	void OnBrowseEditor();
};
}

CEditHandler::CEditHandler(COptionsBase & options)
    : options_(options)
{
	m_timer.Bind(wxEVT_TIMER, [&](wxTimerEvent&) { CheckForModifications(); });
	m_busyTimer.Bind(wxEVT_TIMER, [&](wxTimerEvent&) { CheckForModifications(); });

#ifdef __WXMSW__
	m_lockfile_handle = INVALID_HANDLE_VALUE;
#else
	m_lockfile_descriptor = -1;
#endif
}

void CEditHandler::RemoveTemporaryFiles(std::wstring const& temp)
{
	wxDir dir(temp);
	if (!dir.IsOpened()) {
		return;
	}

	wxString file;
	if (!dir.GetFirst(&file, _T("fz3temp-*"), wxDIR_DIRS)) {
		return;
	}

	wxChar const& sep = wxFileName::GetPathSeparator();
	do {
		if (!m_localDir.empty() && temp + file + sep == m_localDir) {
			// Don't delete own working directory
			continue;
		}

		RemoveTemporaryFilesInSpecificDir((temp + file + sep).ToStdWstring());
	} while (dir.GetNext(&file));
}

void CEditHandler::RemoveTemporaryFilesInSpecificDir(std::wstring const& temp)
{
	std::wstring const lockfile = temp + L"fz3temp-lockfile";
	if (wxFileName::FileExists(lockfile)) {
#ifndef __WXMSW__
		int fd = open(fz::to_string(lockfile).c_str(), O_RDWR | O_CLOEXEC, 0);
		if (fd >= 0) {
			// Try to lock 1 byte region in the lockfile. m_type specifies the byte to lock.
			struct flock f = {};
			f.l_type = F_WRLCK;
			f.l_whence = SEEK_SET;
			f.l_start = 0;
			f.l_len = 1;
			f.l_pid = getpid();
			if (fcntl(fd, F_SETLK, &f)) {
				// In use by other process
				close(fd);
				return;
			}
			close(fd);
		}
#endif
		fz::remove_file(fz::to_native(lockfile), false);

		if (wxFileName::FileExists(lockfile)) {
			return;
		}
	}

	wxLogNull log;

	{
		wxString file;
		wxDir dir(temp);
		bool res;
		for ((res = dir.GetFirst(&file, _T(""), wxDIR_FILES)); res; res = dir.GetNext(&file)) {
			wxRemoveFile(temp + file);
		}
	}

	wxRmdir(temp);

}

std::wstring CEditHandler::GetLocalDirectory()
{
	if (!m_localDir.empty()) {
		return m_localDir;
	}

	wxFileName tmpdir(wxFileName::GetTempDir(), _T(""));
	// Need to call GetLongPath on MSW, GetTempDir can return short path
	// which will cause problems when calculating maximum allowed file
	// length
	wxString dir = tmpdir.GetLongPath();
	if (dir.empty() || !wxFileName::DirExists(dir)) {
		return std::wstring();
	}

	if (dir.Last() != wxFileName::GetPathSeparator()) {
		dir += wxFileName::GetPathSeparator();
	}

	// On POSIX, the permissions of the created directory (700) ensure
	// that this is a safe operation.
	// On Windows, the user's profile directory and associated temp dir
	// already has the correct permissions which get inherited.
	int i = 1;
	do {
		wxString newDir = dir + wxString::Format(_T("fz3temp-%d"), ++i);
		if (wxFileName::FileExists(newDir) || wxFileName::DirExists(newDir)) {
			continue;
		}

		if (!wxMkdir(newDir, 0700)) {
			return std::wstring();
		}

		m_localDir = (newDir + wxFileName::GetPathSeparator()).ToStdWstring();
		break;
	} while (true);

	// Defer deleting stale directories until after having created our own
	// working directory.
	// This avoids some strange errors where freshly deleted directories
	// cannot be instantly recreated.
	RemoveTemporaryFiles(dir.ToStdWstring());

#ifdef __WXMSW__
	m_lockfile_handle = ::CreateFile((m_localDir + L"fz3temp-lockfile").c_str(), GENERIC_WRITE, 0, 0, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, 0);
	if (m_lockfile_handle == INVALID_HANDLE_VALUE) {
		wxRmdir(m_localDir);
		m_localDir.clear();
	}
#else
	auto file = fz::to_native(m_localDir) + "fz3temp-lockfile";
	m_lockfile_descriptor = open(file.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
	if (m_lockfile_descriptor >= 0) {
		// Lock 1 byte region in the lockfile.
		struct flock f = {};
		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = 0;
		f.l_len = 1;
		f.l_pid = getpid();
		fcntl(m_lockfile_descriptor, F_SETLKW, &f);
	}
#endif

	return m_localDir;
}

CEditHandler::~CEditHandler()
{
	if (m_timer.IsRunning()) {
		m_timer.Stop();
	}
	if (m_busyTimer.IsRunning()) {
		m_busyTimer.Stop();
	}

	if (!m_localDir.empty()) {
#ifdef __WXMSW__
		if (m_lockfile_handle != INVALID_HANDLE_VALUE) {
			CloseHandle(m_lockfile_handle);
		}
		wxRemoveFile(m_localDir + _T("fz3temp-lockfile"));
#else
		wxRemoveFile(m_localDir + _T("fz3temp-lockfile"));
		if (m_lockfile_descriptor >= 0) {
			close(m_lockfile_descriptor);
		}
#endif

		wxLogNull log;
		wxRemoveFile(m_localDir + _T("empty_file_yq744zm"));
		RemoveAll(true);
		RemoveTemporaryFilesInSpecificDir(m_localDir);
	}
}

CEditHandler::fileState CEditHandler::GetFileState(std::wstring const& fileName) const
{
	std::list<t_fileData>::const_iterator iter = GetFile(fileName);
	if (iter == m_fileDataList[local].end()) {
		return unknown;
	}

	return iter->state;
}

CEditHandler::fileState CEditHandler::GetFileState(std::wstring const& fileName, CServerPath const& remotePath, Site const& site) const
{
	std::list<t_fileData>::const_iterator iter = GetFile(fileName, remotePath, site);
	if (iter == m_fileDataList[remote].end()) {
		return unknown;
	}

	return iter->state;
}

int CEditHandler::GetFileCount(CEditHandler::fileType type, CEditHandler::fileState state, Site const& site) const
{
	int count = 0;
	if (state == unknown) {
		wxASSERT(!site);
		if (type != remote) {
			count += m_fileDataList[local].size();
		}
		if (type != local) {
			count += m_fileDataList[remote].size();
		}
	}
	else {
		auto f = [state, &site](decltype(m_fileDataList[0]) & items) {
			int cnt = 0;
			for (auto const& data : items) {
				if (data.state != state) {
					continue;
				}

				if (!site || data.site == site) {
					++cnt;
				}
			}
			return cnt;
		};
		if (type != remote) {
			count += f(m_fileDataList[local]);
		}
		if (type != local) {
			count += f(m_fileDataList[remote]);
		}
	}

	return count;
}

bool CEditHandler::AddFile(CEditHandler::fileType type, std::wstring const& localFile, std::wstring const& remoteFile, CServerPath const& remotePath, Site const& site, int64_t size)
{
	if (type == none || !m_pQueue) {
		wxBell();
		return false;
	}

	fileState state;
	if (type == local) {
		state = GetFileState(localFile);
	}
	else {
		state = GetFileState(remoteFile, remotePath, site);
	}

	// It should still be unknown, but due to having displayed dialogs with event loops, something else might have happened so check again just in case.
	if (state != unknown) {
		wxBell();
		return false;
	}

	t_fileData data;
	if (type == remote) {
		data.state = download;
	}
	else {
		data.state = edit;
	}
	data.localFile = localFile;
	data.remoteFile = remoteFile;
	data.remotePath = remotePath;
	data.site = site;
	
	
	if (type == local) {
		bool const launched = LaunchEditor(local, data);

		if (launched && options_.get_bool(OPTION_EDIT_TRACK_LOCAL)) {
			m_fileDataList[type].emplace_back(std::move(data));
		}
		if (!launched) {
			wxMessageBoxEx(wxString::Format(_("The file '%s' could not be opened:\nThe associated command failed"), localFile), _("Opening failed"), wxICON_EXCLAMATION);
		}
		return launched;
	}
	else {
		m_fileDataList[type].emplace_back(std::move(data));

		std::wstring localFileName;
		CLocalPath localPath(localFile, &localFileName);
		if (localFileName == remoteFile) {
			localFileName.clear();
		}
		m_pQueue->QueueFile(false, true, remoteFile, localFileName, localPath, remotePath, site, size, type, QueuePriority::high);
		m_pQueue->QueueFile_Finish(true);
	}

	return true;
}

bool CEditHandler::Remove(std::wstring const& fileName)
{
	std::list<t_fileData>::iterator iter = GetFile(fileName);
	if (iter == m_fileDataList[local].end()) {
		return true;
	}

	wxASSERT(iter->state != upload && iter->state != upload_and_remove);
	if (iter->state == upload || iter->state == upload_and_remove) {
		return false;
	}

	m_fileDataList[local].erase(iter);

	return true;
}

bool CEditHandler::Remove(std::wstring const& fileName, CServerPath const& remotePath, Site const& site)
{
	std::list<t_fileData>::iterator iter = GetFile(fileName, remotePath, site);
	if (iter == m_fileDataList[remote].end()) {
		return true;
	}

	wxASSERT(iter->state != download && iter->state != upload && iter->state != upload_and_remove);
	if (iter->state == download || iter->state == upload || iter->state == upload_and_remove) {
		return false;
	}

	if (wxFileName::FileExists(iter->localFile)) {
		if (!wxRemoveFile(iter->localFile)) {
			iter->state = removing;
			return false;
		}
	}

	m_fileDataList[remote].erase(iter);

	return true;
}

bool CEditHandler::RemoveAll(bool force)
{
	std::list<t_fileData> keep;

	for (std::list<t_fileData>::iterator iter = m_fileDataList[remote].begin(); iter != m_fileDataList[remote].end(); ++iter) {
		if (!force && (iter->state == download || iter->state == upload || iter->state == upload_and_remove)) {
			keep.push_back(*iter);
			continue;
		}

		if (wxFileName::FileExists(iter->localFile)) {
			if (!wxRemoveFile(iter->localFile)) {
				iter->state = removing;
				keep.push_back(*iter);
				continue;
			}
		}
	}
	m_fileDataList[remote].swap(keep);
	keep.clear();

	for (auto iter = m_fileDataList[local].begin(); iter != m_fileDataList[local].end(); ++iter) {
		if (force) {
			continue;
		}

		if (iter->state == upload || iter->state == upload_and_remove) {
			keep.push_back(*iter);
			continue;
		}
	}
	m_fileDataList[local].swap(keep);

	return m_fileDataList[local].empty() && m_fileDataList[remote].empty();
}

bool CEditHandler::RemoveAll(fileState state, Site const& site)
{
	// Others not implemented
	wxASSERT(state == upload_and_remove_failed);
	if (state != upload_and_remove_failed) {
		return false;
	}

	std::list<t_fileData> keep;

	for (auto iter = m_fileDataList[remote].begin(); iter != m_fileDataList[remote].end(); ++iter) {
		if (iter->state != state) {
			keep.push_back(*iter);
			continue;
		}

		if (site && iter->site != site) {
			keep.push_back(*iter);
			continue;
		}

		if (wxFileName::FileExists(iter->localFile)) {
			if (!wxRemoveFile(iter->localFile)) {
				iter->state = removing;
				keep.push_back(*iter);
				continue;
			}
		}
	}
	m_fileDataList[remote].swap(keep);

	return true;
}

std::list<CEditHandler::t_fileData>::iterator CEditHandler::GetFile(std::wstring const& fileName)
{
	std::list<t_fileData>::iterator iter;
	for (iter = m_fileDataList[local].begin(); iter != m_fileDataList[local].end(); ++iter) {
		if (iter->localFile == fileName) {
			break;
		}
	}

	return iter;
}

std::list<CEditHandler::t_fileData>::const_iterator CEditHandler::GetFile(std::wstring const& fileName) const
{
	std::list<t_fileData>::const_iterator iter;
	for (iter = m_fileDataList[local].begin(); iter != m_fileDataList[local].end(); ++iter) {
		if (iter->localFile == fileName) {
			break;
		}
	}

	return iter;
}

std::list<CEditHandler::t_fileData>::iterator CEditHandler::GetFile(std::wstring const& fileName, CServerPath const& remotePath, Site const& site)
{
	std::list<t_fileData>::iterator iter;
	for (iter = m_fileDataList[remote].begin(); iter != m_fileDataList[remote].end(); ++iter) {
		if (iter->remoteFile != fileName) {
			continue;
		}

		if (iter->site != site) {
			continue;
		}

		if (iter->remotePath != remotePath) {
			continue;
		}

		return iter;
	}

	return iter;
}

std::list<CEditHandler::t_fileData>::const_iterator CEditHandler::GetFile(std::wstring const& fileName, CServerPath const& remotePath, Site const& site) const
{
	std::list<t_fileData>::const_iterator iter;
	for (iter = m_fileDataList[remote].begin(); iter != m_fileDataList[remote].end(); ++iter) {
		if (iter->remoteFile != fileName) {
			continue;
		}

		if (iter->site != site) {
			continue;
		}

		if (iter->remotePath != remotePath) {
			continue;
		}

		return iter;
	}

	return iter;
}

void CEditHandler::FinishTransfer(bool, std::wstring const& fileName)
{
	auto iter = GetFile(fileName);
	if (iter == m_fileDataList[local].end()) {
		return;
	}

	wxASSERT(iter->state == upload || iter->state == upload_and_remove);

	switch (iter->state)
	{
	case upload_and_remove:
		m_fileDataList[local].erase(iter);
		break;
	case upload:
		if (wxFileName::FileExists(fileName)) {
			iter->state = edit;
		}
		else {
			m_fileDataList[local].erase(iter);
		}
		break;
	default:
		return;
	}

	SetTimerState();
}

void CEditHandler::FinishTransfer(bool successful, std::wstring const& fileName, CServerPath const& remotePath, Site const& site)
{
	auto iter = GetFile(fileName, remotePath, site);
	if (iter == m_fileDataList[remote].end()) {
		return;
	}

	wxASSERT(iter->state == download || iter->state == upload || iter->state == upload_and_remove);

	switch (iter->state)
	{
	case upload_and_remove:
		if (successful) {
			if (wxFileName::FileExists(iter->localFile) && !wxRemoveFile(iter->localFile)) {
				iter->state = removing;
			}
			else {
				m_fileDataList[remote].erase(iter);
			}
		}
		else {
			if (!wxFileName::FileExists(iter->localFile)) {
				m_fileDataList[remote].erase(iter);
			}
			else {
				iter->state = upload_and_remove_failed;
			}
		}
		break;
	case upload:
		if (wxFileName::FileExists(iter->localFile)) {
			iter->state = edit;
		}
		else {
			m_fileDataList[remote].erase(iter);
		}
		break;
	case download:
		if (wxFileName::FileExists(iter->localFile)) {
			iter->state = edit;
			if (LaunchEditor(remote, *iter)) {
				break;
			}
		}
		if (wxFileName::FileExists(iter->localFile) && !wxRemoveFile(iter->localFile)) {
			iter->state = removing;
		}
		else {
			m_fileDataList[remote].erase(iter);
		}
		break;
	default:
		return;
	}

	SetTimerState();
}

bool CEditHandler::LaunchEditor(std::wstring const& file)
{
	auto iter = GetFile(file);
	if (iter == m_fileDataList[local].end()) {
		return false;
	}

	return LaunchEditor(local, *iter);
}

bool CEditHandler::LaunchEditor(std::wstring const& file, CServerPath const& remotePath, Site const& site)
{
	auto iter = GetFile(file, remotePath, site);
	if (iter == m_fileDataList[remote].end()) {
		return false;
	}

	return LaunchEditor(remote, *iter);
}

bool CEditHandler::LaunchEditor(CEditHandler::fileType type, t_fileData& data)
{
	wxASSERT(type != none);
	wxASSERT(data.state == edit);

	bool is_link;
	if (fz::local_filesys::get_file_info(fz::to_native(data.localFile), is_link, 0, &data.modificationTime, 0) != fz::local_filesys::file) {
		return false;
	}

	auto cmd_with_args = GetAssociation((type == local) ? data.localFile : data.remoteFile);
	if (cmd_with_args.empty() || !ProgramExists(cmd_with_args.front())) {
		return false;
	}
	
	return fz::spawn_detached_process(AssociationToCommand(cmd_with_args, data.localFile));
}

void CEditHandler::CheckForModifications()
{
	CallAfter([&]() { DoCheckForModifications(); });
}

void CEditHandler::DoCheckForModifications()
{
	static bool insideCheckForModifications = false;
	if (insideCheckForModifications) {
		return;
	}
	insideCheckForModifications = true;

	for (int i = 0; i < 2; ++i) {
checkmodifications_loopbegin:
		for (auto iter = m_fileDataList[i].begin(); iter != m_fileDataList[i].end(); ++iter) {
			if (iter->state != edit) {
				continue;
			}

			fz::datetime mtime;
			bool is_link;
			if (fz::local_filesys::get_file_info(fz::to_native(iter->localFile), is_link, 0, &mtime, 0) != fz::local_filesys::file) {
				m_fileDataList[i].erase(iter);

				// Evil goto. Imo the next C++ standard needs a comefrom keyword.
				goto checkmodifications_loopbegin;
			}

			if (mtime.empty()) {
				continue;
			}

			if (!iter->modificationTime.empty() && !iter->modificationTime.compare(mtime)) {
				continue;
			}

			// File has changed, ask user what to do

			m_busyTimer.Stop();
			if (!wxDialogEx::CanShowPopupDialog()) {
				m_busyTimer.Start(1000, true);
				insideCheckForModifications = false;
				return;
			}
			wxTopLevelWindow* pTopWindow = (wxTopLevelWindow*)wxTheApp->GetTopWindow();
			if (pTopWindow && pTopWindow->IsIconized()) {
				pTopWindow->RequestUserAttention(wxUSER_ATTENTION_INFO);
				insideCheckForModifications = false;
				return;
			}

			bool remove;
			int res = DisplayChangeNotification(CEditHandler::fileType(i), *iter, remove);
			if (res == -1) {
				continue;
			}

			if (res == wxID_YES) {
				UploadFile(CEditHandler::fileType(i), iter, remove);
				goto checkmodifications_loopbegin;
			}
			else if (remove && res != wxID_CANCEL) {
				if (i == static_cast<int>(remote)) {
					if (fz::local_filesys::get_file_info(fz::to_native(iter->localFile), is_link, 0, &mtime, 0) != fz::local_filesys::file || wxRemoveFile(iter->localFile)) {
						m_fileDataList[i].erase(iter);
						goto checkmodifications_loopbegin;
					}
					iter->state = removing;
				}
				else {
					m_fileDataList[i].erase(iter);
					goto checkmodifications_loopbegin;
				}
			}
			else if (fz::local_filesys::get_file_info(fz::to_native(iter->localFile), is_link, 0, &mtime, 0) != fz::local_filesys::file) {
				m_fileDataList[i].erase(iter);
				goto checkmodifications_loopbegin;
			}
			else {
				iter->modificationTime = mtime;
			}
		}
	}

	SetTimerState();

	insideCheckForModifications = false;
}

int CEditHandler::DisplayChangeNotification(CEditHandler::fileType type, CEditHandler::t_fileData const& data, bool& remove)
{
	wxDialogEx dlg;

	if (!dlg.Create(wxTheApp->GetTopWindow(), -1, _("File has changed"))) {
		return -1;
	}

	auto& lay = dlg.layout();

	auto main = lay.createMain(&dlg, 1);
	main->AddGrowableCol(0);

	main->Add(new wxStaticText(&dlg, -1, _("A file previously opened has been changed.")));

	auto inner = lay.createFlex(2);
	main->Add(inner);

	inner->Add(new wxStaticText(&dlg, -1, _("Filename:")));
	inner->Add(new wxStaticText(&dlg, -1, LabelEscape((type == local) ? data.localFile : data.remoteFile)));

	if (type == remote) {
		std::wstring file = data.localFile;
		size_t pos = file.rfind(wxFileName::GetPathSeparator());
		if (pos != std::wstring::npos) {
			file = file.substr(pos + 1);
		}
		if (file != data.remoteFile) {
			inner->Add(new wxStaticText(&dlg, -1, _("Opened as:")));
			inner->Add(new wxStaticText(&dlg, -1, LabelEscape(file)));
		}
	}

	inner->Add(new wxStaticText(&dlg, -1, _("Server:")));
	inner->Add(new wxStaticText(&dlg, -1, LabelEscape(data.site.Format(ServerFormat::with_user_and_optional_port))));
	
	inner->Add(new wxStaticText(&dlg, -1, _("Remote path:")));
	inner->Add(new wxStaticText(&dlg, -1, LabelEscape(data.remotePath.GetPath())));

	main->Add(new wxStaticLine(&dlg), lay.grow);

	wxCheckBox* cb{};
	if (type == local) {
		main->Add(new wxStaticText(&dlg, -1, _("Upload this file to the server?")));
		cb = new wxCheckBox(&dlg, -1, _("&Finish editing"));
	}
	else {
		main->Add(new wxStaticText(&dlg, -1, _("Upload this file back to the server?")));
		cb = new wxCheckBox(&dlg, -1, _("&Finish editing and delete local file"));

	}
	main->Add(cb);

	auto buttons = lay.createButtonSizer(&dlg, main, false);
	auto yes = new wxButton(&dlg, wxID_YES, _("&Yes"));
	yes->SetDefault();
	buttons->AddButton(yes);
	auto no = new wxButton(&dlg, wxID_NO, _("&No"));
	buttons->AddButton(no);
	buttons->Realize();

	yes->Bind(wxEVT_BUTTON, [&dlg](wxEvent const&) { dlg.EndDialog(wxID_YES); });
	no->Bind(wxEVT_BUTTON, [&dlg](wxEvent const&) { dlg.EndDialog(wxID_NO); });

	dlg.Layout();
	dlg.GetSizer()->Fit(&dlg);

	int res = dlg.ShowModal();

	remove = cb->IsChecked();

	return res;
}

bool CEditHandler::UploadFile(std::wstring const& file, CServerPath const& remotePath, Site const& site, bool unedit)
{
	std::list<t_fileData>::iterator iter = GetFile(file, remotePath, site);
	return UploadFile(remote, iter, unedit);
}

bool CEditHandler::UploadFile(std::wstring const& file, bool unedit)
{
	std::list<t_fileData>::iterator iter = GetFile(file);
	return UploadFile(local, iter, unedit);
}

bool CEditHandler::UploadFile(fileType type, std::list<t_fileData>::iterator iter, bool unedit)
{
	if (!m_pQueue) {
		wxBell();
		return false;
	}

	wxCHECK(type != none, false);

	if (iter == m_fileDataList[type].end()) {
		return false;
	}

	wxASSERT(iter->state == edit || iter->state == upload_and_remove_failed);
	if (iter->state != edit && iter->state != upload_and_remove_failed) {
		return false;
	}

	iter->state = unedit ? upload_and_remove : upload;

	int64_t size;
	fz::datetime mtime;

	bool is_link;
	if (fz::local_filesys::get_file_info(fz::to_native(iter->localFile), is_link, &size, &mtime, 0) != fz::local_filesys::file) {
		m_fileDataList[type].erase(iter);
		return false;
	}

	if (mtime.empty()) {
		mtime = fz::datetime::now();
	}

	iter->modificationTime = mtime;

	std::wstring file;
	CLocalPath localPath(iter->localFile, &file);
	if (file.empty()) {
		m_fileDataList[type].erase(iter);
		return false;
	}

	m_pQueue->QueueFile(false, false, file, (file == iter->remoteFile) ? std::wstring() : iter->remoteFile, localPath, iter->remotePath, iter->site, size, type, QueuePriority::high);
	m_pQueue->QueueFile_Finish(true);

	return true;
}

void CEditHandler::SetTimerState()
{
	bool editing = GetFileCount(none, edit) != 0;

	if (m_timer.IsRunning()) {
		if (!editing) {
			m_timer.Stop();
		}
	}
	else if (editing) {
		m_timer.Start(10000);
	}
}

std::vector<std::wstring> CEditHandler::CanOpen(std::wstring const& fileName, bool &program_exists)
{
	auto cmd_with_args = GetAssociation(fileName);
	if (cmd_with_args.empty()) {
		return cmd_with_args;
	}

	program_exists = ProgramExists(cmd_with_args.front());

	return cmd_with_args;
}

std::vector<std::wstring> CEditHandler::GetAssociation(std::wstring const& file)
{
	std::vector<std::wstring> ret = GetCustomAssociation(file);
	if (ret.empty() || options_.get_bool(OPTION_EDIT_ALWAYSDEFAULT)) {
		std::wstring command = options_.get_string(OPTION_EDIT_DEFAULTEDITOR);
		if (!command.empty()) {
			if (command[0] == '1') {
				// Text editor
				ret = GetSystemAssociation(L"foo.txt");
			}
			else if (command[0] == '2') {
				ret = UnquoteCommand(std::wstring_view(command).substr(1));
			}
		}
	}

	return ret;
}

std::vector<std::wstring> CEditHandler::GetCustomAssociation(std::wstring_view const& file)
{
	std::vector<std::wstring> ret;

	std::wstring ext = GetExtension(file);
	if (ext.empty()) {
		ext = L"/";
	}

	auto assocs = LoadAssociations(options_);
	auto it = assocs.find(ext);
	if (it != assocs.end()) {
		ret = it->second;
	}
	return ret;
}

std::wstring CEditHandler::GetTemporaryFile(std::wstring name)
{
	name = CQueueView::ReplaceInvalidCharacters(options_, name, true);
#ifdef __WXMSW__
	// MAX_PATH - 1 is theoretical limit, we subtract another 4 to allow
	// editors which create temporary files
	size_t max = MAX_PATH - 5;
#else
	size_t max = std::wstring::npos;
#endif
	if (max != std::wstring::npos) {
		name = TruncateFilename(m_localDir, name, max);
		if (name.empty()) {
			return std::wstring();
		}
	}

	std::wstring file = m_localDir + name;
	if (!FilenameExists(file)) {
		return file;
	}

	if (max != std::wstring::npos) {
		--max;
	}
	int cutoff = 1;
	int n = 1;
	while (++n < 10000) { // Just to give up eventually
		// Further reduce length if needed
		if (max != std::wstring::npos && n >= cutoff) {
			cutoff *= 10;
			--max;
			name = TruncateFilename(m_localDir, name, max);
			if (name.empty()) {
				return std::wstring();
			}
		}

		size_t pos = name.rfind('.');
		if (pos == std::wstring::npos || !pos) {
			file = m_localDir + name + fz::sprintf(L" %d", n);
		}
		else {
			file = m_localDir + name.substr(0, pos) + fz::sprintf(L" %d", n) + name.substr(pos);
		}

		if (!FilenameExists(file)) {
			return file;
		}
	}

	return std::wstring();
}

std::wstring CEditHandler::TruncateFilename(std::wstring const& path, std::wstring const& name, size_t max)
{
	size_t const pathlen = path.size();
	size_t const namelen = name.size();

	if (namelen + pathlen > max) {
		size_t pos = name.rfind('.');
		if (pos != std::wstring::npos) {
			size_t extlen = namelen - pos;
			if (pathlen + extlen >= max)
			{
				// Cannot truncate extension
				return std::wstring();
			}

			return name.substr(0, max - pathlen - extlen) + name.substr(pos);
		}
	}

	return name;
}

bool CEditHandler::FilenameExists(std::wstring const& file)
{
	for (auto const& fileData : m_fileDataList[remote]) {
		// Always ignore case, we don't know which type of filesystem the user profile
		// is installed upon.
		if (!wxString(fileData.localFile).CmpNoCase(file)) {
			return true;
		}
	}

	if (wxFileName::FileExists(file)) {
		// Save to remove, it's not marked as edited anymore.
		{
			wxLogNull log;
			wxRemoveFile(file);
		}

		if (wxFileName::FileExists(file)) {
			return true;
		}
	}

	return false;
}



bool CEditHandler::Edit(CEditHandler::fileType type, std::wstring const& fileName, CServerPath const& path, Site const& site, int64_t size, wxWindow* parent)
{
	std::vector<FileData> data;
	FileData d{fileName, size};
	data.push_back(d);

	return Edit(type, data, path, site, parent);
}

bool CEditHandler::Edit(CEditHandler::fileType type, std::vector<FileData> const& data, CServerPath const& path, Site const& site, wxWindow* parent)
{
	if (type == CEditHandler::remote) {
		std::wstring const& localDir = GetLocalDirectory();
		if (localDir.empty()) {
			wxMessageBoxEx(_("Could not get temporary directory to download file into."), _("Cannot edit file"), wxICON_STOP);
			return false;
		}
	}

	if (data.empty()) {
		wxBell();
		return false;
	}

	if (data.size() > 10) {
		CConditionalDialog dlg(parent, CConditionalDialog::many_selected_for_edit, CConditionalDialog::yesno, options_);
		dlg.SetTitle(_("Confirmation needed"));
		dlg.AddText(_("You have selected more than 10 files for editing, do you really want to continue?"));

		if (!dlg.Run()) {
			return false;
		}
	}

	bool success = true;
	int already_editing_action{};
	for (auto const& file : data) {
		if (!DoEdit(type, file, path, site, parent, data.size(), already_editing_action)) {
			success = false;
		}
	}

	return success;
}

bool CEditHandler::DoEdit(CEditHandler::fileType type, FileData const& file, CServerPath const& path, Site const& site, wxWindow* parent, size_t fileCount, int& already_editing_action)
{
	for (auto const& c : GetExtension(file.name)) {
		if (c < 32 && c != '\t') {
			wxMessageBoxEx(_("Forbidden character in file extension."), _("Cannot view/edit selected file"), wxICON_EXCLAMATION);
			return false;
		}
	}

	// First check whether this file is already being edited
	fileState state;
	if (type == local) {
		state = GetFileState(file.name);
	}
	else {
		state = GetFileState(file.name, path, site);
	}
	switch (state)
	{
	case CEditHandler::download:
	case CEditHandler::upload:
	case CEditHandler::upload_and_remove:
	case CEditHandler::upload_and_remove_failed:
		wxMessageBoxEx(_("A file with that name is already being transferred."), _("Cannot view/edit selected file"), wxICON_EXCLAMATION);
		return false;
	case CEditHandler::removing:
		if (!Remove(file.name, path, site)) {
			wxMessageBoxEx(_("A file with that name is still being edited. Please close it and try again."), _("Selected file is already opened"), wxICON_EXCLAMATION);
			return false;
		}
		break;
	case CEditHandler::edit:
	{
		int action = already_editing_action;
		if (!action) {
			wxDialogEx dlg;
			if (!dlg.Create(parent, -1, _("Selected file already being edited"))) {
				wxBell();
				return false;
			}

			auto& lay = dlg.layout();
			auto main = lay.createMain(&dlg, 1);
			main->AddGrowableCol(0);

			main->Add(new wxStaticText(&dlg, -1, _("The selected file is already being edited:")));
			main->Add(new wxStaticText(&dlg, -1, LabelEscape(file.name)));

			main->AddSpacer(0);

			int choices = options_.get_int(OPTION_PERSISTENT_CHOICES);

			wxRadioButton* reopen{};
			if (type == local) {
				main->Add(new wxStaticText(&dlg, -1, _("Do you want to reopen this file?")));
			}
			else {
				main->Add(new wxStaticText(&dlg, -1, _("Action to perform:")));

				reopen = new wxRadioButton(&dlg, -1, _("&Reopen local file"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
				main->Add(reopen);
				wxRadioButton* retransfer = new wxRadioButton(&dlg, -1, _("&Discard local file then download and edit file anew"));
				main->Add(retransfer);

				if (choices & edit_choices::edit_existing_action) {
					retransfer->SetValue(true);
				}
				else {
					reopen->SetValue(true);
				}
			}

			wxCheckBox* always{};
			if (fileCount > 1) {
				always = new wxCheckBox(&dlg, -1, _("Do the same with &all selected files already being edited"));
				main->Add(always);
				if (choices & edit_choices::edit_existing_always) {
					always->SetValue(true);
				}
			}

			auto buttons = lay.createButtonSizer(&dlg, main, true);

			if (type == remote) {
				auto ok = new wxButton(&dlg, wxID_OK, _("OK"));
				ok->SetDefault();
				buttons->AddButton(ok);
				auto cancel = new wxButton(&dlg, wxID_CANCEL, _("Cancel"));
				buttons->AddButton(cancel);
			}
			else {
				auto yes = new wxButton(&dlg, wxID_YES, _("&Yes"));
				yes->SetDefault();
				buttons->AddButton(yes);
				auto no = new wxButton(&dlg, wxID_NO, _("&No"));
				buttons->AddButton(no);
				yes->Bind(wxEVT_BUTTON, [&dlg](wxEvent const&) { dlg.EndDialog(wxID_YES); });
				no->Bind(wxEVT_BUTTON, [&dlg](wxEvent const&) { dlg.EndDialog(wxID_NO); });
			}
			buttons->Realize();

			dlg.GetSizer()->Fit(&dlg);
			int res = dlg.ShowModal();
			if (res != wxID_OK && res != wxID_YES) {
				wxBell();
				action = -1;
			}
			else if (type == CEditHandler::local || (reopen && reopen->GetValue())) {
				action = 1;
				if (type == CEditHandler::remote) {
					choices &= ~edit_choices::edit_existing_action;
				}
			}
			else {
				action = 2;
				choices |= edit_choices::edit_existing_action;
			}

			if (fileCount > 1) {
				if (always && always->GetValue()) {
					already_editing_action = action;
					choices |= edit_choices::edit_existing_always;
				}
				else {
					choices &= ~edit_choices::edit_existing_always;
				}
			}
			options_.set(OPTION_PERSISTENT_CHOICES, choices);
		}

		if (action == -1) {
			return false;
		}
		else if (action == 1) {
			if (type == CEditHandler::local) {
				LaunchEditor(file.name);
			}
			else {
				LaunchEditor(file.name, path, site);
			}
			return true;
		}
		else {
			if (!Remove(file.name, path, site)) {
				wxMessageBoxEx(_("The selected file is still opened in some other program, please close it."), _("Selected file is still being edited"), wxICON_EXCLAMATION);
				return false;
			}
		}
	}
	break;
	default:
		break;
	}

	// Create local filename if needed
	std::wstring localFile;
	std::wstring remoteFile;
	if (type == fileType::local) {
		localFile = file.name;

		CLocalPath localPath(localFile, &remoteFile);
		if (localPath.empty()) {
			wxBell();
			return false;
		}
	}
	else {
		localFile = GetTemporaryFile(file.name);
		if (localFile.empty()) {
			wxMessageBoxEx(_("Could not create temporary file name."), _("Cannot view/edit selected file"), wxICON_EXCLAMATION);
			return false;
		}
		remoteFile = file.name;
	}


	// Find associated program
	bool program_exists = false;
	std::vector<std::wstring> cmd_with_args;
	if (!wxGetKeyState(WXK_SHIFT) || options_.get_bool(OPTION_EDIT_ALWAYSDEFAULT)) {
		cmd_with_args = CanOpen(file.name, program_exists);
	}
	if (cmd_with_args.empty()) {
		CNewAssociationDialog dlg(parent, options_);
		if (!dlg.Run(file.name)) {
			return false;
		}
		cmd_with_args = CanOpen(file.name, program_exists);
		if (cmd_with_args.empty()) {
			wxMessageBoxEx(wxString::Format(_("The file '%s' could not be opened:\nNo program has been associated on your system with this file type."), file.name), _("Opening failed"), wxICON_EXCLAMATION);
			return false;
		}
	}
	if (!program_exists) {
		wxString msg = wxString::Format(_("The file '%s' cannot be opened:\nThe associated program (%s) could not be found.\nPlease check your filetype associations."), file.name, QuoteCommand(cmd_with_args));
		wxMessageBoxEx(msg, _("Cannot edit file"), wxICON_EXCLAMATION);
		return false;
	}

	// We can proceed with adding the item and either open it or transfer it.
	return AddFile(type, localFile, remoteFile, path, site, file.size);
}


#define COLUMN_NAME 0
#define COLUMN_TYPE 1
#define COLUMN_REMOTEPATH 2
#define COLUMN_STATUS 3

struct CEditHandlerStatusDialog::impl final
{
	impl(COptionsBase& options, CEditHandler* edit_handler)
		: options_(options)
		, editHandler_(edit_handler)
	{}

	wxWindow* parent_{};

	COptionsBase& options_;

	wxListCtrlEx* listCtrl_{};

	wxButton* unedit_{};
	wxButton* upload_{};
	wxButton* upload_and_unedit_{};
	wxButton* open_{};
	CEditHandler* editHandler_{};

	std::unique_ptr<CWindowStateManager> windowStateManager_;
};

CEditHandlerStatusDialog::CEditHandlerStatusDialog(wxWindow* parent, COptionsBase& options, CEditHandler* edit_handler)
	: impl_(std::make_unique<impl>(options, edit_handler))
{
	impl_->parent_ = parent;
}

CEditHandlerStatusDialog::~CEditHandlerStatusDialog()
{
	if (impl_ && impl_->windowStateManager_) {
		impl_->windowStateManager_->Remember(OPTION_EDITSTATUSDIALOG_SIZE);
	}
}

int CEditHandlerStatusDialog::ShowModal()
{
	if (!impl_->editHandler_) {
		return wxID_CANCEL;
	}

	if (!impl_->editHandler_->GetFileCount(CEditHandler::none, CEditHandler::unknown)) {
		wxMessageBoxEx(_("No files are currently being edited."), _("Cannot show dialog"), wxICON_INFORMATION, impl_->parent_);
		return wxID_CANCEL;
	}

	if (!Create(impl_->parent_, -1, _("Files currently being edited"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)) {
		return wxID_CANCEL;
	}

	auto & lay = layout();
	auto main = lay.createMain(this, 1);

	main->Add(new wxStaticText(this, -1, _("The &following files are currently being edited:")));

	impl_->listCtrl_ = new wxListCtrlEx(this, -1, wxDefaultPosition, wxDefaultSize, wxLC_REPORT);
	impl_->listCtrl_->SetFocus();
	main->Add(impl_->listCtrl_, lay.grow);
	main->AddGrowableCol(0);
	main->AddGrowableRow(1);

	impl_->listCtrl_->InsertColumn(0, _("Filename"));
	impl_->listCtrl_->InsertColumn(1, _("Type"));
	impl_->listCtrl_->InsertColumn(2, _("Remote path"));
	impl_->listCtrl_->InsertColumn(3, _("Status"));

	{
		const std::list<CEditHandler::t_fileData>& files = impl_->editHandler_->GetFiles(CEditHandler::remote);
		unsigned int i = 0;
		for (std::list<CEditHandler::t_fileData>::const_iterator iter = files.begin(); iter != files.end(); ++iter, ++i) {
			impl_->listCtrl_->InsertItem(i, iter->remoteFile);
			impl_->listCtrl_->SetItem(i, COLUMN_TYPE, _("Remote"));
			switch (iter->state)
			{
			case CEditHandler::download:
				impl_->listCtrl_->SetItem(i, COLUMN_STATUS, _("Downloading"));
				break;
			case CEditHandler::upload:
				impl_->listCtrl_->SetItem(i, COLUMN_STATUS, _("Uploading"));
				break;
			case CEditHandler::upload_and_remove:
				impl_->listCtrl_->SetItem(i, COLUMN_STATUS, _("Uploading and pending removal"));
				break;
			case CEditHandler::upload_and_remove_failed:
				impl_->listCtrl_->SetItem(i, COLUMN_STATUS, _("Upload failed"));
				break;
			case CEditHandler::removing:
				impl_->listCtrl_->SetItem(i, COLUMN_STATUS, _("Pending removal"));
				break;
			case CEditHandler::edit:
				impl_->listCtrl_->SetItem(i, COLUMN_STATUS, _("Being edited"));
				break;
			default:
				impl_->listCtrl_->SetItem(i, COLUMN_STATUS, _("Unknown"));
				break;
			}
			impl_->listCtrl_->SetItem(i, COLUMN_REMOTEPATH, iter->site.Format(ServerFormat::with_user_and_optional_port) + iter->remotePath.GetPath());
			CEditHandler::t_fileData* pData = new CEditHandler::t_fileData(*iter);
			impl_->listCtrl_->SetItemPtrData(i, (wxUIntPtr)pData);
		}
	}

	{
		const std::list<CEditHandler::t_fileData>& files = impl_->editHandler_->GetFiles(CEditHandler::local);
		unsigned int i = 0;
		for (std::list<CEditHandler::t_fileData>::const_iterator iter = files.begin(); iter != files.end(); ++iter, ++i) {
			impl_->listCtrl_->InsertItem(i, iter->localFile);
			impl_->listCtrl_->SetItem(i, COLUMN_TYPE, _("Local"));
			switch (iter->state)
			{
			case CEditHandler::upload:
				impl_->listCtrl_->SetItem(i, COLUMN_STATUS, _("Uploading"));
				break;
			case CEditHandler::upload_and_remove:
				impl_->listCtrl_->SetItem(i, COLUMN_STATUS, _("Uploading and unediting"));
				break;
			case CEditHandler::edit:
				impl_->listCtrl_->SetItem(i, COLUMN_STATUS, _("Being edited"));
				break;
			default:
				impl_->listCtrl_->SetItem(i, COLUMN_STATUS, _("Unknown"));
				break;
			}
			impl_->listCtrl_->SetItem(i, COLUMN_REMOTEPATH, iter->site.Format(ServerFormat::with_user_and_optional_port) + iter->remotePath.GetPath());
			CEditHandler::t_fileData* pData = new CEditHandler::t_fileData(*iter);
			impl_->listCtrl_->SetItemPtrData(i, (wxUIntPtr)pData);
		}
	}

	for (int i = 0; i < 4; ++i) {
		impl_->listCtrl_->SetColumnWidth(i, wxLIST_AUTOSIZE);
	}
	impl_->listCtrl_->SetMinSize(wxSize(impl_->listCtrl_->GetColumnWidth(0) + impl_->listCtrl_->GetColumnWidth(1) + impl_->listCtrl_->GetColumnWidth(2) + impl_->listCtrl_->GetColumnWidth(3) + lay.dlgUnits(10), impl_->listCtrl_->GetMinSize().GetHeight()));

	auto onsel = [this](wxListEvent const&) { SetCtrlState(); };
	impl_->listCtrl_->Bind(wxEVT_LIST_ITEM_SELECTED, onsel);
	impl_->listCtrl_->Bind(wxEVT_LIST_ITEM_DESELECTED, onsel);

	main->Add(new wxStaticText(this, -1, _("Action on selected file:")));

	auto inner = lay.createGrid(2, 2);
	main->Add(inner, lay.halign);
	
	impl_->unedit_ = new wxButton(this, -1, _("&Unedit"));
	impl_->unedit_->Bind(wxEVT_BUTTON, [this](wxCommandEvent const&) { OnUnedit(); });
	inner->Add(impl_->unedit_, lay.valigng);
	impl_->upload_ = new wxButton(this, -1, _("U&pload"));
	impl_->upload_->Bind(wxEVT_BUTTON, [this](wxCommandEvent const&) { OnUpload(false); });
	inner->Add(impl_->upload_, lay.valigng);
	impl_->upload_and_unedit_ = new wxButton(this, -1, _("Up&load and unedit"));
	impl_->upload_and_unedit_->Bind(wxEVT_BUTTON, [this](wxCommandEvent const&) { OnUpload(true); });
	inner->Add(impl_->upload_and_unedit_, lay.valigng);
	impl_->open_ = new wxButton(this, -1, _("Op&en file"));
	impl_->open_->Bind(wxEVT_BUTTON, [this](wxCommandEvent const&) { OnEdit(); });
	inner->Add(impl_->open_, lay.valigng);


	auto buttons = lay.createButtonSizer(this, main, true);
	auto ok = new wxButton(this, wxID_OK, _("OK"));
	ok->SetDefault();
	buttons->AddButton(ok);
	buttons->Realize();

	GetSizer()->Fit(this);
	SetMinClientSize(GetSizer()->GetMinSize());

	impl_->windowStateManager_ = std::make_unique<CWindowStateManager>(static_cast<wxTopLevelWindow*>(this), impl_->options_);
	impl_->windowStateManager_->Restore(OPTION_EDITSTATUSDIALOG_SIZE, GetSize());

	SetCtrlState();

	int res = wxDialogEx::ShowModal();

	for (int i = 0; i < impl_->listCtrl_->GetItemCount(); ++i) {
		delete (CEditHandler::t_fileData*)impl_->listCtrl_->GetItemData(i);
	}

	return res;
}

void CEditHandlerStatusDialog::SetCtrlState()
{
	bool selectedEdited = false;
	bool selectedOther = false;
	bool selectedUploadRemoveFailed = false;

	int item = -1;
	while ((item = impl_->listCtrl_->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		CEditHandler::fileType type;
		CEditHandler::t_fileData* pData = GetDataFromItem(item, type);
		if (pData->state == CEditHandler::edit) {
			selectedEdited = true;
		}
		else if (pData->state == CEditHandler::upload_and_remove_failed) {
			selectedUploadRemoveFailed = true;
		}
		else {
			selectedOther = true;
		}
	}

	bool const select = selectedEdited && !selectedOther && !selectedUploadRemoveFailed;
	impl_->unedit_->Enable(select || (!selectedOther && selectedUploadRemoveFailed));
	impl_->upload_->Enable(select || (!selectedEdited && !selectedOther && selectedUploadRemoveFailed));
	impl_->upload_and_unedit_->Enable(select || (!selectedEdited && !selectedOther && selectedUploadRemoveFailed));
	impl_->open_->Enable(select);
}

void CEditHandlerStatusDialog::OnUnedit()
{
	std::list<int> files;
	int item = -1;
	while ((item = impl_->listCtrl_->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		impl_->listCtrl_->SetItemState(item, 0, wxLIST_STATE_SELECTED);
		CEditHandler::fileType type;
		CEditHandler::t_fileData* pData = GetDataFromItem(item, type);
		if (pData->state != CEditHandler::edit && pData->state != CEditHandler::upload_and_remove_failed) {
			wxBell();
			return;
		}

		files.push_front(item);
	}

	for (std::list<int>::const_iterator iter = files.begin(); iter != files.end(); ++iter) {
		const int i = *iter;

		CEditHandler::fileType type;
		CEditHandler::t_fileData* pData = GetDataFromItem(i, type);

		if (type == CEditHandler::local) {
			impl_->editHandler_->Remove(pData->localFile);
			delete pData;
			impl_->listCtrl_->DeleteItem(i);
		}
		else {
			if (impl_->editHandler_->Remove(pData->remoteFile, pData->remotePath, pData->site)) {
				delete pData;
				impl_->listCtrl_->DeleteItem(i);
			}
			else {
				impl_->listCtrl_->SetItem(i, COLUMN_STATUS, _("Pending removal"));
			}
		}
	}

	SetCtrlState();
}

void CEditHandlerStatusDialog::OnUpload(bool unedit_after)
{
	std::list<int> files;
	int item = -1;
	while ((item = impl_->listCtrl_->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		impl_->listCtrl_->SetItemState(item, 0, wxLIST_STATE_SELECTED);

		CEditHandler::fileType type;
		CEditHandler::t_fileData* pData = GetDataFromItem(item, type);

		if (pData->state != CEditHandler::edit && pData->state != CEditHandler::upload_and_remove_failed) {
			wxBell();
			return;
		}
		files.push_front(item);
	}

	for (std::list<int>::const_iterator iter = files.begin(); iter != files.end(); ++iter) {
		const int i = *iter;

		CEditHandler::fileType type;
		CEditHandler::t_fileData* pData = GetDataFromItem(i, type);

		bool unedit = unedit_after || pData->state == CEditHandler::upload_and_remove_failed;

		if (type == CEditHandler::local) {
			impl_->editHandler_->UploadFile(pData->localFile, unedit);
		}
		else {
			impl_->editHandler_->UploadFile(pData->remoteFile, pData->remotePath, pData->site, unedit);
		}

		if (!unedit) {
			impl_->listCtrl_->SetItem(i, COLUMN_STATUS, _("Uploading"));
		}
		else if (type == CEditHandler::remote) {
			impl_->listCtrl_->SetItem(i, COLUMN_STATUS, _("Uploading and pending removal"));
		}
		else {
			impl_->listCtrl_->SetItem(i, COLUMN_STATUS, _("Uploading and unediting"));
		}
	}

	SetCtrlState();
}

void CEditHandlerStatusDialog::OnEdit()
{
	std::list<int> files;
	int item = -1;
	while ((item = impl_->listCtrl_->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		impl_->listCtrl_->SetItemState(item, 0, wxLIST_STATE_SELECTED);

		CEditHandler::fileType type;
		CEditHandler::t_fileData* pData = GetDataFromItem(item, type);

		if (pData->state != CEditHandler::edit) {
			wxBell();
			return;
		}
		files.push_front(item);
	}

	for (std::list<int>::const_iterator iter = files.begin(); iter != files.end(); ++iter) {
		const int i = *iter;

		CEditHandler::fileType type;
		CEditHandler::t_fileData* pData = GetDataFromItem(i, type);

		if (type == CEditHandler::local) {
			if (!impl_->editHandler_->LaunchEditor(pData->localFile)) {
				if (impl_->editHandler_->Remove(pData->localFile)) {
					delete pData;
					impl_->listCtrl_->DeleteItem(i);
				}
				else {
					impl_->listCtrl_->SetItem(i, COLUMN_STATUS, _("Pending removal"));
				}
			}
		}
		else {
			if (!impl_->editHandler_->LaunchEditor(pData->remoteFile, pData->remotePath, pData->site)) {
				if (impl_->editHandler_->Remove(pData->remoteFile, pData->remotePath, pData->site)) {
					delete pData;
					impl_->listCtrl_->DeleteItem(i);
				}
				else {
					impl_->listCtrl_->SetItem(i, COLUMN_STATUS, _("Pending removal"));
				}
			}
		}
	}

	SetCtrlState();
}

CEditHandler::t_fileData* CEditHandlerStatusDialog::GetDataFromItem(int item, CEditHandler::fileType &type)
{
	CEditHandler::t_fileData* pData = (CEditHandler::t_fileData*)impl_->listCtrl_->GetItemData(item);
	wxASSERT(pData);

	wxListItem info;
	info.SetMask(wxLIST_MASK_TEXT);
	info.SetId(item);
	info.SetColumn(1);
	impl_->listCtrl_->GetItem(info);
	if (info.GetText() == _("Local")) {
		type = CEditHandler::local;
	}
	else {
		type = CEditHandler::remote;
	}

	return pData;
}

void ShowQuotingRules(wxWindow* parent);

namespace {
struct CNewAssociationDialog::impl
{
	impl(COptionsBase & options)
		: options_(options)
	{}

	COptionsBase & options_;
	wxRadioButton* rbSystem_{};
	wxRadioButton* rbDefault_{};
	wxRadioButton* rbCustom_{};

	wxCheckBox* always_{};

	wxTextCtrlEx* custom_{};
	wxButton* browse_{};
};

CNewAssociationDialog::CNewAssociationDialog(wxWindow *parent, COptionsBase & options)
	: impl_(std::make_unique<impl>(options))
	, parent_(parent)
{
}

CNewAssociationDialog::~CNewAssociationDialog()
{
}

bool CNewAssociationDialog::Run(std::wstring const& file)
{
	file_ = file;

	ext_ = GetExtension(file);

	Create(parent_, -1, _("No program associated with filetype"));

	auto & lay = layout();

	auto * main = lay.createMain(this, 1);

	if (ext_.empty()) {
		main->Add(new wxStaticText(this, -1, _("No program has been associated to edit extensionless files.")));
	}
	else if (ext_ == L".") {
		main->Add(new wxStaticText(this, -1, _("No program has been associated to edit dotfiles.")));
	}
	else {
		main->Add(new wxStaticText(this, -1, wxString::Format(_("No program has been associated to edit files with the extension '%s'."), LabelEscape(ext_))));
	}

	main->Add(new wxStaticText(this, -1, _("Select how these files should be opened.")));

	{
		auto const cmd_with_args = GetSystemAssociation(file);
		if (!cmd_with_args.empty()) {
			impl_->rbSystem_ = new wxRadioButton(this, -1, _("Use system association"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
			impl_->rbSystem_->Bind(wxEVT_RADIOBUTTON, [this](wxEvent const&) { SetCtrlState(); });
			impl_->rbSystem_->SetValue(true);
			main->Add(impl_->rbSystem_);
			main->Add(new wxStaticText(this, -1, _("The default editor for this file type is:") + L" " + LabelEscape(QuoteCommand(cmd_with_args))), 0, wxLEFT, lay.indent);
		}
	}

	{
		auto const cmd_with_args = GetSystemAssociation(L"foo.txt");
		if (!cmd_with_args.empty()) {
			impl_->rbDefault_ = new wxRadioButton(this, -1, _("Use &default editor for text files"), wxDefaultPosition, wxDefaultSize, impl_->rbSystem_ ? 0 : wxRB_GROUP);
			impl_->rbDefault_->Bind(wxEVT_RADIOBUTTON, [this](wxEvent const&) { SetCtrlState(); });
			if (!impl_->rbSystem_) {
				impl_->rbDefault_->SetValue(true);
			}
			main->Add(impl_->rbDefault_);
			main->Add(new wxStaticText(this, -1, _("The default editor for text files is:") + " " + LabelEscape(QuoteCommand(cmd_with_args))), 0, wxLEFT, lay.indent);
			impl_->always_ = new wxCheckBox(this, -1, _("&Always use selection for all unassociated files"));
			main->Add(impl_->always_, 0, wxLEFT, lay.indent);
		}
	}

	impl_->rbCustom_ = new wxRadioButton(this, -1, _("&Use custom program"), wxDefaultPosition, wxDefaultSize, (impl_->rbSystem_ || impl_->rbDefault_) ? 0 : wxRB_GROUP);
	impl_->rbCustom_->Bind(wxEVT_RADIOBUTTON, [this](wxEvent const&) { SetCtrlState(); });
	if (!impl_->rbSystem_ && !impl_->rbDefault_) {
		impl_->rbCustom_->SetValue(true);
	}
	main->Add(impl_->rbCustom_);
	auto row = lay.createFlex(2);
	row->AddGrowableCol(0);
	main->Add(row, 0, wxLEFT|wxGROW, lay.indent);
	
	auto rules = new wxHyperlinkCtrl(this, -1, _("Quoting rules"), wxString());
	main->Add(rules, 0, wxLEFT, lay.indent);
	rules->Bind(wxEVT_HYPERLINK, [this](wxHyperlinkEvent const&) { ShowQuotingRules(this); });


	impl_->custom_ = new wxTextCtrlEx(this, -1, wxString());
	row->Add(impl_->custom_, lay.valigng);
	impl_->browse_ = new wxButton(this, -1, _("&Browse..."));
	impl_->browse_->Bind(wxEVT_BUTTON, [this](wxEvent const&) { OnBrowseEditor(); });
	row->Add(impl_->browse_, lay.valign);

	auto buttons = lay.createButtonSizer(this, main, true);
	auto ok = new wxButton(this, wxID_OK, _("OK"));
	ok->SetDefault();
	buttons->AddButton(ok);
	auto cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
	buttons->AddButton(cancel);
	buttons->Realize();

	ok->Bind(wxEVT_BUTTON, [this](wxEvent const&) { OnOK(); });

	Layout();
	GetSizer()->Fit(this);

	SetCtrlState();

	return ShowModal() == wxID_OK;
}

void CNewAssociationDialog::SetCtrlState()
{
	if (impl_->custom_) {
		impl_->custom_->Enable(impl_->rbCustom_ && impl_->rbCustom_->GetValue());
	}
	if (impl_->browse_) {
		impl_->browse_->Enable(impl_->rbCustom_ && impl_->rbCustom_->GetValue());
	}
	if (impl_->always_) {
		impl_->always_->Enable(impl_->rbDefault_ && impl_->rbDefault_->GetValue());
	}
}

void CNewAssociationDialog::OnOK()
{
	const bool custom = impl_->rbCustom_ && impl_->rbCustom_->GetValue();
	const bool def = impl_->rbDefault_ && impl_->rbDefault_->GetValue();
	const bool always = impl_->always_ && impl_->always_->GetValue();

	if (def && always) {
		impl_->options_.set(OPTION_EDIT_DEFAULTEDITOR, _T("1"));
		EndModal(wxID_OK);

		return;
	}

	std::vector<std::wstring> cmd_with_args;
	if (custom) {
		std::wstring cmd = impl_->custom_->GetValue().ToStdWstring();
		cmd_with_args = UnquoteCommand(cmd);
		if (cmd_with_args.empty()) {
			impl_->custom_->SetFocus();
			wxMessageBoxEx(_("You need to enter a properly quoted command."), _("Cannot set file association"), wxICON_EXCLAMATION);
			return;
		}
		if (!ProgramExists(cmd_with_args.front())) {
			impl_->custom_->SetFocus();
			wxMessageBoxEx(_("Selected editor does not exist."), _("Cannot set file association"), wxICON_EXCLAMATION, this);
			return;
		}
		cmd = QuoteCommand(cmd_with_args);
		impl_->custom_->ChangeValue(cmd);
	}
	else {
		if (def) {
			cmd_with_args = GetSystemAssociation(L"foo.txt");
		}
		else {
			cmd_with_args = GetSystemAssociation(file_);
		}
		if (cmd_with_args.empty()) {
			wxMessageBoxEx(_("The associated program could not be found."), _("Cannot set file association"), wxICON_EXCLAMATION, this);
			return;
		}
	}

	if (ext_.empty()) {
		ext_ = L"/";
	}
	auto associations = LoadAssociations(impl_->options_);
	associations[ext_] = cmd_with_args;
	SaveAssociations(associations, impl_->options_);

	EndModal(wxID_OK);
}

void CNewAssociationDialog::OnBrowseEditor()
{
	wxFileDialog dlg(this, _("Select default editor"), _T(""), _T(""),
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

	if (editor.find_first_of(L" \t'\"") != std::wstring::npos) {
		fz::replace_substrings(editor, L"\"", L"\"\"");
		editor = L"\"" + editor + L"\"";
	}

	impl_->custom_->ChangeValue(editor);
}
}
