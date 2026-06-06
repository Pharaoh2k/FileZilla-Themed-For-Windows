#ifndef FILEZILLA_ENGINE_COMMANDS_HEADER
#define FILEZILLA_ENGINE_COMMANDS_HEADER

#include "server.h"
#include "serverpath.h"

#include <libfilezilla/aio/reader.hpp>
#include <libfilezilla/aio/writer.hpp>

#include <libfilezilla/uri.hpp>

// See below for actual commands and their parameters

// Command IDs
// -----------
enum class Command
{
	none = 0,
	connect,
	disconnect,
	list,
	transfer,
	del,
	removedir,
	mkdir,
	rename,
	chmod,
	raw,
	httprequest, // Only used by HTTP protocol

	// Only used internally
	sleep,
	lookup,
	cwd,
	common_private1, // Internal commands common to multiple protocols
	common_private2,
	private1,
	private2,
	private3,
	private4,
	private5,
	private6,
};

// Reply codes
// -----------
#define FZ_REPLY_OK				(0x0000)
#define FZ_REPLY_WOULDBLOCK		(0x0001)
#define FZ_REPLY_ERROR			(0x0002)
#define FZ_REPLY_CRITICALERROR	(0x0004 | FZ_REPLY_ERROR) // If there is no point to retry an operation, this
														  // code is returned.
#define FZ_REPLY_CANCELED		(0x0008 | FZ_REPLY_ERROR)
#define FZ_REPLY_SYNTAXERROR	(0x0010 | FZ_REPLY_ERROR)
#define FZ_REPLY_NOTCONNECTED	(0x0020 | FZ_REPLY_ERROR)
#define FZ_REPLY_DISCONNECTED	(0x0040)
#define FZ_REPLY_INTERNALERROR	(0x0080 | FZ_REPLY_ERROR) // If you get this reply, the error description will be
														  // given by the last debug_warning log message. This
														  // should not happen unless there is a bug in FileZilla 3.
#define FZ_REPLY_BUSY			(0x0100 | FZ_REPLY_ERROR)
#define FZ_REPLY_ALREADYCONNECTED	(0x0200 | FZ_REPLY_ERROR) // Will be returned by connect if already connected
#define FZ_REPLY_PASSWORDFAILED	0x0400 // Will be returned if PASS fails with 5yz reply code.
#define FZ_REPLY_TIMEOUT		(0x0800 | FZ_REPLY_ERROR)
#define FZ_REPLY_NOTSUPPORTED	(0x1000 | FZ_REPLY_ERROR) // Will be returned if command not supported by that protocol
#define FZ_REPLY_WRITEFAILED	(0x2000 | FZ_REPLY_ERROR) // Happens if local file could not be written during transfer
#define FZ_REPLY_LINKNOTDIR		(0x4000 | FZ_REPLY_ERROR)

#define FZ_REPLY_CONTINUE 0x8000 // Used internally
#define FZ_REPLY_ERROR_NOTFOUND (0x10000 | FZ_REPLY_ERROR) // Used internally

// --------------- //
// Actual commands //
// --------------- //

class FZC_PUBLIC_SYMBOL CCommand
{
public:
	CCommand() = default;
	virtual ~CCommand() = default;

	virtual Command GetId() const = 0;
	virtual CCommand *Clone() const = 0;

	virtual bool valid() const { return true; }

protected:
	CCommand(CCommand const&) = default;
	CCommand& operator=(CCommand const&) = default;
};

template<typename Derived, Command id>
class FZC_PUBLIC_SYMBOL CCommandHelper : public CCommand
{
public:
	virtual Command GetId() const final { return id; }

	virtual CCommand* Clone() const final {
		return new Derived(static_cast<Derived const&>(*this));
	}

protected:
	CCommandHelper() = default;
	CCommandHelper(CCommandHelper const&) = default;
	CCommandHelper& operator=(CCommandHelper const&) = default;
};

template<Command id>
class FZC_PUBLIC_SYMBOL CBasicCommand final : public CCommandHelper<CBasicCommand<id>, id>
{
};

class FZC_PUBLIC_SYMBOL CConnectCommand final : public CCommandHelper<CConnectCommand, Command::connect>
{
public:
	explicit CConnectCommand(CServer const& server, ServerHandle const& handle, Credentials const& credentials, bool retry_conncting = true);

	CServer const& GetServer() const { return server_; }
	ServerHandle const& GetHandle() const { return handle_; }
	Credentials const& GetCredentials() const { return credentials_; }
	bool RetryConnecting() const { return retry_connecting_; }

