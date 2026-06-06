#include "../filezilla.h"

#include "filetransfer.h"
#include "httpcontrolsocket.h"
#include "request.h"

#include "../../include/engine_options.h"

#include "../controlsocket.h"
#include "../engineprivate.h"
#include "../tls.h"

#include <libfilezilla/file.hpp>
#include <libfilezilla/iputils.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/uri.hpp>

using namespace std::literals;

http_client::http_client(CHttpControlSocket & controlSocket)
    : fz::http::client::client(controlSocket, *controlSocket.buffer_pool_, controlSocket.logger(), "FileZilla/"s + ENGINE_VERSION)
	, controlSocket_(controlSocket)
{
}

http_client::~http_client()
{
	destroy();
}

fz::socket_interface* http_client::create_socket(fz::native_string const& host, unsigned short, bool tls)
{
#if FZ_WINDOWS
	controlSocket_.CreateSocket(host);
#else
	controlSocket_.CreateSocket(fz::to_wstring_from_utf8(host));
#endif

	if (tls) {
		controlSocket_.tls_layer_ = std::make_unique<fz::tls_layer>(controlSocket_.event_loop_, nullptr, *controlSocket_.active_layer_, &controlSocket_.engine_.GetContext().GetTlsSystemTrustStore(), controlSocket_.logger_);
		controlSocket_.active_layer_ = controlSocket_.tls_layer_.get();

		controlSocket_.tls_layer_->set_alpn("http/1.1");
		controlSocket_.tls_layer_->set_min_tls_ver(get_min_tls_ver(controlSocket_.engine_.GetOptions()));
		if (!controlSocket_.tls_layer_->client_handshake(&controlSocket_)) {
			controlSocket_.ResetSocket();
			return nullptr;
		}
	}

	return controlSocket_.active_layer_;
}

void http_client::destroy_socket()
{
	controlSocket_.ResetSocket();
}

void http_client::on_alive()
{
	controlSocket_.SetAlive();
}

// Connect is special for HTTP: It is done on a per-command basis, so we need
// to establish a connection before each command.
// The general connect of the control socket is a NOOP.
class CHttpConnectOpData final : public COpData, public CHttpOpData
{
public:
	CHttpConnectOpData(CHttpControlSocket & controlSocket)
	    : COpData(Command::connect, L"CHttpConnectOpData")
	    , CHttpOpData(controlSocket)
	{}

	virtual int Send() override {
		return controlSocket_.buffer_pool_ ? FZ_REPLY_OK : (FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED);
	}
	virtual int ParseResponse() override { return FZ_REPLY_INTERNALERROR; }
};

CHttpControlSocket::CHttpControlSocket(CFileZillaEnginePrivate & engine)
	: CRealControlSocket(engine)
{
	client_.emplace(*this);
}

CHttpControlSocket::~CHttpControlSocket()
{
	remove_handler();
	DoClose();
}

int CHttpControlSocket::DoClose(int nErrorCode)
{
	client_ = std::nullopt;
	return CRealControlSocket::DoClose(nErrorCode);
}

bool CHttpControlSocket::SetAsyncRequestReply(CAsyncRequestNotification *pNotification)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::SetAsyncRequestReply");

	switch (pNotification->GetRequestID())
	{
	case reqId_fileexists:
		{
			if (operations_.back()->opId != Command::transfer) {
				log(logmsg::debug_info, L"No or invalid operation in progress, ignoring request reply %f", pNotification->GetRequestID());
				return false;
			}

			CFileExistsNotification *pFileExistsNotification = static_cast<CFileExistsNotification *>(pNotification);
			return SetFileExistsAction(pFileExistsNotification);
		}
		break;
	case reqId_certificate:
		{
			if (!tls_layer_ || tls_layer_->get_state() != fz::socket_state::connecting) {
				log(logmsg::debug_info, L"No or invalid operation in progress, ignoring request reply %d", pNotification->GetRequestID());
				return false;
			}

			CCertificateNotification* pCertificateNotification = static_cast<CCertificateNotification *>(pNotification);
			tls_layer_->set_verification_result(pCertificateNotification->trusted_);
		}
		break;
	default:
		log(logmsg::debug_warning, L"Unknown request %d", pNotification->GetRequestID());
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return false;
	}

	return true;
}

