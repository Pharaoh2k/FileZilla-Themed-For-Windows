#ifndef FILEZILLA_ENGINE_SFTP_RMD_HEADER
#define FILEZILLA_ENGINE_SFTP_RMD_HEADER

#include "sftpcontrolsocket.h"

class CSftpRemoveDirOpData final : public COpData, public CSftpOpData
{
public:
	CSftpRemoveDirOpData(CSftpControlSocket & controlSocket)
		: COpData(Command::removedir, L"CSftpRemoveDirOpData")
		, CSftpOpData(controlSocket)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override { return FZ_REPLY_INTERNALERROR; }

	virtual continuation do_process_status(fz::ssh::sftp::status_code code, std::wstring_view msg) override;

	CServerPath path_;
	std::wstring subDir_;
};

#endif
