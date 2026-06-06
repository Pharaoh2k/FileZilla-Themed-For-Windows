#ifndef FILEZILLA_ENGINE_FTP_FILETRANSFER_HEADER
#define FILEZILLA_ENGINE_FTP_FILETRANSFER_HEADER

#include "ftpcontrolsocket.h"

class CFtpFileTransferOpData final : public CFileTransferOpData, public CFtpTransferOpData, public CFtpOpData
{
public:
	CFtpFileTransferOpData(CFtpControlSocket& controlSocket, CFileTransferCommand const& cmd);

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int prevResult, COpData const&) override;

	int TestResumeCapability();

	bool fileDidExist_{true};
};

#endif
