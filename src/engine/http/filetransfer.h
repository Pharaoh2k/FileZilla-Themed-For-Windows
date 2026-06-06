#ifndef FILEZILLA_ENGINE_HTTP_FILETRANSFER_HEADER
#define FILEZILLA_ENGINE_HTTP_FILETRANSFER_HEADER

#include "httpcontrolsocket.h"

#include <libfilezilla/file.hpp>

class CServerPath;

class CHttpFileTransferOpData final : public CFileTransferOpData, public CHttpOpData
{
public:
	CHttpFileTransferOpData(CHttpControlSocket & controlSocket, CFileTransferCommand const&);
	CHttpFileTransferOpData(CHttpControlSocket & controlSocket, CHttpRequestCommand const&);

	virtual int Send() override;
	virtual int ParseResponse() override { return FZ_REPLY_INTERNALERROR; }
	virtual int SubcommandResult(int prevResult, COpData const& previousOperation) override;

private:
	fz::http::continuation OnHeader(std::shared_ptr<HttpRequestResponse> const&);

	HttpRequestResponse rr_;

	int redirectCount_{};
};

#endif
