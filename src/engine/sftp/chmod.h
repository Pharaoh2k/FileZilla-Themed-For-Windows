#ifndef FILEZILLA_ENGINE_SFTP_CHMOD_HEADER
#define FILEZILLA_ENGINE_SFTP_CHMOD_HEADER

#include "sftpcontrolsocket.h"

class CSftpChmodOpData final : public COpData, public CSftpOpData
{
public:
	CSftpChmodOpData(CSftpControlSocket & controlSocket, CChmodCommand const& command)
		: COpData(Command::chmod, L"CSftpChmodOpData")
		, CSftpOpData(controlSocket)
		, command_(command)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override { return FZ_REPLY_INTERNALERROR; }

protected:
	virtual continuation process_attributes(fz::ssh::sftp::attributes & attrs) override;
	virtual continuation do_process_status(fz::ssh::sftp::status_code code, std::wstring_view msg) override;

	CChmodCommand command_;
};

#endif
