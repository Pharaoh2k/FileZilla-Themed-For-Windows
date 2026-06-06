#ifndef FILEZILLA_ENGINE_STORJ_DELETE_HEADER
#define FILEZILLA_ENGINE_STORJ_DELETE_HEADER

#include "storjcontrolsocket.h"

class CStorjDeleteOpData final : public COpData, public CStorjOpData
{
public:
	CStorjDeleteOpData(CStorjControlSocket & controlSocket, CServerPath const& path, std::vector<std::wstring> && files)
		: COpData(Command::del, L"CStorjDeleteOpData")
		, CStorjOpData(controlSocket)
		, path_(path)
		, files_(files)
	{
	}

	virtual int Send() override;
	virtual int ParseResponse() override;

	CServerPath path_;
	std::vector<std::wstring> files_;

	// Set to fz::datetime::Now initially and after
	// sending an updated listing to the UI.
	fz::datetime time_;

	bool needSendListing_{};

	// Set to true if deletion of at least one file failed
	bool deleteFailed_{};
};

#endif
