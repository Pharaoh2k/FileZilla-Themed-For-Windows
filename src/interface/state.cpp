#include "filezilla.h"
#include "state.h"
#include "commandqueue.h"
#include "Options.h"
#include "Mainfrm.h"
#include "queue.h"
#include "filezillaapp.h"
#include "local_recursive_operation.h"
#include "remote_recursive_operation.h"
#include "listingcomparison.h"
#include "xrc_helper.h"
#include "file_utils.h"

#include "../commonui/misc.h"

#include "../include/FileZillaEngine.h"

#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/glue/wx.hpp>
#include <libfilezilla/glue/wxinvoker.hpp>

#include <algorithm>

bool CStateFilterManager::FilenameFiltered(std::wstring const& name, std::wstring const& path, bool dir, int64_t size, bool local, int attributes, fz::datetime const& date) const
{
	if (local) {
		if (m_localFilter && FilenameFilteredByFilter(m_localFilter, name, path, dir, size, attributes, date)) {
			return true;
		}
	}
	else {
		if (m_remoteFilter && FilenameFilteredByFilter(m_remoteFilter, name, path, dir, size, attributes, date)) {
			return true;
		}
	}

	return CFilterManager::FilenameFiltered(name, path, dir, size, local, attributes, date);
}

CContextManager CContextManager::m_the_context_manager;

CContextManager::CContextManager()
{
	m_current_context = -1;
}

CContextManager* CContextManager::Get()
{
	return &m_the_context_manager;
}

CState* CContextManager::CreateState(CMainFrame &mainFrame)
{
	CState* pState = new CState(mainFrame);

	m_contexts.push_back(pState);

	NotifyHandlers(pState, STATECHANGE_NEWCONTEXT, _T(""), 0);

	if (!pState->CreateEngine()) {
		DestroyState(pState);
		return nullptr;
	}

	return pState;
}

void CContextManager::DestroyState(CState* pState)
{
	for (unsigned int i = 0; i < m_contexts.size(); ++i) {
		if (m_contexts[i] != pState) {
			continue;
		}

		m_contexts.erase(m_contexts.begin() + i);
		if ((int)i < m_current_context) {
			--m_current_context;
		}
		else if ((int)i == m_current_context) {
			if (i >= m_contexts.size()) {
				--m_current_context;
			}
			NotifyHandlers(GetCurrentContext(), STATECHANGE_CHANGEDCONTEXT, _T(""), 0);
		}

		break;
	}

	NotifyHandlers(pState, STATECHANGE_REMOVECONTEXT, _T(""), 0);
	delete pState;
}

void CContextManager::SetCurrentContext(CState* pState)
{
	if (GetCurrentContext() == pState) {
		return;
	}

	for (unsigned int i = 0; i < m_contexts.size(); ++i) {
		if (m_contexts[i] != pState) {
			continue;
		}

		m_current_context = i;
		NotifyHandlers(GetCurrentContext(), STATECHANGE_CHANGEDCONTEXT, _T(""), 0);
	}
}

void CContextManager::DestroyAllStates()
{
	m_current_context = -1;
	NotifyHandlers(GetCurrentContext(), STATECHANGE_CHANGEDCONTEXT, _T(""), 0);

	while (!m_contexts.empty()) {
		CState* pState = m_contexts.back();
		m_contexts.pop_back();

		NotifyHandlers(pState, STATECHANGE_REMOVECONTEXT, _T(""), 0);
		delete pState;
	}
}

void CContextManager::RegisterHandler(CGlobalStateEventHandler* pHandler, t_statechange_notifications notification, bool current_only)
{
	wxASSERT(pHandler);
	wxASSERT(notification != STATECHANGE_MAX && notification != STATECHANGE_NONE);

	auto &handlers = m_handlers[notification];
	for (auto const& it : handlers) {
		if (it.pHandler == pHandler) {
			return;
		}
	}

	t_handler handler;
	handler.pHandler = pHandler;
	handler.current_only = current_only;
	handlers.push_back(handler);
}

void CContextManager::UnregisterHandler(CGlobalStateEventHandler* pHandler, t_statechange_notifications notification)
{
	wxASSERT(pHandler);
	wxASSERT(notification != STATECHANGE_MAX);

	if (notification == STATECHANGE_NONE) {
		for (int i = 0; i < STATECHANGE_MAX; ++i) {
			auto &handlers = m_handlers[i];
			for (auto iter = handlers.begin(); iter != handlers.end(); ++iter) {
				if (iter->pHandler == pHandler) {
					handlers.erase(iter);
					break;
				}
			}
		}
	}
	else {
		auto &handlers = m_handlers[notification];
		for (auto iter = handlers.begin(); iter != handlers.end(); ++iter) {
			if (iter->pHandler == pHandler) {
				handlers.erase(iter);
				return;
			}
		}
	}
}

size_t CContextManager::HandlerCount(t_statechange_notifications notification) const
{
	wxASSERT(notification != STATECHANGE_NONE && notification != STATECHANGE_MAX);
	return m_handlers[notification].size();
}

void CContextManager::NotifyHandlers(CState* pState, t_statechange_notifications notification, std::wstring const& data, void const* data2)
{
	wxASSERT(notification != STATECHANGE_NONE && notification != STATECHANGE_MAX);

	auto const& handlers = m_handlers[notification];
	for (auto const& handler : handlers) {
		if (handler.current_only && pState != GetCurrentContext()) {
			continue;
		}

		handler.pHandler->OnStateChange(pState, notification, data, data2);
	}
}

CState* CContextManager::GetCurrentContext()
{
	if (m_current_context == -1) {
		return 0;
	}

	return m_contexts[m_current_context];
}

void CContextManager::NotifyAllHandlers(t_statechange_notifications notification, std::wstring const& data, void const* data2)
{
	for (auto const& context : m_contexts) {
		context->NotifyHandlers(notification, data, data2);
	}
}

