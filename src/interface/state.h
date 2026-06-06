#ifndef FILEZILLA_INTERFACE_STATE_HEADER
#define FILEZILLA_INTERFACE_STATE_HEADER

#include "filter_manager.h"
#include "sitemanager.h"
#include "sitemanager_dialog.h"

#include "../include/local_path.h"

#include <memory>
#include <optional>

enum t_statechange_notifications
{
	STATECHANGE_NONE, // Used to unregister all notifications

	STATECHANGE_REMOTE_DIR,
	STATECHANGE_REMOTE_DIR_OTHER,
	STATECHANGE_REMOTE_RECV,
	STATECHANGE_REMOTE_SEND,
	STATECHANGE_REMOTE_LINKNOTDIR,
	STATECHANGE_LOCAL_DIR,

	// data contains name (excluding path) of file to refresh
	STATECHANGE_LOCAL_REFRESH_FILE,

	STATECHANGE_APPLYFILTER,

	STATECHANGE_REMOTE_IDLE,
	STATECHANGE_SERVER,
	STATECHANGE_ENCRYPTION,

	STATECHANGE_SYNC_BROWSE,
	STATECHANGE_COMPARISON,

	STATECHANGE_REMOTE_RECURSION_STATUS,
	STATECHANGE_LOCAL_RECURSION_STATUS,

	STATECHANGE_LOCAL_RECURSION_LISTING,

	/* Global notifications */
	STATECHANGE_QUEUEPROCESSING,
	STATECHANGE_NEWCONTEXT, /* New context created */
	STATECHANGE_CHANGEDCONTEXT, /* Currently active context changed */
	STATECHANGE_REMOVECONTEXT, /* Right before deleting a context */
	STATECHANGE_GLOBALBOOKMARKS,
	STATECHANGE_REWRITE_CREDENTIALS,

	STATECHANGE_QUITNOW,

	STATECHANGE_MAX
};

class CDirectoryListing;
class CFileZillaEngine;
class CCommandQueue;
class CLocalDataObject;
class CLocalRecursiveOperation;
class CMainFrame;
class CGlobalStateEventHandler;
class CStateEventHandler;
class CRemoteDataObject;
class CRemoteRecursiveOperation;
class CComparisonManager;

class CStateFilterManager final : public CFilterManager
{
public:
	virtual bool FilenameFiltered(std::wstring const& name, std::wstring const& path, bool dir, int64_t size, bool local, int attributes, fz::datetime const& date) const override;

	CFilter const& GetLocalFilter() const { return m_localFilter; }
	void SetLocalFilter(CFilter const& filter) { m_localFilter = filter; }

	CFilter const& GetRemoteFilter() const { return m_remoteFilter; }
	void SetRemoteFilter(CFilter const& filter) { m_remoteFilter = filter; }

private:
	CFilter m_localFilter;
	CFilter m_remoteFilter;
};

class CState;
class CContextManager final
{
	friend class CState;
public:
	// If current_only is set, only notifications from the current (at the time
	// of notification emission) context is dispatched to the handler.
	void RegisterHandler(CGlobalStateEventHandler* pHandler, t_statechange_notifications notification, bool current_only);
	void UnregisterHandler(CGlobalStateEventHandler* pHandler, t_statechange_notifications notification);

	size_t HandlerCount(t_statechange_notifications notification) const;

	CState* CreateState(CMainFrame &mainFrame);
	void DestroyState(CState* pState);
	void DestroyAllStates();

	CState* GetCurrentContext();
	const std::vector<CState*>* GetAllStates() { return &m_contexts; }

	static CContextManager* Get();

	void NotifyAllHandlers(t_statechange_notifications notification, std::wstring const& data = std::wstring(), void const* data2 = 0);
	void NotifyGlobalHandlers(t_statechange_notifications notification, std::wstring const& data = std::wstring(), void const* data2 = 0);

	void SetCurrentContext(CState* pState);

	void ProcessDirectoryListing(CServer const& server, std::shared_ptr<CDirectoryListing> const& listing, CState const* exempt);

protected:
	CContextManager();

