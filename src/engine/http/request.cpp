#include "../filezilla.h"
#include "request.h"

CHttpRequestOpData::CHttpRequestOpData(CHttpControlSocket & controlSocket, std::shared_ptr<fz::http::client::request_response_interface> const& request)
	: COpData(PrivCommand::http_request, L"CHttpRequestOpData")
	, CHttpOpData(controlSocket)
{
	if (controlSocket_.client_) {
		pending_ = 1;
		controlSocket_.client_->add_request(request);
	}
}

CHttpRequestOpData::CHttpRequestOpData(CHttpControlSocket & controlSocket, std::deque<std::shared_ptr<fz::http::client::request_response_interface>> && requests)
	: COpData(PrivCommand::http_request, L"CHttpRequestOpData")
	, CHttpOpData(controlSocket)
{
	if (controlSocket_.client_) {
		for (auto const& rr : requests) {
			controlSocket_.client_->add_request(rr);
		}
		pending_ = requests.size();
	}
}

CHttpRequestOpData::~CHttpRequestOpData()
{
}

void CHttpRequestOpData::OnResponse(uint64_t, bool success)
{
	if (!success) {
		error_ = true;
	}

	--pending_;
	if (!pending_) {
		controlSocket_.ResetOperation(error_ ? FZ_REPLY_ERROR : FZ_REPLY_OK);
	}
}

void CHttpRequestOpData::AddRequest(std::shared_ptr<fz::http::client::request_response_interface> const& rr)
{
	if (controlSocket_.client_) {
		++pending_;
		controlSocket_.client_->add_request(rr);
	}
}

int CHttpRequestOpData::Send()
{
	if (!controlSocket_.client_) {
		return FZ_REPLY_ERROR;
	}
	return FZ_REPLY_WOULDBLOCK;
}

int CHttpRequestOpData::Reset(int result)
{
	if (controlSocket_.client_) {
		controlSocket_.client_->stop(true);
	}
	return result;
}
