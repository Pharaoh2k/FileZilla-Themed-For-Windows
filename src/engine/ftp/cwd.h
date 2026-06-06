#ifndef FILEZILLA_ENGINE_FTP_CWD_HEADER
#define FILEZILLA_ENGINE_FTP_CWD_HEADER

#include "ftpcontrolsocket.h"

class CFtpChangeDirOpData final : public CChangeDirOpData, public CFtpOpData
{
public:
	CFtpChangeDirOpData(CFtpControlSocket & controlSocket)
	    : CChangeDirOpData(L"CFtpChangeDirOpData")
		, CFtpOpData(controlSocket)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;

	virtual int SubcommandResult(int, COpData const&) override
	{
		return FZ_REPLY_CONTINUE;
	}

	bool tried_cdup_{};
};

#endif
