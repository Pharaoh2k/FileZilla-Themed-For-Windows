#ifndef FILEZILLA_ENGINE_CONTROLSOCKET_HEADER
#define FILEZILLA_ENGINE_CONTROLSOCKET_HEADER

#include "../include/activity_logger.h"
#include "../include/directorylisting.h"
#include "../include/server.h"
#include "../include/serverpath.h"

#include "logging_private.h"
#include "oplock_manager.h"

#include <libfilezilla/buffer.hpp>
#include <libfilezilla/socket.hpp>

#include <optional>

enum class async_request_type : unsigned {
	waiting,
	parallel,
	global
};

class COpData
{
public:
	explicit COpData(Command op_Id, wchar_t const* name)
		: opId(op_Id)
		, name_(name)
	{}

	virtual ~COpData() = default;

	COpData(COpData const&) = delete;
	COpData& operator=(COpData const&) = delete;

	// Functions here must return one of '4' values:
	// - FZ_REPLY_OK, operation succeeded
	// - FZ_REPLY_ERROR (possibly with flags)
	// - FZ_REPLY_WOULDBLOCK, waiting on some exvent
	// - FZ_REPLY_CONTINUE, caller should issue the next command

	virtual int Send() = 0;
	virtual int ParseResponse() = 0;

	virtual int SubcommandResult(int prevResult, COpData const&) { return prevResult == FZ_REPLY_OK ? FZ_REPLY_CONTINUE : prevResult; }

	// Called just prior to destructing the operation. Do not push a new operation here.
	virtual int Reset(int result) { return result; }

	int opState{};
	Command const opId;

	OpLock opLock_;

	wchar_t const* const name_;

	logmsg::type sendLogLevel_{logmsg::debug_verbose};

	bool topLevelOperation_{}; // If set to true, if this command finishes, any other commands on the stack do not get a SubCommandResult

	std::optional<std::pair<std::shared_ptr<unsigned int>, async_request_type>> pending_async_request_;
};

template<typename T>
class CProtocolOpData
{
public:
	CProtocolOpData(T & controlSocket)
		: controlSocket_(controlSocket)
		, engine_(controlSocket.engine_)
		, currentServer_(controlSocket.currentServer_)
		, currentPath_(controlSocket.currentPath_)
		, options_(engine_.GetOptions())
	{
	}

	virtual ~CProtocolOpData() = default;

	template<typename...Args>
	void log(Args&& ...args) const {
		controlSocket_.log(std::forward<Args>(args)...);
	}

	T & controlSocket_;
	CFileZillaEnginePrivate & engine_;
	CServer & currentServer_;
	CServerPath& currentPath_;
	COptionsBase& options_;
};

class ResultOpData : public COpData
{
public:
	explicit ResultOpData(int result)
		: COpData(Command::none, L"ResultOpData")
		, result_(result)
	{}

	virtual int Send() override { return result_; }
	virtual int ParseResponse() override { return FZ_REPLY_INTERNALERROR; }

	int const result_{};
};

class CNotSupportedOpData : public COpData
{
public:
	CNotSupportedOpData()
		: COpData(Command::none, L"CNotSupportedOpData")
	{}

	virtual int Send() override { return FZ_REPLY_NOTSUPPORTED; }
	virtual int ParseResponse() override { return FZ_REPLY_INTERNALERROR; }
};

class SleepOpData final : public COpData, public::fz::event_handler
{
public:
	SleepOpData(CControlSocket & controlSocket, fz::duration const& delay);

	virtual ~SleepOpData()
	{
		remove_handler();
	}

	virtual int Send() override { return FZ_REPLY_WOULDBLOCK; }
	virtual int ParseResponse() override { return FZ_REPLY_INTERNALERROR; }

private:
	virtual void operator()(fz::event_base const&) override;

	CControlSocket & controlSocket_;
};

class CFileTransferOpData : public COpData
{
public:
	CFileTransferOpData(wchar_t const* name, CFileTransferCommand const& cmd);

	bool download() const { return flags_ & transfer_flags::download; }

	bool tryAbsolutePath_{};
	bool resume_{};

	transfer_flags flags_;

	// Set to true when sending the command which
	// starts the actual transfer
	bool transferInitiated_{};

