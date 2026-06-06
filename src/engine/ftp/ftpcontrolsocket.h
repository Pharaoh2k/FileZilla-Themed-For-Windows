#ifndef FILEZILLA_ENGINE_FTP_FTPCONTROLSOCKET_HEADER
#define FILEZILLA_ENGINE_FTP_FTPCONTROLSOCKET_HEADER

#include "../logging_private.h"
#include "../controlsocket.h"
#include "../rtt.h"

namespace PrivCommand {
auto const cwd = Command::private1;
auto const rawtransfer = Command::private2;
}

class CExternalIPResolver;
class CTransferSocket;
class CFtpTransferOpData;
class CFtpRawTransferOpData;

namespace fz {
class tls_layer;
}

struct filezilla_engine_ftp_transfer_end_event;
typedef fz::simple_event<filezilla_engine_ftp_transfer_end_event> TransferEndEvent;

enum class TransferEndReason
{
	none,
	successful,
	timeout,
	transfer_failure,					// Error during transfer, like lost connection. Retry automatically
	transfer_failure_critical,			// Error during transfer like lack of diskspace. Needs user interaction
	pre_transfer_command_failure,		// If a command fails prior to sending the transfer command
	transfer_command_failure_immediate,	// Used if server does not send the 150 reply after the transfer command
	transfer_command_failure,			// Used if the transfer command fails, but after receiving a 150 first
	failure,							// Other unspecific failure
	failed_resumetest,
	failed_tls_resumption,
	wrong_tls_alpn
};

class CFtpControlSocket final : public CRealControlSocket
{
	friend class CTransferSocket;
public:
	CFtpControlSocket(CFileZillaEnginePrivate & engine);
	virtual ~CFtpControlSocket();

	virtual bool SetAsyncRequestReply(CAsyncRequestNotification *pNotification) override;

protected:

	virtual void Push(std::unique_ptr<COpData> && pNewOpData) override;

	virtual int ResetOperation(int nErrorCode) override;
	virtual int DoClose(int nErrorCode = FZ_REPLY_DISCONNECTED | FZ_REPLY_ERROR) override;

	// Implicit FZ_REPLY_CONTINUE
	virtual void Connect(CServer const& server, Credentials const& credentials) override;
	virtual int Disconnect() override;
	virtual void List(CServerPath const& path = CServerPath(), std::wstring const& subDir = std::wstring(), int flags = 0) override;
	void ChangeDir(CServerPath const& path = CServerPath(), std::wstring const& subDir = std::wstring(), bool link_discovery = false);
	virtual void FileTransfer(CFileTransferCommand const& cmd) override;
	virtual void RawCommand(std::wstring const& command) override;
	virtual void Delete(CServerPath const& path, std::vector<std::wstring>&& files) override;
	virtual void RemoveDir(CServerPath const& path, std::wstring const& subDir) override;
	virtual void Mkdir(CServerPath const& path, transfer_flags const& flags = {}) override;
	virtual void Rename(CRenameCommand const& command) override;
	virtual void Chmod(CChmodCommand const& command) override;
	void Transfer(std::wstring const& cmd, CFtpTransferOpData* oldData);

	void TransferEnd();

	virtual void OnConnect() override;
	virtual void OnReceive() override;

	void OnVerifyCert(fz::tls_layer* source, fz::tls_session_info& info);

	virtual void ResetSocket() override;

	int SendCommand(std::wstring const& str, bool maskArgs = false, bool measureRTT = true);

	// Parse the latest reply line from the server
	void ParseLine(std::wstring line);

	// Parse the actual response and delegate it to the handlers.
	// It's the last line in a multi-line response.
	void ParseResponse();

	virtual bool CanSendNextCommand() override;

	int GetReplyCode() const;

	int GetExternalIPAddress(std::string& address);

	void StartKeepaliveTimer();

	std::wstring m_Response;
	std::wstring m_MultilineResponseCode;
	std::vector<std::wstring> m_MultilineResponseLines;

	std::unique_ptr<CTransferSocket> m_pTransferSocket;

	// Some servers keep track of the offset specified by REST between sessions
	// So we always sent a REST 0 for a normal transfer following a restarted one
	bool m_sentRestartOffset{};

	fz::buffer receiveBuffer_;
	int m_repliesToSkip{}; // Set to the amount of pending replies if cancelling an action

	int m_pendingReplies{1};

	std::unique_ptr<CExternalIPResolver> m_pIPResolver;

	std::unique_ptr<fz::tls_layer> tls_layer_;
	bool m_protectDataChannel{};

	int m_lastTypeBinary{-1};

	// Used by keepalive code so that we're not using keep alive
	// till the end of time. Stop after a couple of minutes.
	fz::monotonic_clock m_lastCommandCompletionTime;

	fz::timer_id m_idleTimer{};

	CLatencyMeasurement m_rtt;

	virtual void operator()(fz::event_base const& ev) override;

	void OnExternalIPAddress();
	void OnTimer(fz::timer_id id);

	friend class CProtocolOpData<CFtpControlSocket>;
	friend class CFtpChangeDirOpData;
	friend class CFtpChmodOpData;
	friend class CFtpDeleteOpData;
	friend class CFtpFileTransferOpData;
	friend class CFtpListOpData;
	friend class CFtpLogonOpData;
	friend class CFtpMkdirOpData;
	friend class CFtpRawCommandOpData;
	friend class CFtpRawTransferOpData;
	friend class CFtpRemoveDirOpData;
	friend class CFtpRenameOpData;
};

typedef CProtocolOpData<CFtpControlSocket> CFtpOpData;

class CFtpTransferOpData
{
public:
	CFtpTransferOpData() = default;
	virtual ~CFtpTransferOpData() = default;

	TransferEndReason transferEndReason{TransferEndReason::successful};
	bool tranferCommandSent{};

	int64_t resumeOffset{};
	bool binary{true};
};

#endif