	virtual bool valid() const override;
protected:
	CServer const server_;
	ServerHandle const handle_;
	Credentials const credentials_;
	bool const retry_connecting_;
};

typedef CBasicCommand<Command::disconnect> CDisconnectCommand;

#define LIST_FLAG_REFRESH 1
#define LIST_FLAG_AVOID 2
#define LIST_FLAG_FALLBACK_CURRENT 4
#define LIST_FLAG_LINK 8
#define LIST_FLAG_CLEARCACHE 16
class FZC_PUBLIC_SYMBOL CListCommand final : public CCommandHelper<CListCommand, Command::list>
{
	// Without a given directory, the current directory will be listed.
	// Directories can either be given as absolute path or as
	// pair of an absolute path and the very last path segments.

	// Set LIST_FLAG_REFRESH to get a directory listing even if a cache
	// lookup can be made after finding out true remote directory.
	//
	// Set LIST_FLAG_AVOID to get a directory listing only if cache lookup
	// fails or contains unsure entries, otherwise don't send listing.
	//
	// If LIST_FLAG_FALLBACK_CURRENT is set and CWD fails, list whatever
	// directory we are currently in. Useful for initial reconnect to the
	// server when we don't know if remote directory still exists
	//
	// LIST_FLAG_LINK is used for symlink discovery. There's unfortunately
	// no sane way to distinguish between symlinks to files and symlinks to
	// directories.
public:
	explicit CListCommand(int flags = 0);
	explicit CListCommand(CServerPath path, std::wstring const& subDir = std::wstring(), int flags = 0);

	CServerPath GetPath() const;
	std::wstring GetSubDir() const;

	int GetFlags() const { return m_flags; }

	bool valid() const;

protected:
	CServerPath const m_path;
	std::wstring const m_subDir;
	int const m_flags;
};

enum class transfer_flags : unsigned short
{
	none = 0,

	interface_reserved_mask = 0x08u, // The engine will never touch these

	download = 0x10,
	fsync = 0x20,

	// Free bits in the middle

	protocol_reserved_mask = 0xff00,
	protocol_reserved_max = 0x8000 // The highest bit
};

inline bool operator&(transfer_flags lhs, transfer_flags rhs)
{
	return (static_cast<std::underlying_type_t<transfer_flags>>(lhs) & static_cast<std::underlying_type_t<transfer_flags>>(rhs)) != 0;
}

inline transfer_flags operator|(transfer_flags lhs, transfer_flags rhs)
{
	return static_cast<transfer_flags>(static_cast<std::underlying_type_t<transfer_flags>>(lhs) | static_cast<std::underlying_type_t<transfer_flags>>(rhs));
}

inline transfer_flags& operator|=(transfer_flags& lhs, transfer_flags rhs)
{
	lhs = static_cast<transfer_flags>(static_cast<std::underlying_type_t<transfer_flags>>(lhs) | static_cast<std::underlying_type_t<transfer_flags>>(rhs));
	return lhs;
}

inline transfer_flags operator-(transfer_flags lhs, transfer_flags rhs)
{
	return static_cast<transfer_flags>(static_cast<std::underlying_type_t<transfer_flags>>(lhs) & ~static_cast<std::underlying_type_t<transfer_flags>>(rhs));
}

inline transfer_flags& operator-=(transfer_flags& lhs, transfer_flags rhs)
{
	lhs = static_cast<transfer_flags>(static_cast<std::underlying_type_t<transfer_flags>>(lhs) & ~static_cast<std::underlying_type_t<transfer_flags>>(rhs));
	return lhs;
}

inline bool operator!(transfer_flags flags)
{
	return static_cast<std::underlying_type_t<transfer_flags>>(flags) == 0;
}

namespace ftp_transfer_flags
{
	auto constexpr ascii = transfer_flags::protocol_reserved_max;
}

class FZC_PUBLIC_SYMBOL CFileTransferCommand final : public CCommandHelper<CFileTransferCommand, Command::transfer>
{
public:
	CFileTransferCommand(fz::reader_factory_holder const& reader, CServerPath const& remotePath, std::wstring const& remoteFile, transfer_flags const& flags, std::wstring const& extraflags = {}, std::string const& persistentState = {});
	CFileTransferCommand(fz::writer_factory_holder const& writer, CServerPath const& remotePath, std::wstring const& remoteFile, transfer_flags const& flags, std::wstring const& extraFlags = {}, std::string const& persistentState = {});

	CServerPath GetRemotePath() const;
	std::wstring GetRemoteFile() const;
	bool Download() const { return flags_ & transfer_flags::download; }
	transfer_flags const& GetFlags() const { return flags_; }
	std::wstring const& GetExtraFlags() const { return extraFlags_; }

