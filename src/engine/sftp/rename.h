#ifndef FILEZILLA_ENGINE_SFTP_RENAME_HEADER
#define FILEZILLA_ENGINE_SFTP_RENAME_HEADER

#include "sftpcontrolsocket.h"

class CSftpRenameOpData final : public COpData, public CSftpOpData
{
public:
	CSftpRenameOpData(CSftpControlSocket & controlSocket, CRenameCommand const& command)
		: COpData(Command::rename, L"CSftpRenameOpData")
		, CSftpOpData(controlSocket)
		, command_(command)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override { return FZ_REPLY_INTERNALERROR; }

	virtual continuation do_process_status(fz::ssh::sftp::status_code code, std::wstring_view msg) override;

	CRenameCommand command_;
};

#endif
