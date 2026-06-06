#ifndef FILEZILLA_ENGINE_STORJ_RMD_HEADER
#define FILEZILLA_ENGINE_STORJ_RMD_HEADER

#include "storjcontrolsocket.h"

class CStorjRemoveDirOpData final : public COpData, public CStorjOpData
{
public:
	CStorjRemoveDirOpData(CStorjControlSocket & controlSocket)
		: COpData(Command::removedir, L"CStorjRemoveDirOpData")
		, CStorjOpData(controlSocket)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;

	CServerPath path_;
};

#endif
