#ifndef FILEZILLA_ENGINE_STORJ_MKD_HEADER
#define FILEZILLA_ENGINE_STORJ_MKD_HEADER

#include "storjcontrolsocket.h"

class CStorjMkdirOpData final : public CMkdirOpData, public CStorjOpData
{
public:
	CStorjMkdirOpData(CStorjControlSocket & controlSocket)
		: CMkdirOpData(L"CStorjMkdirOpData")
		, CStorjOpData(controlSocket)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;
};

#endif
