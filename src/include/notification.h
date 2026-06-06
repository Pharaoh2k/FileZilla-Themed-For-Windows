#ifndef FILEZILLA_ENGINE_NOTIFICATION_HEADER
#define FILEZILLA_ENGINE_NOTIFICATION_HEADER

// Notification overview
// ---------------------

// To inform the application about what's happening, the engine sends
// some notifications to the application through the notification callback
// passed to the engine on construction.
// Whenever the callback is called, CFileZillaEngine::GetNextNotification
// has to be called until it returns 0 to re-arm the callback,
// or you will lose important notifications or your memory will fill with
// pending notifications.
//
// Note: It may be called from a worker thread.

// A special class of notifications are the asynchronous requests. These
// requests have to be answered. Once processed, call
// CFileZillaEngine::SetAsyncRequestReply to continue the current operation.

#include "commands.h"
#include "local_path.h"
#include "logging.h"
#include "server.h"

#include <fzssh/ssh.hpp>

#include <libfilezilla/time.hpp>
#include <libfilezilla/tls_info.hpp>

#include <optional>

enum NotificationId : unsigned int
{
	nId_logmsg,            // notification about new messages for the message log
	nId_operation,         // operation reply codes
	nId_transferstatus,    // transfer information: bytes transferred, transfer speed and such
	nId_listing,           // directory listings
	nId_asyncrequest,      // asynchronous request
	nId_sftp_encryption,   // information about key exchange, encryption algorithms and so on for SFTP
	nId_local_dir_created, // local directory has been created
	nId_serverchange,      // With some protocols, actual server identity isn't known until after logon
	nId_persistent_state,  // See PersistentStateNotification
	nId_ftp_tls_resumption
};

// Async request IDs
enum RequestId : unsigned int
{
	reqId_fileexists,          // Target file already exists, awaiting further instructions
	reqId_interactiveLogin,    // gives a challenge prompt for a password
	reqId_hostkey,             // used only by SSH/SFTP to indicate new host key
	reqId_certificate,         // sent after a successful TLS handshake to allow certificate
	                           // validation.
	reqId_insecure_connection, // If using opportunistic FTP over TLS, or a completely
	                           // unprotected protocol ask user whether he really wants
	                           // to use a plaintext connection.
	reqId_tls_no_resumption,

	reqId_password,
	reqId_otp,
	reqId_keyfile_password,

	reqId_count
};

class FZC_PUBLIC_SYMBOL CNotification
{
public:
	virtual ~CNotification() = default;
	NotificationId GetID() const { return id_; }

protected:
	explicit CNotification(NotificationId id);
	CNotification(CNotification const&) = delete;
	CNotification& operator=(CNotification const&) = delete;

private:
	NotificationId const id_;
};

template<NotificationId id>
class FZC_PUBLIC_SYMBOL CNotificationHelper : public CNotification
{
public:
	virtual NotificationId GetID() const final { return id; }

protected:
	CNotificationHelper()
		: CNotification(id)
	{}
	CNotificationHelper(CNotificationHelper const&) = delete;
	CNotificationHelper& operator=(CNotificationHelper const&) = delete;
};

class FZC_PUBLIC_SYMBOL CLogmsgNotification final : public CNotificationHelper<nId_logmsg>
{
public:
	explicit CLogmsgNotification(logmsg::type t)
		: msgType(t)
	{}

	template<typename String>
	CLogmsgNotification(logmsg::type t, String && m, fz::datetime const& time)
		: msg(std::forward<String>(m))
		, time_(time)
		, msgType(t)
	{
	}

	std::wstring msg;
	fz::datetime time_;
	logmsg::type msgType{logmsg::status}; // Type of message, see logging.h for details
};

// If CFileZillaEngine does return with FZ_REPLY_WOULDBLOCK, you will receive
// a nId_operation notification once the operation ends.
class FZC_PUBLIC_SYMBOL COperationNotification final : public CNotificationHelper<nId_operation>
{
public:
	COperationNotification() = default;
	COperationNotification(int replyCode, Command commandId)
		: replyCode_(replyCode)
		, commandId_(commandId)
	{}

	int replyCode_{};
	Command commandId_{Command::none};
};

