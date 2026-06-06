#ifndef FILEZILLA_ENGINE_SFTP_SFTPCONTROLSOCKET_HEADER
#define FILEZILLA_ENGINE_SFTP_SFTPCONTROLSOCKET_HEADER

#include "../controlsocket.h"

#include <fzssh/sftp/sftp_client.hpp>

namespace fz::ssh {
class client;
class public_key;
class session;
struct algorithm_info;
}

class CSftpControlSocket final : public CRealControlSocket
{
public:
	CSftpControlSocket(CFileZillaEnginePrivate & engine);
	virtual ~CSftpControlSocket();

	virtual void Connect(CServer const& server, Credentials const& credentials) override;
	virtual void FileTransfer(CFileTransferCommand const& cmd) override;
	virtual bool SetAsyncRequestReply(CAsyncRequestNotification *pNotification) override;
	void ChangeDir(CServerPath const& path = CServerPath(), std::wstring const& subDir = std::wstring(), bool link_discovery = false);
	virtual void List(CServerPath const& path = CServerPath(), std::wstring const& subDir = std::wstring(), int flags = 0) override;
	virtual void Mkdir(CServerPath const& path, transfer_flags const& flags = {}) override;
	virtual void Delete(CServerPath const& path, std::vector<std::wstring>&& files) override;
	virtual void RemoveDir(CServerPath const& path = CServerPath(), std::wstring const& subDir = std::wstring()) override;
	virtual void Rename(CRenameCommand const& command) override;
	virtual void Chmod(CChmodCommand const& command) override;

protected:
	virtual void SetSocketBufferSizes() override;

	std::wstring QuoteFilename(std::wstring const& filename);

	virtual void Push(std::unique_ptr<COpData> && pNewOpData) override;

	virtual void operator()(fz::event_base const& ev) override;
	virtual int DoClose(int nErrorCode = FZ_REPLY_DISCONNECTED | FZ_REPLY_ERROR) override;

	void OnConnect() override;

	void on_hostkey_verification(fz::ssh::session*, std::unique_ptr<fz::ssh::public_key> &, fz::ssh::algorithm_info &);
	void on_session_done(fz::ssh::session*);
	void on_sftp_done(fz::ssh::sftp::sftp_client*);

	std::unique_ptr<fz::ssh::client> ssh_;
	std::unique_ptr<fz::ssh::sftp::sftp_client> sftp_;

	friend class CSftpOpData;
	friend class CProtocolOpData<CSftpControlSocket>;
	friend class CSftpChangeDirOpData;
	friend class CSftpChmodOpData;
	friend class CSftpConnectOpData;
	friend class CSftpDeleteOpData;
	friend class CSftpFileTransferOpData;
	friend class CSftpListOpData;
	friend class CSftpMkdirOpData;
	friend class CSftpRenameOpData;
	friend class CSftpRemoveDirOpData;
};

class CSftpOpData : public CProtocolOpData<CSftpControlSocket>, public fz::ssh::sftp::response_handler, public fz::event_handler
{
public:
	using continuation = fz::ssh::sftp::continuation;

	CSftpOpData(CSftpControlSocket & controlSocket)
		: CProtocolOpData<CSftpControlSocket>(controlSocket)
		, event_handler(controlSocket_, fz::child_event_handler)
		, sftp_(controlSocket_.sftp_)
	{}

	~CSftpOpData();

	virtual continuation failure() override
	{
		trigger_reset(FZ_REPLY_ERROR);
		return continuation::next;
	}

	virtual continuation process_status(fz::ssh::sftp::status_code, std::string_view) override final;
	virtual continuation do_process_status(fz::ssh::sftp::status_code, std::wstring_view /*formatted_error*/) { return continuation::next; }

protected:
	void trigger_next();

	void trigger_reset(int res);
	virtual void operator()(fz::event_base const& ev) override;

	std::unique_ptr<fz::ssh::sftp::sftp_client> & sftp_;

private:
	void on_reset_event(int res);
	void on_next_event();

	struct reset_event_type;
	typedef fz::simple_event<reset_event_type, int> reset_event;

	struct next_event_type;
	typedef fz::simple_event<next_event_type> next_event;
};

#endif
