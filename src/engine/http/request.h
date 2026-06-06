#ifndef FILEZILLA_ENGINE_HTTP_REQUEST_HEADER
#define FILEZILLA_ENGINE_HTTP_REQUEST_HEADER

#include "httpcontrolsocket.h"

#include <libfilezilla/buffer.hpp>

class CServerPath;

class CHttpRequestOpData final : public COpData, public CHttpOpData
{
public:
	CHttpRequestOpData(CHttpControlSocket & controlSocket, std::shared_ptr<fz::http::client::request_response_interface> const& request);
	CHttpRequestOpData(CHttpControlSocket & controlSocket, std::deque<std::shared_ptr<fz::http::client::request_response_interface>> && requests);
	virtual ~CHttpRequestOpData();

	virtual int Send() override;
	virtual int ParseResponse() override { return FZ_REPLY_INTERNALERROR; }
	virtual int Reset(int result) override;

	void AddRequest(std::shared_ptr<fz::http::client::request_response_interface> const& rr);

	void OnResponse(uint64_t id, bool success);
private:
	bool error_{};
	size_t pending_{};
};

#endif