// You get this type of notification every time a directory listing has been
// requested explicitly or when a directory listing was retrieved implicitly
// during another operation, e.g. file transfers.
//
// Primary notifications are those resulting from a CListCommand, other ones
// can happen spontaneously through other actions.
class CDirectoryListing;
class FZC_PUBLIC_SYMBOL CDirectoryListingNotification final : public CNotificationHelper<nId_listing>
{
public:
	explicit CDirectoryListingNotification(CServerPath const& path, bool const primary, bool const failed = false);
	bool Primary() const { return primary_; }
	bool Failed() const { return m_failed; }
	const CServerPath GetPath() const { return m_path; }

protected:
	bool const primary_{};
	bool m_failed{};
	CServerPath m_path;
};

class FZC_PUBLIC_SYMBOL CAsyncRequestNotification : public CNotificationHelper<nId_asyncrequest>
{
public:
	RequestId GetRequestID() const { return req_id_; }
	bool IsPending() const;

	std::weak_ptr<unsigned int> requestNumber_;

protected:

	explicit CAsyncRequestNotification(RequestId id);
	CAsyncRequestNotification(CAsyncRequestNotification const&) = delete;
	CAsyncRequestNotification& operator=(CAsyncRequestNotification const&) = delete;

private:
	RequestId const req_id_;
};

class FZC_PUBLIC_SYMBOL CFileExistsNotification final : public CAsyncRequestNotification
{
public:
	CFileExistsNotification()
		: CAsyncRequestNotification(reqId_fileexists)
	{}

	bool download{};

	std::wstring localFile;
	int64_t localSize{-1};
	fz::datetime localTime;

	std::wstring remoteFile;
	CServerPath remotePath;
	int64_t remoteSize{-1};
	fz::datetime remoteTime;

	bool ascii{};

	bool canResume{};

	// overwriteAction will be set by the request handler
	enum OverwriteAction : signed char
	{
		unknown = -1,
		ask,
		overwrite,
		overwriteNewer,	// Overwrite if source file is newer than target file
		overwriteSize,	// Overwrite if source file is is different in size than target file
		overwriteSizeOrNewer,	// Overwrite if source file is different in size or newer than target file
		resume, // Overwrites if cannot be resumed
		rename,
		skip,

		ACTION_COUNT
	};

	// Set overwriteAction to the desired action
	OverwriteAction overwriteAction{unknown};

	// On uploads: Set to new filename if overwriteAction is rename. Might trigger further
	// file exists notifications if new target file exists as well.
	std::wstring newName;

	// On downloads: New writer if overwriteAction is rename
	fz::writer_factory_holder new_writer_factory_;
};

class FZC_PUBLIC_SYMBOL CInteractiveLoginNotification final : public CAsyncRequestNotification
{
public:
	CInteractiveLoginNotification(CServer const& server, ServerHandle const& handle);

	CServer server_;
	ServerHandle handle_;

	// Name and instruction may be empty
	std::string name_;
	std::string instruction_;
	std::vector<fz::ssh::keyboard_interactive_prompt> prompts_;

	std::optional<std::vector<std::string>> responses_;
};

class FZC_PUBLIC_SYMBOL PasswordRequest final : public CAsyncRequestNotification
{
public:
	PasswordRequest(CServer const& server, ServerHandle const& handle, bool canRemember);

	CServer server_;
	ServerHandle handle_;

	std::optional<std::wstring> password_;

	bool const canRemember_;
};
class FZC_PUBLIC_SYMBOL OtpRequest final : public CAsyncRequestNotification
{
public:
	OtpRequest(CServer const& server, ServerHandle const& handle);

	CServer server_;
	ServerHandle handle_;

	std::string otp_;
};

class FZC_PUBLIC_SYMBOL KeyfilePasswordRequest final : public CAsyncRequestNotification
{
public:
	KeyfilePasswordRequest(CServer const& server, ServerHandle const& handle);

	CServer server_;
	ServerHandle handle_;

	// These may be empty
	std::string file_;
	std::string fingerprint_;
	std::string comment_;

	bool repeated_{};

	std::optional<std::string> password_;
};

class FZC_PUBLIC_SYMBOL CTransferStatus final
{
public:
	CTransferStatus() {}
	CTransferStatus(int64_t total, int64_t start, bool l)
		: totalSize(total)
		, startOffset(start)
		, currentOffset(start)
		, list(l)
	{}