void CContextManager::NotifyGlobalHandlers(t_statechange_notifications notification, std::wstring const& data, void const* data2)
{
	auto const& handlers = m_handlers[notification];
	for (auto const& handler : handlers) {
		handler.pHandler->OnStateChange(0, notification, data, data2);
	}
}

void CContextManager::ProcessDirectoryListing(CServer const& server, std::shared_ptr<CDirectoryListing> const& listing, CState const* exempt)
{
	for (auto state : m_contexts) {
		if (state == exempt) {
			continue;
		}
		if (state->GetSite() && state->GetSite().server == server) {
			state->SetRemoteDir(listing, false);
		}
	}
}

CState::CState(CMainFrame &mainFrame)
	: pool_(mainFrame.GetEngineContext().GetThreadPool())
	, m_mainFrame(mainFrame)
{
	m_title = _("Not connected");

	m_pComparisonManager = new CComparisonManager(*this, m_mainFrame.GetOptions());

	m_pLocalRecursiveOperation = new CLocalRecursiveOperation(*this);
	m_pRemoteRecursiveOperation = new CRemoteRecursiveOperation(m_mainFrame.GetOptions(), *this);

	m_localDir.SetPath(std::wstring(1, CLocalPath::path_separator));
}

CState::~CState()
{
	delete m_pComparisonManager;
	delete m_pCommandQueue;
	engine_.reset();
	delete m_pLocalRecursiveOperation;
	delete m_pRemoteRecursiveOperation;

	// Unregister all handlers
	for (int i = 0; i < STATECHANGE_MAX; ++i) {
		wxASSERT(m_handlers[i].handlers.empty());
	}
}

CLocalPath CState::GetLocalDir() const
{
	return m_localDir;
}


bool CState::SetLocalDir(std::wstring const& dir, std::wstring *error, bool rememberPreviousSubdir)
{
	CLocalPath p(m_localDir);
#ifdef __WXMSW__
	if (dir == _T("..") && !p.HasParent() && p.HasLogicalParent()) {
		// Parent of C:\ is drive list
		if (!p.MakeParent()) {
			return false;
		}
	}
	else
#endif
	if (!p.ChangePath(dir)) {
		return false;
	}

	return DoSetLocalDir(p, error, rememberPreviousSubdir);
}

bool CState::SetLocalDir(CLocalPath const& dir, std::wstring *error, bool rememberPreviousSubdir)
{
	CLocalPath p(dir);
	return DoSetLocalDir(p, error, rememberPreviousSubdir);
}

bool CState::DoSetLocalDir(CLocalPath & dir, std::wstring *error, bool rememberPreviousSubdir)
{
	if (m_changeDirFlags.syncbrowse) {
		wxMessageBoxEx(_("Cannot change directory, there already is a synchronized browsing operation in progress."), _("Synchronized browsing"));
		return false;
	}

	size_t links{};
	while (++links < 5) {
		bool is_link{};
		if (!dir.Exists(error, &is_link)) {
			return false;
		}
		if (!is_link || !m_mainFrame.GetOptions().get_bool(OPTION_RESOLVE_LOCAL_SYMLINKS)) {
			break;
		}
		auto path = dir.GetPath();
		auto target = fz::local_filesys::get_link_target(fz::to_native(path));
#ifdef FZ_WINDOWS
		if (fz::starts_with(target, L"\\??\\Volume{")) {
			break;
		}
#endif
		dir.MakeParent();
		if (!dir.ChangePath(fz::to_wstring(target))) {
			return false;
		}
	}
	if (links >= 5) {
		if (error) {
			*error = _("Too many symbolic links");
		}
		return false;
	}

	if (!m_sync_browse.local_root.empty()) {
		wxASSERT(m_site);

		if (dir != m_sync_browse.local_root && !dir.IsSubdirOf(m_sync_browse.local_root)) {
			wxString msg = wxString::Format(_("The local directory '%s' is not below the synchronization root (%s).\nDisable synchronized browsing and continue changing the local directory?"),
					dir.GetPath(),
					m_sync_browse.local_root.GetPath());
			if (wxMessageBoxEx(msg, _("Synchronized browsing"), wxICON_QUESTION | wxYES_NO) != wxYES) {
				return false;
			}
			SetSyncBrowse(false);
		}
		else if (!IsRemoteIdle(true)) {
			wxString msg(_("A remote operation is in progress and synchronized browsing is enabled.\nDisable synchronized browsing and continue changing the local directory?"));
			if (wxMessageBoxEx(msg, _("Synchronized browsing"), wxICON_QUESTION | wxYES_NO) != wxYES) {
				return false;
			}
			SetSyncBrowse(false);
		}
		else {
			CServerPath remote_path = GetSynchronizedDirectory(dir);
			if (remote_path.empty()) {
				SetSyncBrowse(false);
				wxString msg = wxString::Format(_("Could not obtain corresponding remote directory for the local directory '%s'.\nSynchronized browsing has been disabled."),
					dir.GetPath());
				wxMessageBoxEx(msg, _("Synchronized browsing"));
				return false;
			}

			m_changeDirFlags.syncbrowse = true;
			m_changeDirFlags.compare = m_pComparisonManager->IsComparing();
			m_sync_browse.target_path = remote_path;
			CListCommand *pCommand = new CListCommand(remote_path);
			m_pCommandQueue->ProcessCommand(pCommand);

			return true;
		}
	}

	if (dir == m_localDir.GetParent() && rememberPreviousSubdir) {
#ifdef __WXMSW__
		if (dir.GetPath() == _T("\\")) {
			m_previouslyVisitedLocalSubdir = m_localDir.GetPath();
			m_previouslyVisitedLocalSubdir.pop_back();
		}
		else
#endif
		{
			m_previouslyVisitedLocalSubdir = m_localDir.GetLastSegment();
		}
	}
	else {
		m_previouslyVisitedLocalSubdir.clear();
	}


	m_localDir = dir;

	NotifyHandlers(STATECHANGE_LOCAL_DIR);

	return true;
}

