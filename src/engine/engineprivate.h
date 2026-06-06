#ifndef FILEZILLA_ENGINEPRIVATE_HEADER
#define FILEZILLA_ENGINEPRIVATE_HEADER

#include "../include/engine_context.h"
#include "../include/FileZillaEngine.h"
#include "../include/optionsbase.h"

#include <libfilezilla/event.hpp>
#include <libfilezilla/event_handler.hpp>
#include <libfilezilla/mutex.hpp>
#include <libfilezilla/time.hpp>

#include <atomic>
#include <list>
#include <deque>

class CControlSocket;
class CLogging;
class OpLockManager;

enum EngineNotificationType
{
	engineCancel,
};

struct filezilla_engine_event_type;
typedef fz::simple_event<filezilla_engine_event_type, EngineNotificationType> CFileZillaEngineEvent;

class CTransferStatusManager final
{
public:
	CTransferStatusManager(CFileZillaEnginePrivate& engine);

	CTransferStatusManager(CTransferStatusManager const&) = delete;
	CTransferStatusManager& operator=(CTransferStatusManager const&) = delete;

	bool empty();

	void Init(int64_t totalSize, int64_t startOffset, bool list);
	void Reset();
	void SetStartTime();
	void SetMadeProgress();
	void Update(int64_t transferredBytes);

	CTransferStatus Get(bool &changed);

protected:
	fz::mutex mutex_;

	CTransferStatus status_;
	std::atomic<int64_t> currentOffset_{};
	int send_state_{};
	std::atomic_bool made_progress_{};

	CFileZillaEnginePrivate& engine_;
};

class CFileZillaEnginePrivate final : public fz::event_handler
{
public:
	CFileZillaEnginePrivate(CFileZillaEngineContext& engine_context, CFileZillaEngine& parent, std::function<void(CFileZillaEngine*)> const& notification_cb);
	virtual ~CFileZillaEnginePrivate();

	int Execute(CCommand const& command);
	int Cancel();
	int ResetOperation(int nErrorCode);

	bool IsBusy() const;
	bool IsConnected() const;

	void SetAsyncRequestReply(std::unique_ptr<CAsyncRequestNotification> && pNotification);
	unsigned int GetNextAsyncRequestNumber();

	CTransferStatus GetTransferStatus(bool &changed);

	int CacheLookup(CServerPath const& path, CDirectoryListing& listing);

	// Add new pending notification
	void AddNotification(fz::scoped_lock& lock, std::unique_ptr<CNotification> && notification);
	void AddNotification(std::unique_ptr<CNotification> && notification);
	void AddLogNotification(std::unique_ptr<CLogmsgNotification> && notification);
	std::unique_ptr<CNotification> GetNextNotification();

	COptionsBase& GetOptions() { return options_; }
	fz::rate_limiter& GetRateLimiter() { return rate_limiter_; }
	CDirectoryCache& GetDirectoryCache() { return directory_cache_; }
	CPathCache& GetPathCache() { return path_cache_; }
	fz::thread_pool& GetThreadPool() { return thread_pool_; }
	CFileZillaEngineContext& GetContext() { return context_; }
	CFileZillaEngine& GetParent() { return parent_; }

	// If deleting or renaming a directory, it could be possible that another
	// engine's CControlSocket instance still has that directory as
	// current working directory (m_CurrentPath)
	// Since this would cause problems, this function interate over all engines
	// connected ot the same server and invalidates the current working
	// directories if they match or if it is a subdirectory of the changed
	// directory.
	void InvalidateCurrentWorkingDirs(const CServerPath& path);

	unsigned int GetEngineId() const { return m_engine_id; }

	CTransferStatusManager transfer_status_;

	CustomEncodingConverterBase const& GetEncodingConverter() const { return encoding_converter_; }

	OpLockManager & opLockManager_;

	fz::logger_interface& GetLogger();
	activity_logger& activity_logger_;

	void shutdown();

protected:
	void OnOptionsChanged(watched_options const& options);

	void SendQueuedLogs(bool reset_flag = false);
	void ClearQueuedLogs(bool reset_flag);
	void ClearQueuedLogs(fz::scoped_lock& lock, bool reset_flag);
	bool ShouldQueueLogsFromOptions() const;

	int CheckCommandPreconditions(CCommand const& command, bool checkBusy);

	void OnSetAsyncRequestReplyEvent(std::unique_ptr<CAsyncRequestNotification> const& reply);

	// Command handlers, only called by CFileZillaEngine::Command
	int Connect(CConnectCommand const& command);
	int Disconnect(CDisconnectCommand const& command);
	int List(CListCommand const&command);
	int FileTransfer(CFileTransferCommand const& command);
	int RawCommand(CRawCommand const& command);
	int Delete(CDeleteCommand& command);
	int RemoveDir(CRemoveDirCommand const& command);
	int Mkdir(CMkdirCommand const& command);
	int Rename(CRenameCommand const& command);
	int Chmod(CChmodCommand const& command);

	void DoCancel();

	int ContinueConnect();

	void operator()(fz::event_base const& ev);
	void OnEngineEvent(EngineNotificationType type);
	void OnTimer(fz::timer_id);
	void OnCommandEvent();
	void OnInvalidateCurrentWorkingDir(CServer const& server, CServerPath const& path);

	// Todo: More fine-grained locking, a global mutex isn't nice
	static fz::mutex global_mutex_;

	// General mutex for operations on this engine
	mutable fz::mutex mutex_;

	// Used to synchronize access to the notification list
	fz::mutex notification_mutex_{false};

	std::function<void(CFileZillaEngine*)> notification_cb_;

	unsigned int const m_engine_id;

	static std::vector<CFileZillaEnginePrivate*> m_engineList;

	std::unique_ptr<CControlSocket> controlSocket_;

	std::unique_ptr<CCommand> currentCommand_;

	// Protect access to these with notification_mutex_
	std::deque<CNotification*> m_NotificationList;
	bool m_maySendNotificationEvent{true};
	bool queue_logs_{true};
	std::vector<CLogmsgNotification*> queued_logs_;


	std::atomic<unsigned int> asyncRequestCounter_{};

	COptionsBase& options_;

	std::unique_ptr<CLogging> logger_;

	// Everything related to the retry code
	// ------------------------------------

	void RegisterFailedLoginAttempt(const CServer& server, bool critical);

	// Get the amount of time to wait till next reconnection attempt
	fz::duration GetRemainingReconnectDelay(CServer const& server);

	struct t_failedLogins final
	{
		CServer server;
		fz::monotonic_clock time;
		bool critical{};
	};
	static std::list<t_failedLogins> m_failedLogins;
	int m_retryCount{};
	fz::timer_id m_retryTimer{};

	fz::rate_limiter& rate_limiter_;
	CDirectoryCache& directory_cache_;
	CPathCache& path_cache_;

	CFileZillaEngine& parent_;

	fz::thread_pool & thread_pool_;

	CustomEncodingConverterBase const& encoding_converter_;

	CFileZillaEngineContext& context_;
};

struct async_request_reply_event_type{};
typedef fz::simple_event<async_request_reply_event_type, std::unique_ptr<CAsyncRequestNotification>> CAsyncRequestReplyEvent;

#endif
