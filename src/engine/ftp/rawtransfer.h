#ifndef FILEZILLA_ENGINE_FTP_RAWTRANSFER_HEADER
#define FILEZILLA_ENGINE_FTP_RAWTRANSFER_HEADER

#include "ftpcontrolsocket.h"

namespace FtpRawTransferStates {
enum type
{
        rawtransfer_init = 0,
        rawtransfer_type,
        rawtransfer_port_pasv,
        rawtransfer_rest,
        rawtransfer_transfer,
        rawtransfer_waitfinish,
        rawtransfer_waittransferpre,
        rawtransfer_waittransfer,
        rawtransfer_waitsocket
};
}

class CFtpRawTransferOpData final : public COpData, public CFtpOpData
{
public:
	CFtpRawTransferOpData(CFtpControlSocket& controlSocket)
		: COpData(PrivCommand::rawtransfer, L"CFtpRawTransferOpData")
		, CFtpOpData(controlSocket)
	{
	}

	virtual int Send() override;
	virtual int ParseResponse() override;

	std::wstring GetPassiveCommand();
	bool ParsePasvResponse();
	bool ParseEpsvResponse();

	std::wstring cmd_;

	CFtpTransferOpData* pOldData{};

	bool bPasv{true};
	bool bTriedPasv{};
	bool bTriedActive{};

private:
	std::wstring host_;
	unsigned short port_{};
};

#endif
