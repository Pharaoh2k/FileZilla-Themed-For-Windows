#ifndef FILEZILLA_ENGINE_SFTP_MKD_HEADER
#define FILEZILLA_ENGINE_SFTP_MKD_HEADER

#include "sftpcontrolsocket.h"

#include <deque>

class CSftpMkdirOpData final : public CMkdirOpData, public CSftpOpData
{
public:
	CSftpMkdirOpData(CSftpControlSocket & controlSocket, CServerPath const& path)
	    : CMkdirOpData(L"CSftpMkdirOpData", path)
		, CSftpOpData(controlSocket)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override { return FZ_REPLY_INTERNALERROR; }

	virtual continuation do_process_status(fz::ssh::sftp::status_code, std::wstring_view) override;

	std::vector<CServerPath> paths_;
};

#endif