bool CState::SetRemoteDir(std::shared_ptr<CDirectoryListing> const& pDirectoryListing, bool primary)
{
	if (!pDirectoryListing) {
		m_changeDirFlags.compare = false;
		SetSyncBrowse(false);
		if (!primary) {
			return false;
		}

		if (m_pDirectoryListing) {
			m_pDirectoryListing = 0;
			NotifyHandlers(STATECHANGE_REMOTE_DIR, std::wstring(), &primary);
		}
		m_previouslyVisitedRemoteSubdir.clear();
		return true;
	}

	wxASSERT(pDirectoryListing->m_firstListTime);

	if (pDirectoryListing && m_pDirectoryListing &&
		pDirectoryListing->path == m_pDirectoryListing->path.GetParent())
	{
		m_previouslyVisitedRemoteSubdir = m_pDirectoryListing->path.GetLastSegment();
	}
	else {
		m_previouslyVisitedRemoteSubdir.clear();
	}

	if (!primary) {
		if (!m_pDirectoryListing || m_pDirectoryListing->path != pDirectoryListing->path) {
			// We aren't interested in these listings
			return true;
		}
	}
	else {
		m_last_path = pDirectoryListing->path;
	}

	if (m_pDirectoryListing && m_pDirectoryListing->path == pDirectoryListing->path &&
		pDirectoryListing->failed())
	{
		// We still got an old listing, no need to display the new one
		return true;
	}

	m_pDirectoryListing = pDirectoryListing;

	NotifyHandlers(STATECHANGE_REMOTE_DIR, std::wstring(), &primary);

	bool compare = m_changeDirFlags.compare;
	if (primary) {
		m_changeDirFlags.compare = false;
	}

	if (m_changeDirFlags.syncbrowse && primary) {
		m_changeDirFlags.syncbrowse = false;
		if (m_pDirectoryListing->path != m_sync_browse.remote_root && !m_pDirectoryListing->path.IsSubdirOf(m_sync_browse.remote_root, false)) {
			SetSyncBrowse(false);
			wxString msg = wxString::Format(_("Current remote directory (%s) is not below the synchronization root (%s).\nSynchronized browsing has been disabled."),
					m_pDirectoryListing->path.GetPath(),
					m_sync_browse.remote_root.GetPath());
			wxMessageBoxEx(msg, _("Synchronized browsing"));
		}
		else {
			CLocalPath local_path = GetSynchronizedDirectory(m_pDirectoryListing->path);
			if (local_path.empty()) {
				SetSyncBrowse(false);
				wxString msg = wxString::Format(_("Could not obtain corresponding local directory for the remote directory '%s'.\nSynchronized browsing has been disabled."),
					m_pDirectoryListing->path.GetPath());
				wxMessageBoxEx(msg, _("Synchronized browsing"));
				compare = false;
			}
			else {
				std::wstring error;
				if (!local_path.Exists(&error)) {
					SetSyncBrowse(false);
					wxString msg = error + _T("\n") + _("Synchronized browsing has been disabled.");
					wxMessageBoxEx(msg, _("Synchronized browsing"));
					compare = false;
				}
				else {
					m_localDir = local_path;

					NotifyHandlers(STATECHANGE_LOCAL_DIR);
				}
			}
		}
	}

	if (compare && !m_pComparisonManager->IsComparing()) {
		m_pComparisonManager->CompareListings();
	}

	return true;
}

std::shared_ptr<CDirectoryListing> CState::GetRemoteDir() const
{
	return m_pDirectoryListing;
}

const CServerPath CState::GetRemotePath() const
{
	if (!m_pDirectoryListing) {
		return CServerPath();
	}

	return m_pDirectoryListing->path;
}

void CState::RefreshLocal()
{
	NotifyHandlers(STATECHANGE_LOCAL_DIR);
}

void CState::RefreshLocalFile(std::wstring const& file)
{
	std::wstring file_name;
	CLocalPath path(file, &file_name);
	if (path.empty()) {
		return;
	}

	if (file_name.empty()) {
		if (!path.HasParent()) {
			return;
		}
		path.MakeParent(&file_name);
		wxASSERT(!file_name.empty());
	}

	if (path != m_localDir) {
		return;
	}

	NotifyHandlers(STATECHANGE_LOCAL_REFRESH_FILE, file_name);
}

void CState::LocalDirCreated(const CLocalPath& path)
{
	if (!path.IsSubdirOf(m_localDir)) {
		return;
	}

	std::wstring next_segment = path.GetPath().substr(m_localDir.GetPath().size());
	size_t pos = next_segment.find(CLocalPath::path_separator);
	if (pos == std::wstring::npos || !pos) {
		// Shouldn't ever come true
		return;
	}

	// Current local path is /foo/
	// Called with /foo/bar/baz/
	// -> Refresh /foo/bar/
	next_segment = next_segment.substr(0, pos);
	NotifyHandlers(STATECHANGE_LOCAL_REFRESH_FILE, next_segment);
}

void CState::SetSite(Site const& site, CServerPath const& path)
{
	if (m_site) {
		if (site == m_site) {
			// Nothing changes, other than possibly credentials
			m_site = site;
			return;
		}

		SetRemoteDir(nullptr, true);
		tls_session_info_.reset();
		ssh_session_info_.reset();
	}
	if (site) {
		if (!path.empty()) {
			m_last_path = path;
		}
		else if (m_last_site.server != site.server) {
			m_last_path.clear();
		}
		m_last_site = site;
	}

	m_site = site;

	UpdateTitle();

	m_successful_connect = false;

	NotifyHandlers(STATECHANGE_SERVER);
}

Site const& CState::GetSite() const
{
	return m_site;
}