	fz::reader_factory_holder reader_factory_;
	fz::writer_factory_holder writer_factory_;
	std::wstring localName_;
	std::wstring remoteFile_;
	CServerPath remotePath_;

	uint64_t localFileSize_{fz::aio_base::nosize};
	fz::datetime localFileTime_;

	int64_t remoteFileSize_{-1};
	fz::datetime remoteFileTime_;
};

class CMkdirOpData : public COpData
{
public:
	CMkdirOpData(wchar_t const* name)
	    : COpData(Command::mkdir, name)
	{
	}

	CMkdirOpData(wchar_t const* name, CServerPath const& path)
	    : COpData(Command::mkdir, name)
	    , path_(path)
	{
	}

	CServerPath path_;
	CServerPath currentMkdPath_;
	CServerPath commonParent_;
	std::vector<std::wstring> segments_;
};

class CChangeDirOpData : public COpData
{
public:
	CChangeDirOpData(wchar_t const* name)
		: COpData(Command::cwd, name)
	{
	}

	bool tryMkdOnFail_{};
	bool link_discovery_{};

	CServerPath path_;
	std::wstring subDir_;
	CServerPath target_;
};

namespace fz {
class socket_layer;
}
class CFileExistsNotification;
class CTransferStatus;
class CControlSocket : public fz::event_handler
{
public:
	CControlSocket(CFileZillaEnginePrivate & engine, bool use_shm = false);
	virtual ~CControlSocket();

	CControlSocket(CControlSocket const&) = delete;
	CControlSocket& operator=(CControlSocket const&) = delete;

	virtual int Disconnect();

	virtual void Cancel();

	// Implicit FZ_REPLY_CONTINUE
	virtual void Connect(CServer const& server, Credentials const& credentials) = 0;
	virtual void List(CServerPath const& path = CServerPath(), std::wstring const& subDir = std::wstring(), int flags = 0);

	virtual void FileTransfer(CFileTransferCommand const& command) = 0;
	virtual void RawCommand(std::wstring const& command = std::wstring());
	virtual void Delete(CServerPath const& path, std::vector<std::wstring>&& files);
	virtual void RemoveDir(CServerPath const& path = CServerPath(), std::wstring const& subDir = std::wstring());
	virtual void Mkdir(CServerPath const& path, transfer_flags const& flags);
	virtual void Rename(CRenameCommand const& command);
	virtual void Chmod(CChmodCommand const& command);
	void Sleep(fz::duration const& delay);

	Command GetCurrentCommandId() const;

	void SendAsyncRequest(std::unique_ptr<CAsyncRequestNotification> && notification, async_request_type type = async_request_type::waiting);
	void CallSetAsyncRequestReply(CAsyncRequestNotification *pNotification);
	bool SetFileExistsAction(CFileExistsNotification *pFileExistsNotification);

	CServer const& GetCurrentServer() const;

	// Conversion function which convert between local and server charset.
	std::wstring ConvToLocal(char const* buffer, size_t len);
	std::string ConvToServer(std::wstring const&);

	void RecordActivity(activity_logger::_direction direction, uint64_t amount);
	template<typename T, std::enable_if_t<std::is_signed_v<T>, int> = 0>
	inline void RecordActivity(activity_logger::_direction direction, int64_t amount) {
		if (amount > 0) {
			RecordActivity(direction, static_cast<uint64_t>(amount));
		}
	}

	void SetHandle(ServerHandle const& handle) { handle_ = handle; }
	ServerHandle const& GetHandle() const { return handle_; }

	// ---
	// The following two functions control the timeout behaviour:
	// ---

	// Call this on activity that precludes a timeout
	void SetAlive();

	// Set to true if waiting for data
	void SetWait(bool waiting);

	CFileZillaEnginePrivate& GetEngine() { return engine_; }

	// Only called from the engine, see there for description
	void InvalidateCurrentWorkingDir(CServerPath const& path);

	virtual bool CanSendNextCommand() { return true; }
	int SendNextCommand();

	template<typename ...Args>
	void log(Args&& ... args) {
		logger_.log(std::forward<Args>(args)...);
	}
	template<typename ...Args>
	void log_raw(Args&& ... args) {
		logger_.log_raw(std::forward<Args>(args)...);
	}