	fz::datetime started;
	int64_t totalSize{-1};		// Total size of the file to transfer, -1 if unknown
	int64_t startOffset{-1};
	int64_t currentOffset{-1};

	void clear() { startOffset = -1; }
	bool empty() const { return startOffset < 0; }

	explicit operator bool() const { return !empty(); }

	// True on download notifications iff currentOffset != startOffset.
	// True on FTP upload notifications iff currentOffset != startOffset
	// AND after the first accepted data after the first EWOULDBLOCK.
	// SFTP uploads: Set to true if currentOffset >= startOffset + 65536.
	bool madeProgress{};

	bool list{};
};

class FZC_PUBLIC_SYMBOL CTransferStatusNotification final : public CNotificationHelper<nId_transferstatus>
{
public:
	CTransferStatusNotification() {}
	CTransferStatusNotification(CTransferStatus const& status);

	CTransferStatus const& GetStatus() const;

protected:
	CTransferStatus const status_;
};


namespace fz::ssh {
class public_key;
}
class FZC_PUBLIC_SYMBOL CHostKeyNotification final : public CAsyncRequestNotification
{
public:
	CHostKeyNotification(CServer const& server, ServerHandle const& handle, std::unique_ptr<fz::ssh::public_key> && hostkey, fz::ssh::algorithm_info && algorithms);
	~CHostKeyNotification();

	CServer server_;
	ServerHandle handle_;

	// Set to true if you trust the server
	bool trust_{};

	std::unique_ptr<fz::ssh::public_key> hostkey_;
	fz::ssh::algorithm_info algorithms_;
};

class FZC_PUBLIC_SYMBOL CCertificateNotification final : public CAsyncRequestNotification
{
public:
	CCertificateNotification(fz::tls_session_info && info);

	fz::tls_session_info info_;

	bool trusted_{};
};

class FZC_PUBLIC_SYMBOL CSftpEncryptionNotification final : public CNotificationHelper<nId_sftp_encryption>
{
public:
	CSftpEncryptionNotification(fz::ssh::algorithm_info const& algs, std::string const& fingerprint)
		: algorithms_(algs)
		, hostkey_fingerprint_(fingerprint)
	{}

	fz::ssh::algorithm_info algorithms_;
	std::string hostkey_fingerprint_;
};

class FZC_PUBLIC_SYMBOL CLocalDirCreatedNotification final : public CNotificationHelper<nId_local_dir_created>
{
public:
	CLocalPath dir;
};

class FZC_PUBLIC_SYMBOL CInsecureConnectionNotification final : public CAsyncRequestNotification
{
public:
	CInsecureConnectionNotification(CServer const& server);
	virtual RequestId GetRequestID() const { return reqId_insecure_connection; }

	CServer const server_;
	bool allow_{};
};

class FZC_PUBLIC_SYMBOL ServerChangeNotification final : public CNotificationHelper<nId_serverchange>
{
public:
	ServerChangeNotification() = default;

	explicit ServerChangeNotification(CServer const& server)
	    : newServer_(server)
	{}

	CServer newServer_{};
};

class FZC_PUBLIC_SYMBOL FtpTlsResumptionNotification final : public CNotificationHelper<nId_ftp_tls_resumption>
{
public:
	FtpTlsResumptionNotification() = default;

	explicit FtpTlsResumptionNotification(CServer const& server)
	    : server_(server)
	{}

	CServer const server_{};
};

class FZC_PUBLIC_SYMBOL FtpTlsNoResumptionNotification final : public CAsyncRequestNotification
{
public:
	FtpTlsNoResumptionNotification(CServer const& server)
		: CAsyncRequestNotification(reqId_tls_no_resumption)
	    , server_(server)
	{}

	CServer const server_;
	bool allow_{};
};

// Can be sent by the engine while processing a CFileTransferCommand.
// Should the transfer fail, the persistent state can be passed in a subsequent
// transfer command as the "state" member.
class FZC_PUBLIC_SYMBOL PersistentStateNotification final : public CNotificationHelper<nId_persistent_state>
{
public:
	PersistentStateNotification() = default;

	explicit PersistentStateNotification(std::string && state)
	    : persistent_state_(std::move(state))
	{}

	explicit PersistentStateNotification(std::string_view const& state)
	    : persistent_state_(state)
	{}

	std::string persistent_state_;
};

#endif