wxString CState::GetTitle() const
{
	return m_title;
}

bool CState::Connect(Site const& site, CServerPath const& path, bool compare)
{
	if (!site) {
		return false;
	}
	if (!engine_) {
		return false;
	}
	if (engine_->IsConnected() || engine_->IsBusy() || !m_pCommandQueue->Idle()) {
		m_pCommandQueue->Cancel();
	}
	m_pRemoteRecursiveOperation->StopRecursiveOperation();
	SetSyncBrowse(false);
	m_changeDirFlags.compare = compare;

	SetSite(site, path);

	// Use m_site from here on
	m_pCommandQueue->ProcessCommand(new CConnectCommand(m_site.server, m_site.Handle(), m_site.credentials));
	m_pCommandQueue->ProcessCommand(new CListCommand(path, std::wstring(), LIST_FLAG_FALLBACK_CURRENT));

	return true;
}

bool CState::Disconnect()
{
	if (!engine_) {
		return false;
	}

	if (!IsRemoteConnected()) {
		return true;
	}

	if (!IsRemoteIdle()) {
		return false;
	}

	SetSite(Site());
	m_pCommandQueue->ProcessCommand(new CDisconnectCommand());

	return true;
}

bool CState::CreateEngine()
{
	wxASSERT(!engine_);
	if (engine_) {
		return true;
	}

	engine_ = std::make_unique<CFileZillaEngine>(m_mainFrame.GetEngineContext(), fz::make_invoker(m_mainFrame, [frame = &m_mainFrame](CFileZillaEngine* engine){ frame->OnEngineEvent(engine); }));

	m_pCommandQueue = new CCommandQueue(engine_.get(), &m_mainFrame, *this);

	return true;
}

void CState::DestroyEngine()
{
	delete m_pCommandQueue;
	m_pCommandQueue = 0;
	engine_.reset();
}

void CState::RegisterHandler(CStateEventHandler* pHandler, t_statechange_notifications notification, CStateEventHandler* insertBefore)
{
	wxASSERT(pHandler);
	wxASSERT(&pHandler->m_state == this);
	if (!pHandler || &pHandler->m_state != this) {
		return;
	}
	wxASSERT(notification != STATECHANGE_MAX && notification != STATECHANGE_NONE);
	wxASSERT(pHandler != insertBefore);


	auto &handlers = m_handlers[notification];

	wxASSERT(!insertBefore || !handlers.inNotify_);

	auto insertionPoint = handlers.handlers.end();

	for (auto it = handlers.handlers.begin(); it != handlers.handlers.end(); ++it) {
		if (*it == insertBefore) {
			insertionPoint = it;
		}
		if (*it == pHandler) {
			wxASSERT(insertionPoint == handlers.handlers.end());
			return;
		}
	}

	handlers.handlers.insert(insertionPoint, pHandler);
}

void CState::UnregisterHandler(CStateEventHandler* pHandler, t_statechange_notifications notification)
{
	wxASSERT(pHandler);
	wxASSERT(notification != STATECHANGE_MAX);

	if (notification == STATECHANGE_NONE) {
		for (int i = 0; i < STATECHANGE_MAX; ++i) {
			auto &handlers = m_handlers[i];
			for (auto iter = handlers.handlers.begin(); iter != handlers.handlers.end(); ++iter) {
				if (*iter == pHandler) {
					if (handlers.inNotify_) {
						handlers.compact_ = true;
						*iter = 0;
					}
					else {
						handlers.handlers.erase(iter);
					}
					break;
				}
			}
		}
	}
	else {
		auto &handlers = m_handlers[notification];
		for (auto iter = handlers.handlers.begin(); iter != handlers.handlers.end(); ++iter) {
			if (*iter == pHandler) {
				if (handlers.inNotify_) {
					handlers.compact_ = true;
					*iter = 0;
				}
				else {
					handlers.handlers.erase(iter);
				}
				return;
			}
		}
	}
}

void CState::NotifyHandlers(t_statechange_notifications notification, std::wstring const& data, const void* data2)
{
	wxASSERT(notification != STATECHANGE_NONE && notification != STATECHANGE_MAX);

	auto & handlers = m_handlers[notification];

	wxASSERT(!handlers.inNotify_);

	handlers.inNotify_ = true;
	// Can't use iterators as inserting a handler might invalidate them
	for (size_t i = 0; i < handlers.handlers.size(); ++i) {
		if (handlers.handlers[i]) {
			handlers.handlers[i]->OnStateChange(notification, data, data2);
		}
	}

	if (handlers.compact_) {
		handlers.handlers.erase(std::remove(handlers.handlers.begin(), handlers.handlers.end(), nullptr), handlers.handlers.end());
		handlers.compact_ = false;
	}

	handlers.inNotify_ = false;

	CContextManager::Get()->NotifyHandlers(this, notification, data, data2);
}

CStateEventHandler::CStateEventHandler(CState &state)
	: m_state(state)
{
}

CStateEventHandler::~CStateEventHandler()
{
	m_state.UnregisterHandler(this, STATECHANGE_NONE);
}

CGlobalStateEventHandler::~CGlobalStateEventHandler()
{
	CContextManager::Get()->UnregisterHandler(this, STATECHANGE_NONE);
}

void CState::UploadDroppedFiles(CLocalDataObject const* pLocalDataObject, std::wstring const& subdir, bool queueOnly)
{
	if (!m_site || !m_pDirectoryListing) {
		return;
	}

	CServerPath path = m_pDirectoryListing->path;
	if (subdir == L".." && path.HasParent()) {
		path = path.GetParent();
	}
	else if (!subdir.empty()) {
		path.AddSegment(subdir);
	}

	UploadDroppedFiles(pLocalDataObject, path, queueOnly);
}