	fz::logger_interface& logger() const { return logger_; }

	virtual size_t max_buffer_count() const;

protected:
	virtual void Lookup(CServerPath const& path, std::wstring const& file, CDirentry * entry = nullptr);
	virtual void Lookup(CServerPath const& path, std::vector<std::wstring> const& files);

	friend class SleepOpData;
	friend class LookupOpData;
	friend class LookupManyOpData;
	friend class CProtocolOpData<CControlSocket>;

	virtual bool SetAsyncRequestReply(CAsyncRequestNotification *pNotification) = 0;
	void SendDirectoryListingNotification(CServerPath const& path, bool failed);

	std::shared_ptr<unsigned int> pending_global_async_request_;

	fz::duration GetInferredTimezoneOffset() const;

	virtual int DoClose(int nErrorCode = FZ_REPLY_DISCONNECTED | FZ_REPLY_ERROR);

	virtual int ResetOperation(int nErrorCode);
	virtual void UpdateCache(COpData const& data, CServerPath const& serverPath, std::wstring const& remoteFile, int64_t fileSize);

	void LogTransferResultMessage(int nErrorCode, CFileTransferOpData *pData);

	// Called by ResetOperation if there's a queued operation
	int ParseSubcommandResult(int prevResult, std::unique_ptr<COpData> && previousOperation);

	std::wstring ConvertDomainName(std::wstring const& domain);

	int CheckOverwriteFile();

	CServerPath ParsePath(std::wstring reply);
	bool ParsePwdReply(std::wstring reply, const CServerPath& defaultPath = CServerPath(), bool quoted = true);

	virtual void Push(std::unique_ptr<COpData> && pNewOpData);

	OpLock Lock(locking_reason reason, CServerPath const& path, bool inclusive = false);

	bool InitBufferPool(bool use_shm);

	std::unique_ptr<fz::writer_base> OpenWriter(fz::writer_factory_holder & h, uint64_t resumeOffset, bool withProgress);

	std::optional<fz::aio_buffer_pool> buffer_pool_;
	std::vector<std::unique_ptr<COpData>> operations_;
	CFileZillaEnginePrivate & engine_;
	CServer currentServer_;
	Credentials credentials_;

	CServerPath currentPath_;

	bool m_useUTF8{true};
	bool shown_encoding_error_{};

	// Timeout data
	fz::timer_id m_timer{};
	fz::monotonic_clock m_lastActivity;

	OpLockManager & opLockManager_;

	bool m_invalidateCurrentPath{};
	ServerHandle handle_;

	fz::logger_interface& logger_;

	virtual void operator()(fz::event_base const& ev);

	void OnTimer(fz::timer_id id);
	void OnObtainLock();
};

class activity_logger_layer;
class ProxyBase;

namespace fz {
class rate_limited_layer;
}

class CRealControlSocket : public CControlSocket
{
public:
	CRealControlSocket(CFileZillaEnginePrivate& engine);
	virtual ~CRealControlSocket();

	int DoConnect(std::wstring const& host, unsigned int port);

protected:
	void CreateSocket(std::wstring const& host);

	virtual int DoClose(int nErrorCode = FZ_REPLY_DISCONNECTED | FZ_REPLY_ERROR) override;
	virtual void ResetSocket();

	virtual void operator()(fz::event_base const& ev) override;
	void OnSocketEvent(fz::socket_event_source* source, fz::socket_event_flag t, int error);
	void OnHostAddress(fz::socket_event_source* source, std::string const& address);

	virtual void OnConnect();
	virtual void OnReceive();
	virtual int OnSend();
	virtual void OnSocketError(int error);

	virtual void SetSocketBufferSizes() {};

	int Send(unsigned char const* buffer, unsigned int len);
	int Send(char const* buffer, unsigned int len) {
		return Send(reinterpret_cast<unsigned char const*>(buffer), len);
	}

	std::unique_ptr<fz::socket> socket_;
	std::unique_ptr<activity_logger_layer> activity_logger_layer_;
	std::unique_ptr<fz::rate_limited_layer> ratelimit_layer_;
	std::unique_ptr<ProxyBase> proxy_layer_;
	fz::socket_layer* active_layer_{};

	fz::buffer send_buffer_;
};

#endif
