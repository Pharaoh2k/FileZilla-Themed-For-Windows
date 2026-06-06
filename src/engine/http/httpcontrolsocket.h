#ifndef FILEZILLA_ENGINE_HTTP_HTTPCONTROLSOCKET_HEADER
#define FILEZILLA_ENGINE_HTTP_HTTPCONTROLSOCKET_HEADER

#include "../controlsocket.h"

#include <libfilezilla/http/client.hpp>

#include <libfilezilla/file.hpp>

namespace PrivCommand {
auto const http_request = Command::private1;
}

typedef fz::http::client::request HttpRequest;
typedef fz::http::client::response HttpResponse;

typedef fz::http::client::request_response_holder<HttpRequest, HttpResponse> HttpRequestResponse;

template<typename T> void null_deleter(T *) {}

template<typename R = fz::http::client::request_response_interface, typename T>
std::shared_ptr<R> make_simple_rr(T * rr)
{
	return std::shared_ptr<R>(rr, &null_deleter<R>);
}

namespace fz {
class tls_layer;
}

class CHttpControlSocket;
class http_client : public fz::http::client::client
{
public:
	http_client(CHttpControlSocket &);
	~http_client();
	virtual fz::socket_interface* create_socket(fz::native_string const& host, unsigned short port, bool tls) override;
	virtual void destroy_socket() override;

	CHttpControlSocket & controlSocket_;

protected:
	void on_alive() override;
};

class CHttpControlSocket : public CRealControlSocket
{
public:
	CHttpControlSocket(CFileZillaEnginePrivate & engine);
	virtual ~CHttpControlSocket();

	void FileTransfer(CHttpRequestCommand const& command);

protected:
	virtual void Connect(CServer const& server, Credentials const& credentials) override;
	virtual void FileTransfer(CFileTransferCommand const& cmd) override;
	virtual int DoClose(int nErrorCode = FZ_REPLY_DISCONNECTED | FZ_REPLY_ERROR) override;

	void Request(std::shared_ptr<fz::http::client::request_response_interface> const& request);
	void Request(std::deque<std::shared_ptr<fz::http::client::request_response_interface>> && requests);

	template<typename T>
	void RequestMany(T && requests)
	{
		std::deque<std::shared_ptr<fz::http::client::request_response_interface>> rrs;
		for (auto const& request : requests) {
			rrs.push_back(request);
		}
		Request(std::move(rrs));
	}

	virtual int Disconnect() override;

	virtual bool SetAsyncRequestReply(CAsyncRequestNotification *pNotification) override;

	virtual void operator()(fz::event_base const& ev) override;
	void OnVerifyCert(fz::tls_layer* source, fz::tls_session_info& info);
	void OnRequestDone(uint64_t id, bool success);

	std::unique_ptr<fz::tls_layer> tls_layer_;

	virtual void ResetSocket() override;

	virtual void SetSocketBufferSizes() override;

	friend class CProtocolOpData<CHttpControlSocket>;
	friend class CHttpFileTransferOpData;
	friend class CHttpRequestOpData;
	friend class CHttpConnectOpData;
	friend class http_client;

private:
	std::optional<http_client> client_;
};

typedef CProtocolOpData<CHttpControlSocket> CHttpOpData;

#endif