	bool valid() const;

	fz::reader_factory_holder const& GetReader() const { return reader_; }
	fz::writer_factory_holder const& GetWriter() const { return writer_; }

protected:
	fz::reader_factory_holder const reader_;
	fz::writer_factory_holder const writer_;
	CServerPath const m_remotePath;
	std::wstring const m_remoteFile;
	std::wstring const extraFlags_;
	std::string const persistentState_;
	transfer_flags const flags_;
};

class FZC_PUBLIC_SYMBOL CHttpRequestCommand final : public CCommandHelper<CHttpRequestCommand, Command::httprequest>
{
public:
	CHttpRequestCommand(fz::uri const& uri, fz::writer_factory_holder const& output, std::string const& verb = std::string("GET"), fz::reader_factory_holder const& body = fz::reader_factory_holder(), bool confidential_qs = false)
		: uri_(uri)
		, verb_(verb)
		, body_(body)
		, output_(output)
		, confidential_qs_(confidential_qs)
	{}

	fz::uri const uri_;
	std::string const verb_;

	fz::reader_factory_holder body_;
	fz::writer_factory_holder output_;

	bool confidential_qs_{};
};

class FZC_PUBLIC_SYMBOL CRawCommand final : public CCommandHelper<CRawCommand, Command::raw>
{
public:
	explicit CRawCommand(std::wstring const& command);

	std::wstring GetCommand() const;

	bool valid() const { return !m_command.empty(); }

protected:
	std::wstring m_command;
};

class FZC_PUBLIC_SYMBOL CDeleteCommand final : public CCommandHelper<CDeleteCommand, Command::del>
{
public:
	CDeleteCommand(CServerPath const& path, std::vector<std::wstring> && files);

	CServerPath GetPath() const { return m_path; }
	const std::vector<std::wstring>& GetFiles() const { return files_; }
	std::vector<std::wstring>&& ExtractFiles() { return std::move(files_); }

	virtual bool valid() const override;

protected:
	CServerPath const m_path;
	std::vector<std::wstring> files_;
};

class FZC_PUBLIC_SYMBOL CRemoveDirCommand final : public CCommandHelper<CRemoveDirCommand, Command::removedir>
{
public:
	// Directories can either be given as absolute path or as
	// pair of an absolute path and the very last path segments.
	CRemoveDirCommand(CServerPath const& path, std::wstring const& subdDir);

	CServerPath GetPath() const { return m_path; }
	std::wstring GetSubDir() const { return m_subDir; }

	bool valid() const;

protected:
	CServerPath const m_path;
	std::wstring const m_subDir;
};

class FZC_PUBLIC_SYMBOL CMkdirCommand final : public CCommandHelper<CMkdirCommand, Command::mkdir>
{
public:
	explicit CMkdirCommand(CServerPath const& path, transfer_flags const& flags);

	CServerPath GetPath() const { return m_path; }
	transfer_flags const& GetFlags() const { return flags_; }

	bool valid() const;

protected:
	CServerPath const m_path;
	transfer_flags const flags_;
};

class FZC_PUBLIC_SYMBOL CRenameCommand final : public CCommandHelper<CRenameCommand, Command::rename>
{
public:
	CRenameCommand(CServerPath const& fromPath, std::wstring const& fromFile,
				   CServerPath const& toPath, std::wstring const& toFile);

	CServerPath GetFromPath() const { return m_fromPath; }
	CServerPath GetToPath() const { return m_toPath; }
	std::wstring GetFromFile() const { return m_fromFile; }
	std::wstring GetToFile() const { return m_toFile; }

	bool valid() const;

protected:
	CServerPath const m_fromPath;
	CServerPath const m_toPath;
	std::wstring const m_fromFile;
	std::wstring const m_toFile;
};

class FZC_PUBLIC_SYMBOL CChmodCommand final : public CCommandHelper<CChmodCommand, Command::chmod>
{
public:
	// The permission string should be given in a format understandable by the server.
	// Most likely it's the default octal representation used by the unix chmod command,
	// i.e. chmod 755 foo.bar
	CChmodCommand(CServerPath const& path, std::wstring const& file, std::wstring const& permission);

	CServerPath GetPath() const { return m_path; }
	std::wstring GetFile() const { return m_file; }
	std::wstring GetPermission() const { return m_permission; }

	bool valid() const;

protected:
	CServerPath const m_path;
	std::wstring const m_file;
	std::wstring const m_permission;
};

#endif