void CState::UploadDroppedFiles(const wxFileDataObject* pFileDataObject, std::wstring const& subdir, bool queueOnly)
{
	if (!m_site || !m_pDirectoryListing) {
		return;
	}

	CServerPath path = m_pDirectoryListing->path;
	if (subdir == L".." && path.HasParent()) {
		path = path.GetParent();
	}
	else if (!subdir.empty()) {
		path.AddSegment(subdir);
	}

	UploadDroppedFiles(pFileDataObject, path, queueOnly);
}

namespace {
template <typename T>
void DoUploadDroppedFiles(CState& state, CMainFrame & mainFrame, T const& files, const CServerPath& path, bool queueOnly)
{
	if (files.empty()) {
		return;
	}

	if (!state.GetSite()) {
		return;
	}

	if (path.empty()) {
		return;
	}

	auto recursiveOperation = state.GetLocalRecursiveOperation();
	if (!recursiveOperation || recursiveOperation->IsActive()) {
		wxBell();
		return;
	}

	for (auto const& file : files) {
		int64_t size;
		bool is_link;
		fz::local_filesys::type type = fz::local_filesys::get_file_info(fz::to_native(file), is_link, &size, 0, 0);
		if (type == fz::local_filesys::file) {
			std::wstring localFile;
			CLocalPath const localPath(fz::to_wstring(file), &localFile);
			mainFrame.GetQueue()->QueueFile(queueOnly, false, localFile, std::wstring(), localPath, path, state.GetSite(), size);
			mainFrame.GetQueue()->QueueFile_Finish(!queueOnly);
		}
		else if (type == fz::local_filesys::dir) {
			CLocalPath localPath(fz::to_wstring(file));
			if (localPath.HasParent()) {

				CServerPath remotePath = path;
				if (!remotePath.ChangePath(localPath.GetLastSegment())) {
					continue;
				}

				local_recursion_root root;
				root.add_dir_to_visit(localPath, remotePath);
				recursiveOperation->AddRecursionRoot(std::move(root));
			}
		}
	}

	CFilterManager filter;
	recursiveOperation->StartRecursiveOperation(recursive_operation::recursive_transfer, filter.GetActiveFilters(), !queueOnly);
}
}

void CState::UploadDroppedFiles(const CLocalDataObject* pLocalDataObject, const CServerPath& path, bool queueOnly)
{
	auto const files = pLocalDataObject->GetFilesW();
	DoUploadDroppedFiles(*this, m_mainFrame, files, path, queueOnly);
}

void CState::UploadDroppedFiles(const wxFileDataObject* pFileDataObject, const CServerPath& path, bool queueOnly)
{
	wxArrayString const& files = pFileDataObject->GetFilenames();
	DoUploadDroppedFiles(*this, m_mainFrame, files, path, queueOnly);
}

namespace {
template<typename T>
void DoHandleDroppedFiles(CState & state, CMainFrame & mainFrame, T const& files, CLocalPath const& path, bool copy)
{
	if (files.empty()) {
		return;
	}

#ifdef __WXMSW__
	int len = 1;

	for (unsigned int i = 0; i < files.size(); ++i) {
		len += files[i].size() + 1;
	}

	// SHFILEOPSTRUCT's pTo and pFrom accept null-terminated lists
	// of null-terminated filenames.
	wchar_t* from = new wchar_t[len];
	wchar_t* p = from;
	for (auto const& file : files) {
		memcpy(p, static_cast<wchar_t const*>(file.c_str()), (file.size() + 1) * sizeof(wchar_t));
		p += file.size() + 1;
	}
	*p = 0; // End of list

	wchar_t* to = new wchar_t[path.GetPath().size() + 2];
	memcpy(to, static_cast<wchar_t const*>(path.GetPath().c_str()), (path.GetPath().size() + 1) * sizeof(wchar_t));
	to[path.GetPath().size() + 1] = 0; // End of list

	SHFILEOPSTRUCT op{};
	op.pFrom = from;
	op.pTo = to;
	op.wFunc = copy ? FO_COPY : FO_MOVE;
	op.hwnd = (HWND)mainFrame.GetHandle();
	CallSHFileOperation(&mainFrame, op);

	delete[] to;
	delete[] from;
#else
	(void)mainFrame;

	wxString error;
	for (auto const& file : files) {
		int64_t size;
		bool is_link;
		fz::local_filesys::type type = fz::local_filesys::get_file_info(fz::to_native(file), is_link, &size, 0, 0);
		if (type == fz::local_filesys::file) {
			std::wstring name;
			CLocalPath sourcePath(fz::to_wstring(file), &name);
			if (name.empty()) {
				continue;
			}
			wxString target = path.GetPath() + name;
			if (file == target) {
				continue;
			}

			if (copy) {
				wxCopyFile(file, target);
			}
			else {
				wxRenameFile(file, target);
			}
		}
		else if (type == fz::local_filesys::dir) {
			CLocalPath sourcePath(fz::to_wstring(file));
			if (sourcePath == path || sourcePath.GetParent() == path) {
				continue;
			}
			if (sourcePath.IsParentOf(path)) {
				error = _("A directory cannot be dragged into one of its subdirectories.");
				continue;
			}

			if (copy) {
				state.RecursiveCopy(sourcePath, path);
			}
			else {
				if (!sourcePath.HasParent()) {
					continue;
				}
				wxRenameFile(file, path.GetPath() + sourcePath.GetLastSegment());
			}
		}
	}
	if (!error.empty()) {
		wxMessageBoxEx(error, _("Could not complete operation"));
	}
#endif

	state.RefreshLocal();
}
}

void CState::HandleDroppedFiles(CLocalDataObject const* pLocalDataObject, CLocalPath const& path, bool copy)
{
	auto const files = pLocalDataObject->GetFilesW();
	DoHandleDroppedFiles(*this, m_mainFrame, files, path, copy);
}

void CState::HandleDroppedFiles(const wxFileDataObject* pFileDataObject, const CLocalPath& path, bool copy)
{
	wxArrayString const& files = pFileDataObject->GetFilenames();
	DoHandleDroppedFiles(*this, m_mainFrame, files, path, copy);
}

