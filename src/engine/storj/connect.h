#ifndef FILEZILLA_ENGINE_STORJ_CONNECT_HEADER
#define FILEZILLA_ENGINE_STORJ_CONNECT_HEADER

#include "storjcontrolsocket.h"

namespace storjConnectStates {
enum type {
	connect_init,
	connect_host,
	connect_user,
	connect_pass
};
}

class CStorjConnectOpData final : public COpData, public CStorjOpData
{
public:
	CStorjConnectOpData(CStorjControlSocket & controlSocket)
		: COpData(Command::connect, L"CStorjConnectOpData")
		, CStorjOpData(controlSocket)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;
};

#endif