	std::vector<CState*> m_contexts;
	int m_current_context;

	struct t_handler
	{
		CGlobalStateEventHandler* pHandler;
		bool current_only;
	};
	std::vector<t_handler> m_handlers[STATECHANGE_MAX];

	void NotifyHandlers(CState* pState, t_statechange_notifications notification, std::wstring const& data, void const* data2);

	static CContextManager m_the_context_manager;
};

class CState final
{
	friend class CCommandQueue;
public:
	CState(CMainFrame& mainFrame);
	~CState();

	CState(CState const&) = delete;
	CState& operator=(CState const&) = delete;

	bool CreateEngine();
	void DestroyEngine();

	CLocalPath GetLocalDir() const;
	bool SetLocalDir(CLocalPath const& dir, std::wstring *error = 0, bool rememberPreviousSubdir = true);
	bool SetLocalDir(std::wstring const& dir, std::wstring *error = 0, bool rememberPreviousSubdir = true);

	bool Connect(Site const& site, CServerPath const& path = CServerPath(), bool compare = false);
	bool Disconnect();

	bool ChangeRemoteDir(CServerPath const& path, std::wstring const& subdir = std::wstring(), int flags = 0, bool ignore_busy = false, bool compare = false);
	bool SetRemoteDir(std::shared_ptr<CDirectoryListing> const& pDirectoryListing, bool primary);
	std::shared_ptr<CDirectoryListing> GetRemoteDir() const;
	const CServerPath GetRemotePath() const;

	Site const& GetSite() const;
	wxString GetTitle() const;

	void RefreshLocal();
	void RefreshLocalFile(std::wstring const& file);
	void LocalDirCreated(CLocalPath const& path);

	bool RefreshRemote(bool clear_cache = false);

	void RegisterHandler(CStateEventHandler* pHandler, t_statechange_notifications notification, CStateEventHandler* insertBefore = 0);
	void UnregisterHandler(CStateEventHandler* pHandler, t_statechange_notifications notification);

	std::unique_ptr<CFileZillaEngine> engine_;
	CCommandQueue* m_pCommandQueue{};
	CComparisonManager* GetComparisonManager() { return m_pComparisonManager; }

	void UploadDroppedFiles(CLocalDataObject const* pLocalDataObject, std::wstring const& subdir, bool queueOnly);
	void UploadDroppedFiles(wxFileDataObject const* pFileDataObject, std::wstring const& subdir, bool queueOnly);
	void UploadDroppedFiles(CLocalDataObject const* pLocalDataObject, CServerPath const& path, bool queueOnly);
	void UploadDroppedFiles(wxFileDataObject const* pFileDataObject, CServerPath const& path, bool queueOnly);
	void HandleDroppedFiles(CLocalDataObject const* pLocalDataObject, CLocalPath const& path, bool copy);
	void HandleDroppedFiles(wxFileDataObject const* pFileDataObject, CLocalPath const& path, bool copy);
	bool DownloadDroppedFiles(CRemoteDataObject const* pRemoteDataObject, CLocalPath const& path, bool queueOnly = false);

	static bool RecursiveCopy(CLocalPath source, CLocalPath const& targte);

	bool IsRemoteConnected() const;
	bool IsRemoteIdle(bool ignore_recursive = false) const;
	bool IsLocalIdle(bool ignore_recursive = false) const;

	CLocalRecursiveOperation* GetLocalRecursiveOperation() { return m_pLocalRecursiveOperation; }
	CRemoteRecursiveOperation* GetRemoteRecursiveOperation() { return m_pRemoteRecursiveOperation; }

	void NotifyHandlers(t_statechange_notifications notification, std::wstring const& data = std::wstring(), void const* data2 = 0);

	bool SuccessfulConnect() const { return m_successful_connect; }
	void SetSuccessfulConnect() { m_successful_connect = true; }

	void ListingFailed(int error);
	void LinkIsNotDir(CServerPath const& path, std::wstring const& subdir);