bool CState::RecursiveCopy(CLocalPath source, const CLocalPath& target)
{
	if (source.empty() || target.empty()) {
		return false;
	}

	if (source == target) {
		return false;
	}

	if (source.IsParentOf(target)) {
		return false;
	}

	if (!source.HasParent()) {
		return false;
	}

	std::wstring last_segment;
	if (!source.MakeParent(&last_segment)) {
		return false;
	}

	std::list<std::wstring> dirsToVisit;
	dirsToVisit.push_back(last_segment + CLocalPath::path_separator);

	fz::native_string const nsource = fz::to_native(source.GetPath());

	// Process any subdirs which still have to be visited
	while (!dirsToVisit.empty()) {
		std::wstring dirname = dirsToVisit.front();
		dirsToVisit.pop_front();
		wxMkdir(target.GetPath() + dirname);

		fz::local_filesys fs;
		if (!fs.begin_find_files(nsource + fz::to_native(dirname), false)) {
			continue;
		}

		bool is_link{};
		fz::local_filesys::type t{};
		fz::native_string file;
		while (fs.get_next_file(file, is_link, t, 0, 0, 0)) {
			if (file.empty()) {
				wxGetApp().DisplayEncodingWarning();
				continue;
			}

			if (t == fz::local_filesys::dir) {
				if (is_link) {
					continue;
				}

				std::wstring const subDir = dirname + fz::to_wstring(file) + CLocalPath::path_separator;
				dirsToVisit.push_back(subDir);
			}
			else {
				wxCopyFile(source.GetPath() + dirname + file, target.GetPath() + dirname + file);
			}
		}
	}

	return true;
}

bool CState::DownloadDroppedFiles(const CRemoteDataObject* pRemoteDataObject, const CLocalPath& path, bool queueOnly /*=false*/)
{
	bool hasDirs = false;
	bool hasFiles = false;
	std::vector<CRemoteDataObject::t_fileInfo> const& files = pRemoteDataObject->GetFiles();
	for (auto const& fileInfo : files) {
		if (fileInfo.dir) {
			hasDirs = true;
		}
		else {
			hasFiles = true;
		}
	}

	if (hasDirs) {
		if (!IsRemoteConnected() || !IsRemoteIdle()) {
			return false;
		}
	}

	if (hasFiles) {
		m_mainFrame.GetQueue()->QueueFiles(queueOnly, path, *pRemoteDataObject);
	}

	if (!hasDirs) {
		return true;
	}

	recursion_root root(pRemoteDataObject->GetServerPath(), false);
	for (auto const& fileInfo : files) {
		if (!fileInfo.dir) {
			continue;
		}

		CLocalPath newPath(path);
		newPath.AddSegment(CQueueView::ReplaceInvalidCharacters(m_mainFrame.GetOptions(), fileInfo.name));
		root.add_dir_to_visit(pRemoteDataObject->GetServerPath(), fileInfo.name, newPath, fileInfo.link);
	}

	if (!root.empty()) {
		m_pRemoteRecursiveOperation->AddRecursionRoot(std::move(root));

		CFilterManager filter;
		m_pRemoteRecursiveOperation->StartRecursiveOperation(recursive_operation::recursive_transfer, filter.GetActiveFilters(), !queueOnly);
	}

	return true;
}

bool CState::IsRemoteConnected() const
{
	if (!engine_) {
		return false;
	}

	return static_cast<bool>(m_site);
}

bool CState::IsRemoteIdle(bool ignore_recursive) const
{
	if (!ignore_recursive && m_pRemoteRecursiveOperation && m_pRemoteRecursiveOperation->GetOperationMode() != recursive_operation::recursive_none) {
		return false;
	}

	if (!m_pCommandQueue) {
		return true;
	}

	return m_pCommandQueue->Idle(ignore_recursive ? CCommandQueue::normal : CCommandQueue::any);
}

bool CState::IsLocalIdle(bool ignore_recursive) const
{
	if (!ignore_recursive && m_pLocalRecursiveOperation && m_pLocalRecursiveOperation->GetOperationMode() != recursive_operation::recursive_none) {
		return false;
	}

	return true;
}