void CHttpControlSocket::FileTransfer(CFileTransferCommand const& cmd)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::FileTransfer()");

	if (cmd.GetFlags() & transfer_flags::download) {
		log(logmsg::status, _("Downloading %s"), cmd.GetRemotePath().FormatFilename(cmd.GetRemoteFile()));
	}

	Push(std::make_unique<CHttpFileTransferOpData>(*this, cmd));
}

void CHttpControlSocket::FileTransfer(CHttpRequestCommand const& cmd)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::FileTransfer()");

	log(logmsg::status, _("Requesting %s"), cmd.uri_.to_string(!cmd.confidential_qs_));

	Push(std::make_unique<CHttpFileTransferOpData>(*this, cmd));
}

void CHttpControlSocket::Request(std::shared_ptr<fz::http::client::request_response_interface> const& request)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::Request()");

	if (!request) {
		log(logmsg::debug_warning, L"Dropping null request");
		return;
	}

	auto op = dynamic_cast<CHttpRequestOpData*>(operations_.empty() ? nullptr : operations_.back().get());
	if (op) {
		if (!client_) {
			log(logmsg::debug_warning, L"Dropping request when HTTP client already gone.");
			return;
		}
		op->AddRequest(request);
	}
	else {
		if (!client_) {
			client_.emplace(*this);
		}
		Push(std::make_unique<CHttpRequestOpData>(*this, request));
		SetWait(true);
	}
}

void CHttpControlSocket::Request(std::deque<std::shared_ptr<fz::http::client::request_response_interface>> && requests)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::Request()");
	if (!client_) {
		client_.emplace(*this);
	}
	Push(std::make_unique<CHttpRequestOpData>(*this, std::move(requests)));
	SetWait(true);
}

void CHttpControlSocket::ResetSocket()
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::ResetSocket()");

	active_layer_ = nullptr;

	tls_layer_.reset();

	CRealControlSocket::ResetSocket();
}

int CHttpControlSocket::Disconnect()
{
	DoClose();
	return FZ_REPLY_OK;
}

void CHttpControlSocket::Connect(CServer const& server, Credentials const& credentials)
{
	currentServer_ = server;
	credentials_ = credentials;
	Push(std::make_unique<CHttpConnectOpData>(*this));
}

void CHttpControlSocket::SetSocketBufferSizes()
{
	if (!socket_) {
		return;
	}

	const int size_read = engine_.GetOptions().get_int(OPTION_SOCKET_BUFFERSIZE_RECV);
	const int size_write = -1;
	socket_->set_buffer_sizes(size_read, size_write);
}

void CHttpControlSocket::operator()(fz::event_base const& ev)
{
	if (fz::dispatch<fz::certificate_verification_event, fz::http::client::done_event>(ev, this, &CHttpControlSocket::OnVerifyCert, &CHttpControlSocket::OnRequestDone)) {
		return;
	}
	CRealControlSocket::operator()(ev);
}

void CHttpControlSocket::OnVerifyCert(fz::tls_layer* source, fz::tls_session_info& info)
{
	if (!tls_layer_ || source != tls_layer_.get()) {
		return;
	}

	SendAsyncRequest(std::make_unique<CCertificateNotification>(std::move(info)));
}

void CHttpControlSocket::OnRequestDone(uint64_t id, bool success)
{
	auto op = dynamic_cast<CHttpRequestOpData*>(operations_.empty() ? nullptr : operations_.back().get());
	if (op) {
		op->OnResponse(id, success);
	}
}