	bool SetSyncBrowse(bool enable, CServerPath const& assumed_remote_root = CServerPath());
	bool GetSyncBrowse() const { return !m_sync_browse.local_root.empty(); }

	Site const& GetLastSite() const { return m_last_site; }
	CServerPath GetLastServerPath() const { return m_last_path; }
	void SetLastSite(Site const& server, CServerPath const& path)
		{ m_last_site = server; m_last_path = path; }

	std::optional<fz::tls_session_info> const& GetTlsSessionInfo() const;
	void SetTlsSessionInfo(fz::tls_session_info const& info);

	std::optional<std::pair<fz::ssh::algorithm_info, std::string>> GetSshSessionInfo();
	void SetSshSessionInfo(fz::ssh::algorithm_info const& algs, std::string const& fingerprint);

	// If the previously selected directory was a direct child of the current directory, this
	// returns the relative name of the subdirectory.
	std::wstring GetPreviouslyVisitedLocalSubdir() const { return m_previouslyVisitedLocalSubdir; }
	std::wstring GetPreviouslyVisitedRemoteSubdir() const { return m_previouslyVisitedRemoteSubdir; }
	void ClearPreviouslyVisitedLocalSubdir() { m_previouslyVisitedLocalSubdir.clear(); }
	void ClearPreviouslyVisitedRemoteSubdir() { m_previouslyVisitedRemoteSubdir.clear(); }

	void UpdateKnownSites(std::vector<CSiteManagerDialog::_connected_site> const& active_sites);
	void UpdateSite(std::wstring const& oldPath, Site const& newSite);

	CStateFilterManager& GetStateFilterManager() { return m_stateFilterManager; }

	void ChangeServer(CServer const& newServer);

	fz::thread_pool & pool_;

protected:
	bool DoSetLocalDir(CLocalPath & dir, std::wstring *error, bool rememberPreviousSubdir);

	void SetSite(Site const& site, CServerPath const& path = CServerPath());

	void UpdateTitle();

	CLocalPath m_localDir;
	std::shared_ptr<CDirectoryListing> m_pDirectoryListing;

	Site m_site;

	wxString m_title;
	bool m_successful_connect{};

	Site m_last_site;
	CServerPath m_last_path;

	CMainFrame& m_mainFrame;

	CLocalRecursiveOperation* m_pLocalRecursiveOperation;
	CRemoteRecursiveOperation* m_pRemoteRecursiveOperation;

	CComparisonManager* m_pComparisonManager;

	CStateFilterManager m_stateFilterManager;

	struct t_handlersForNotification
	{
		std::vector<CStateEventHandler*> handlers;
		bool compact_{};
		bool inNotify_{};
	};

	t_handlersForNotification m_handlers[STATECHANGE_MAX];

	CLocalPath GetSynchronizedDirectory(CServerPath remote_path);
	CServerPath GetSynchronizedDirectory(CLocalPath local_path);

	struct _sync_browse
	{
		CLocalPath local_root;
		CServerPath remote_root;

		// The target path when changing remote directory
		CServerPath target_path;
	} m_sync_browse;

	struct _post_setdir
	{
		bool compare{};
		bool syncbrowse{};
	} m_changeDirFlags;

	std::optional<fz::tls_session_info> tls_session_info_;
	std::optional<std::pair<fz::ssh::algorithm_info, std::string>> ssh_session_info_;

	std::wstring m_previouslyVisitedLocalSubdir;
	std::wstring m_previouslyVisitedRemoteSubdir;
};

class CGlobalStateEventHandler
{
public:
	CGlobalStateEventHandler() = default;
	virtual ~CGlobalStateEventHandler();

	virtual void OnStateChange(CState* pState, t_statechange_notifications notification, std::wstring const& data, const void* data2) = 0;
};

class CStateEventHandler
{
public:
	CStateEventHandler(CState& state);
	virtual ~CStateEventHandler();

	CState& m_state;

	virtual void OnStateChange(t_statechange_notifications notification, std::wstring const& data, const void* data2) = 0;
};

#endif