void CState::ListingFailed(int)
{
	bool const compare = m_changeDirFlags.compare;
	m_changeDirFlags.compare = false;

	if (m_changeDirFlags.syncbrowse && !m_sync_browse.target_path.empty()) {
		wxDialogEx dlg;
		if (!dlg.Create(nullptr, nullID, _("Synchronized browsing"))) {
			SetSyncBrowse(false);
			return;
		}

		auto & lay = dlg.layout();
		auto top = lay.createMain(&dlg, 2);

		top->Add(new wxStaticBitmap(&dlg, nullID, wxArtProvider::GetBitmap(wxART_QUESTION, wxART_OTHER, wxSize(32, 32))), 0, wxALL, lay.dlgUnits(2));

		auto main = lay.createFlex(1);
		top->Add(main);

		main->Add(new wxStaticText(&dlg, nullID, wxString::Format(_("The remote directory '%s' does not exist."), LabelEscape(m_sync_browse.target_path.GetPath()))));

		auto create = new wxRadioButton(&dlg, nullID, _("Create &missing remote directory and enter it"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		main->Add(create);
		create->SetFocus();
		auto disable = new wxRadioButton(&dlg, nullID, _("&Disable synchronized browsing and continue changing the local directory"));
		main->Add(disable);

		lay.createButtonSizerButtons(&dlg, main, false);

		dlg.GetSizer()->Fit(&dlg);
		dlg.GetSizer()->SetSizeHints(&dlg);

		if (dlg.ShowModal() == wxID_OK) {
			if (create->GetValue()) {
				transfer_flags const flags = GetMkdirFlags(m_site.server, m_mainFrame.GetOptions(), m_sync_browse.target_path);
				m_pCommandQueue->ProcessCommand(new CMkdirCommand(m_sync_browse.target_path, flags));
				m_pCommandQueue->ProcessCommand(new CListCommand(m_sync_browse.target_path));
				m_sync_browse.target_path.clear();
				m_changeDirFlags.compare = compare;
				return;
			}
			else {
				CLocalPath local = GetSynchronizedDirectory(m_sync_browse.target_path);
				SetSyncBrowse(false);
				SetLocalDir(local);
			}
		}
		else {
			m_changeDirFlags.syncbrowse = false;
		}
	}
}

void CState::LinkIsNotDir(const CServerPath& path, std::wstring const& subdir)
{
	m_changeDirFlags.compare = false;
	m_changeDirFlags.syncbrowse = false;

	NotifyHandlers(STATECHANGE_REMOTE_LINKNOTDIR, subdir, &path);
}

bool CState::ChangeRemoteDir(CServerPath const& path, std::wstring const& subdir, int flags, bool ignore_busy, bool compare)
{
	if (!m_site || !m_pCommandQueue) {
		return false;
	}

	if (!m_sync_browse.local_root.empty()) {
		CServerPath p(path);
		if (!subdir.empty() && !p.ChangePath(subdir)) {
			wxString msg = wxString::Format(_("Could not get full remote path."));
			wxMessageBoxEx(msg, _("Synchronized browsing"));
			return false;
		}

		if (p != m_sync_browse.remote_root && !p.IsSubdirOf(m_sync_browse.remote_root, false)) {
			wxString msg = wxString::Format(_("The remote directory '%s' is not below the synchronization root (%s).\nDisable synchronized browsing and continue changing the remote directory?"),
					p.GetPath(),
					m_sync_browse.remote_root.GetPath());
			if (wxMessageBoxEx(msg, _("Synchronized browsing"), wxICON_QUESTION | wxYES_NO) != wxYES) {
				return false;
			}
			SetSyncBrowse(false);
		}
		else if (!IsRemoteIdle(true) && !ignore_busy) {
			wxString msg(_("Another remote operation is already in progress, cannot change directory now."));
			wxMessageBoxEx(msg, _("Synchronized browsing"), wxICON_EXCLAMATION);
			return false;
		}
		else {
			std::wstring error;
			CLocalPath local_path = GetSynchronizedDirectory(p);
			if (local_path.empty()) {
				wxString msg = wxString::Format(_("Could not obtain corresponding local directory for the remote directory '%s'.\nDisable synchronized browsing and continue changing the remote directory?"),
					p.GetPath());
				if (wxMessageBoxEx(msg, _("Synchronized browsing"), wxICON_QUESTION | wxYES_NO) != wxYES) {
					return false;
				}
				SetSyncBrowse(false);
			}
			else if (!local_path.Exists(&error)) {
				wxDialogEx dlg;
				if (!dlg.Create(nullptr, nullID, _("Synchronized browsing"))) {
					return false;
				}

				auto & lay = dlg.layout();
				auto top = lay.createMain(&dlg, 2);

				top->Add(new wxStaticBitmap(&dlg, nullID, wxArtProvider::GetBitmap(wxART_QUESTION, wxART_OTHER, wxSize(32, 32))), 0, wxALL, lay.dlgUnits(2));

				auto main = lay.createFlex(1);
				top->Add(main);

				main->Add(new wxStaticText(&dlg, nullID, wxString::Format(_("The local directory '%s' does not exist."), LabelEscape(local_path.GetPath()))));

				auto create = new wxRadioButton(&dlg, nullID, _("Create &missing local directory and enter it"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
				main->Add(create);
				create->SetFocus();
				auto disable = new wxRadioButton(&dlg, nullID, _("&Disable synchronized browsing and continue changing the remote directory"));
				main->Add(disable);

				lay.createButtonSizerButtons(&dlg, main, false);

				dlg.GetSizer()->Fit(&dlg);
				dlg.GetSizer()->SetSizeHints(&dlg);

				if (dlg.ShowModal() != wxID_OK) {
					return false;
				}

				if (create->GetValue()) {
					{
						wxLogNull log;
						wxMkdir(local_path.GetPath());
					}

					if (!local_path.Exists(&error)) {
						wxMessageBoxEx(wxString::Format(_("The local directory '%s' could not be created."), local_path.GetPath()), _("Synchronized browsing"), wxICON_EXCLAMATION);
						return false;
					}
					m_changeDirFlags.syncbrowse = true;
					m_changeDirFlags.compare = m_pComparisonManager->IsComparing();
					m_sync_browse.target_path.clear();
				}
				else {
					SetSyncBrowse(false);
				}
			}
			else {
				m_changeDirFlags.syncbrowse = true;
				m_changeDirFlags.compare = m_pComparisonManager->IsComparing();
				m_sync_browse.target_path.clear();
			}
		}
	}

	CListCommand *pCommand = new CListCommand(path, subdir, flags);
	m_pCommandQueue->ProcessCommand(pCommand);

	if (compare) {
		m_changeDirFlags.compare = true;
	}

	return true;
}

bool CState::SetSyncBrowse(bool enable, CServerPath const& assumed_remote_root)
{
	if (enable != m_sync_browse.local_root.empty()) {
		return enable;
	}

	if (!enable) {
		wxASSERT(assumed_remote_root.empty());
		m_sync_browse.local_root.clear();
		m_sync_browse.remote_root.clear();
		m_changeDirFlags.syncbrowse = false;

		NotifyHandlers(STATECHANGE_SYNC_BROWSE);
		return false;
	}

	if (!m_pDirectoryListing && assumed_remote_root.empty()) {
		NotifyHandlers(STATECHANGE_SYNC_BROWSE);
		return false;
	}

	m_changeDirFlags.syncbrowse = false;
	m_sync_browse.local_root = m_localDir;

	if (assumed_remote_root.empty()) {
		m_sync_browse.remote_root = m_pDirectoryListing->path;
	}
	else {
		m_sync_browse.remote_root = assumed_remote_root;
		m_changeDirFlags.syncbrowse = true;
	}

	while (m_sync_browse.local_root.HasParent() && m_sync_browse.remote_root.HasParent() &&
		m_sync_browse.local_root.GetLastSegment() == m_sync_browse.remote_root.GetLastSegment())
	{
		m_sync_browse.local_root.MakeParent();
		m_sync_browse.remote_root = m_sync_browse.remote_root.GetParent();
	}

	NotifyHandlers(STATECHANGE_SYNC_BROWSE);
	return true;
}

CLocalPath CState::GetSynchronizedDirectory(CServerPath remote_path)
{
	std::list<std::wstring> segments;
	while (remote_path.HasParent() && remote_path != m_sync_browse.remote_root) {
		segments.push_front(remote_path.GetLastSegment());
		remote_path = remote_path.GetParent();
	}
	if (remote_path != m_sync_browse.remote_root) {
		return CLocalPath();
	}

	CLocalPath local_path = m_sync_browse.local_root;
	for (auto const& segment : segments) {
		local_path.AddSegment(segment);
	}

	return local_path;
}


CServerPath CState::GetSynchronizedDirectory(CLocalPath local_path)
{
	std::list<std::wstring> segments;
	while (local_path.HasParent() && local_path != m_sync_browse.local_root) {
		std::wstring last;
		local_path.MakeParent(&last);
		segments.push_front(last);
	}
	if (local_path != m_sync_browse.local_root) {
		return CServerPath();
	}

	CServerPath remote_path = m_sync_browse.remote_root;
	for (auto const& segment : segments) {
		remote_path.AddSegment(segment);
	}

	return remote_path;
}

bool CState::RefreshRemote(bool clear_cache)
{
	if (!m_pCommandQueue) {
		return false;
	}

	if (!IsRemoteConnected() || !IsRemoteIdle(true)) {
		return false;
	}

	int flags = LIST_FLAG_REFRESH;
	if (clear_cache) {
		flags |= LIST_FLAG_CLEARCACHE;
	}

	return ChangeRemoteDir(GetRemotePath(), std::wstring(), flags);
}

std::optional<fz::tls_session_info> const& CState::GetTlsSessionInfo() const
{
	return tls_session_info_;
}

void CState::SetTlsSessionInfo(fz::tls_session_info const& info)
{
	ssh_session_info_.reset();
	tls_session_info_ = info;
	NotifyHandlers(STATECHANGE_ENCRYPTION);
}

std::optional<std::pair<fz::ssh::algorithm_info, std::string>> CState::GetSshSessionInfo()
{
	return ssh_session_info_;
}

void CState::SetSshSessionInfo(fz::ssh::algorithm_info const& algs, std::string const& fingerprint)
{
	tls_session_info_.reset();
	ssh_session_info_ = std::make_pair(algs, fingerprint);
	NotifyHandlers(STATECHANGE_ENCRYPTION);
}

void CState::UpdateSite(std::wstring const& oldPath, Site const& newSite)
{
	if (newSite.SitePath().empty() || !newSite) {
		return;
	}

	bool changed = false;
	if (m_site && m_site != newSite) {
		if (m_site.SitePath() == oldPath && m_site.GetOriginalServer().SameResource(newSite.GetOriginalServer())) {
			// Update handles
			m_site.Update(newSite);
			changed = true;
		}
	}
	if (m_last_site && m_last_site != newSite) {
		if (m_last_site.SitePath() == oldPath && m_last_site.GetOriginalServer().SameResource(newSite.GetOriginalServer())) {
			m_last_site.Update(newSite);
			if (!m_site) {
				// Active site has precedence over historic data
				changed = true;
			}
		}
	}
	if (changed) {
		UpdateTitle();
		NotifyHandlers(STATECHANGE_SERVER);
	}
}

void CState::UpdateKnownSites(std::vector<CSiteManagerDialog::_connected_site> const& active_sites)
{
	bool changed{};
	if (m_site) {
		for (auto const& active_site : active_sites) {
			if (active_site.old_path == m_site.SitePath()) {
				if (active_site.old_path == m_site.SitePath() && active_site.site && m_site.GetOriginalServer().SameResource(active_site.site.GetOriginalServer())) {
					if (m_site != active_site.site) {
						changed = true;
						m_site.Update(active_site.site);
					}
				}
				else {
					changed = true;
					m_site.SetSitePath(std::wstring());
				}
				m_last_site = m_site;
				break;
			}
		}
	}
	else if (m_last_site) {
		for (auto const& active_site : active_sites) {
			if (active_site.old_path == m_last_site.SitePath()) {
				if (active_site.old_path == m_last_site.SitePath() && active_site.site && m_last_site.GetOriginalServer().SameResource(active_site.site.GetOriginalServer())) {
					if (m_last_site != active_site.site) {
						changed = true;
						m_last_site.Update(active_site.site);
					}
				}
				else {
					changed = true;
					m_last_site.SetSitePath(std::wstring());
				}
				break;
			}
		}
	}

	if (changed) {
		UpdateTitle();
		NotifyHandlers(STATECHANGE_SERVER);
	}
}

void CState::UpdateTitle()
{
	if (m_site) {
		std::wstring const& name = m_site.GetName();
		m_title.clear();
		if (!name.empty()) {
			m_title = name + _T(" - ");
		}
		m_title += m_site.Format(ServerFormat::with_user_and_optional_port);
	}
	else {
		m_title = _("Not connected");
	}
}

void CState::ChangeServer(CServer const& newServer)
{
	if (m_site) {
		if (!m_site.originalServer) {
			m_site.originalServer = m_site.server;
		}
		m_site.server = newServer;
	}
}
