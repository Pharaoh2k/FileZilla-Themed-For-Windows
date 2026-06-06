#ifndef FILEZILLA_ENGINE_SFTP_CWD_HEADER
#define FILEZILLA_ENGINE_SFTP_CWD_HEADER

#include "sftpcontrolsocket.h"

class CSftpChangeDirOpData final : public CChangeDirOpData, public CSftpOpData
{
public:
	CSftpChangeDirOpData(CSftpControlSocket & controlSocket)
		: CChangeDirOpData(L"CSftpChangeDirOpData")
		, CSftpOpData(controlSocket)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override { return FZ_REPLY_INTERNALERROR; }

	virtual int SubcommandResult(int, COpData const&) override
	{
		return FZ_REPLY_CONTINUE;
	}

	virtual continuation process_name(fz::ssh::sftp::entry & e, bool) override;
	virtual continuation do_process_status(fz::ssh::sftp::status_code code, std::wstring_view msg) override;
	virtual continuation process_attributes(fz::ssh::sftp::attributes & attrs) override;
};

#endif
