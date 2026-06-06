#include "../filezilla.h"
#include "../activity_logger_layer.h"
#include "../directorylistingparser.h"
#include "../engineprivate.h"
#include "../proxy.h"
#include "../servercapabilities.h"
#include "../tls.h"

#include "ftpcontrolsocket.h"
#include "transfersocket.h"

#include "../../include/engine_options.h"

#include <libfilezilla/rate_limited_layer.hpp>
#include <libfilezilla/util.hpp>

using namespace std::literals;

#if HAVE_ASCII_TRANSFORM
#include <libfilezilla/ascii_layer.hpp>
#endif

CTransferSocket::CTransferSocket(CFileZillaEnginePrivate & engine, CFtpControlSocket & controlSocket, TransferMode transferMode)
	: fz::event_handler(controlSocket, fz::child_event_handler)
	, engine_(engine)
	, controlSocket_(controlSocket)
	, m_transferMode(transferMode)
{
}

CTransferSocket::~CTransferSocket()
{
	remove_handler();
	if (m_transferEndReason == TransferEndReason::none) {
		m_transferEndReason = TransferEndReason::successful;
	}
	ResetSocket();

	reader_.reset();
	writer_.reset();
}

void CTransferSocket::set_reader(std::unique_ptr<fz::reader_base> && reader, [[maybe_unused]] bool ascii)
{
#if HAVE_ASCII_TRANSFORM
	use_ascii_ = ascii;
#endif
	reader_ = std::move(reader);
}

void CTransferSocket::set_writer(std::unique_ptr<fz::writer_base> && writer, [[maybe_unused]] bool ascii)
{
#if HAVE_ASCII_TRANSFORM
	use_ascii_ = ascii;
#endif
	writer_ = std::move(writer);
}

void CTransferSocket::ResetSocket()
{
	socketServer_.reset();

	active_layer_ = nullptr;

#if HAVE_ASCII_TRANSFORM
	ascii_layer_.reset();
#endif
	tls_layer_.reset();
	proxy_layer_.reset();
	ratelimit_layer_.reset();
	activity_logger_layer_.reset();
	socket_.reset();
	buffer_.release();
}

std::wstring CTransferSocket::SetupActiveTransfer(std::string const& ip)
{
	passive_mode_data_.reset();
	ResetSocket();
	socketServer_ = CreateSocketServer();

	if (!socketServer_) {
		controlSocket_.log(logmsg::debug_warning, L"CreateSocketServer failed");
		return std::wstring();
	}

	int error;
	int port = socketServer_->local_port(error);
	if (port == -1)	{
		ResetSocket();

		controlSocket_.log(logmsg::debug_warning, L"GetLocalPort failed: %s", fz::socket_error_description(error));
		return std::wstring();
	}

	if (engine_.GetOptions().get_int(OPTION_LIMITPORTS)) {
		port += static_cast<int>(engine_.GetOptions().get_int(OPTION_LIMITPORTS_OFFSET));
		if (port <= 0 || port >= 65536) {
			controlSocket_.log(logmsg::debug_warning, L"Port outside valid range");
			return std::wstring();
		}
	}

	std::wstring portArguments;
	if (socketServer_->address_family() == fz::address_type::ipv6) {
		portArguments = fz::sprintf(L"|2|%s|%d|", ip, port);
	}
	else {
		portArguments = fz::to_wstring(ip);
		fz::replace_substrings(portArguments, L".", L",");
		portArguments += fz::sprintf(L",%d,%d", port / 256, port % 256);
	}

	return portArguments;
}

void CTransferSocket::OnSocketEvent(fz::socket_event_source* source, fz::socket_event_flag t, int error)
{
	if (socketServer_) {
		if (t == fz::socket_event_flag::connection) {
			OnAccept(error);
		}
		else {
			controlSocket_.log(logmsg::debug_info, L"Unhandled socket event %d from listening socket", t);
		}
		return;
	}

	switch (t)
	{
	case fz::socket_event_flag::connection:
		if (error) {
			if (source == proxy_layer_.get()) {
				controlSocket_.log(logmsg::error, _("Proxy handshake failed: %s"), fz::socket_error_description(error));
			}
			else {
				controlSocket_.log(logmsg::error, _("The data connection could not be established: %s"), fz::socket_error_description(error));
			}
			if (error == EADDRINUSE && passive_mode_data_ && ++passive_mode_data_->attempts_ < 2) {
				if (DoSetupPassiveTransfer()) {
					controlSocket_.log(logmsg::status, _("Retrying..."));
					return;
				}
			}
			TransferEnd(TransferEndReason::transfer_failure);
		}
		else {
			OnConnect();
		}
		break;
	case fz::socket_event_flag::read:
		if (error) {
			OnSocketError(error);
		}
		else {
			if (OnReceive()) {
				resend_current_event();
			}
		}
		break;
	case fz::socket_event_flag::write:
		if (error) {
			OnSocketError(error);
		}
		else {
			if (OnSend()) {
				resend_current_event();
			}
		}
		break;
	default:
		// Uninteresting
		break;
	}
}

void CTransferSocket::OnAccept(int error)
{
	controlSocket_.SetAlive();
	controlSocket_.log(logmsg::debug_verbose, L"CTransferSocket::OnAccept(%d)", error);

	if (!socketServer_) {
		controlSocket_.log(logmsg::debug_warning, L"No socket server in OnAccept", error);
		return;
	}

	socket_ = socketServer_->accept(error);
	if (!socket_) {
		if (error == EAGAIN) {
			controlSocket_.log(logmsg::debug_verbose, L"No pending connection");
		}
		else {
			controlSocket_.log(logmsg::status, _("Could not accept connection: %s"), fz::socket_error_description(error));
			TransferEnd(TransferEndReason::transfer_failure);
		}
		return;
	}
	socketServer_.reset();

	if (!InitLayers(true)) {
		TransferEnd(TransferEndReason::transfer_failure);
		return;
	}

	if (active_layer_->get_state() == fz::socket_state::connected) {
		OnConnect();
	}
}

void CTransferSocket::OnConnect()
{
	controlSocket_.SetAlive();
	controlSocket_.log(logmsg::debug_verbose, L"CTransferSocket::OnConnect");

	if (!socket_) {
		controlSocket_.log(logmsg::debug_verbose, L"CTransferSocket::OnConnect called without socket");
		return;
	}

	if (tls_layer_) {
		auto const cap = CServerCapabilities::GetCapability(controlSocket_.currentServer_, tls_resumption);

		if (controlSocket_.tls_layer_->get_alpn() == "x-filezilla-ftp"sv) {
			if (!tls_layer_->resumed_session()) {
				TransferEnd(TransferEndReason::failed_tls_resumption);
				return;
			}
			else if (tls_layer_->get_alpn() != "ftp-data"sv) {
				controlSocket_.log(logmsg::error, _("Wrong ALPN on data connection"));
				TransferEnd(TransferEndReason::wrong_tls_alpn);
				return;
			}

			if (cap != yes) {
				engine_.AddNotification(std::make_unique<FtpTlsResumptionNotification>(controlSocket_.currentServer_));
				CServerCapabilities::SetCapability(controlSocket_.currentServer_, tls_resumption, yes);
			}
		}
		else {
			if (tls_layer_->resumed_session()) {
				if (cap != yes) {
					engine_.AddNotification(std::make_unique<FtpTlsResumptionNotification>(controlSocket_.currentServer_));
					CServerCapabilities::SetCapability(controlSocket_.currentServer_, tls_resumption, yes);
				}
			}
			else {
				if (cap == yes) {
					TransferEnd(TransferEndReason::failed_tls_resumption);
					return;
				}
				else if (cap == unknown) {
					// Ask whether to allow this insecure connection
					++activity_block_;
					controlSocket_.SendAsyncRequest(std::make_unique<FtpTlsNoResumptionNotification>(controlSocket_.currentServer_));
				}
			}
		}
		// Re-enable Nagle algorithm
		socket_->set_flags(fz::socket::flag_nodelay, false);
	}

#ifdef FZ_WINDOWS
	if (m_transferMode == TransferMode::upload) {
		// For send buffer tuning
		add_timer(fz::duration::from_seconds(1), false);
	}
#endif

	if (!activity_block_) {
		TriggerPostponedEvents();
	}

	if (OnSend()) {
		send_event<fz::socket_event>(active_layer_, fz::socket_event_flag::write, 0);
	}
}

bool CTransferSocket::OnReceive()
{
	controlSocket_.log(logmsg::debug_debug, L"CTransferSocket::OnReceive(), m_transferMode=%d", m_transferMode);

	if (activity_block_) {
		controlSocket_.log(logmsg::debug_verbose, L"Postponing receive, m_bActive was false.");
		m_postponedReceive = true;
		return false;
	}

	if (m_transferEndReason == TransferEndReason::none) {
		if (m_transferMode == TransferMode::list) {
			constexpr size_t to_read = 1024 * 16;
			fz::buffer & buf = m_pDirectoryListingParser->GetInputBuffer();

			int error;
			int numread = active_layer_->read(buf.get(1024 * 16), to_read, error);
			if (numread < 0) {
				if (error != EAGAIN) {
					controlSocket_.log(logmsg::error, L"Could not read from transfer socket: %s", fz::socket_error_description(error));
					TransferEnd(TransferEndReason::transfer_failure);
				}
			}
			else if (numread > 0) {
				buf.add(numread);
				if (!m_pDirectoryListingParser->ProcessAddedData()) {
					TransferEnd(TransferEndReason::transfer_failure);
					return false;
				}

				controlSocket_.SetAlive();
				if (!m_madeProgress) {
					m_madeProgress = 2;
					engine_.transfer_status_.SetMadeProgress();
				}
				engine_.transfer_status_.Update(numread);
				return true;
			}
			else {
				TransferEnd(TransferEndReason::successful);
			}
			return false;
		}
		else if (m_transferMode == TransferMode::download) {
			if (!CheckGetNextWriteBuffer()) {
				return false;
			}

			int error{};
			size_t to_read = buffer_->capacity() - buffer_->size();
			int numread = active_layer_->read(buffer_->get(to_read), static_cast<unsigned int>(to_read), error);

			if (numread < 0) {
				if (error != EAGAIN) {
					controlSocket_.log(logmsg::error, L"Could not read from transfer socket: %s", fz::socket_error_description(error));
					TransferEnd(TransferEndReason::transfer_failure);
				}
			}
			else {
				controlSocket_.SetAlive();
				if (!m_madeProgress) {
					m_madeProgress = 2;
					engine_.transfer_status_.SetMadeProgress();
				}

				if (!numread) {
					FinalizeWrite();
				}
				else {
					buffer_->add(static_cast<size_t>(numread));
					return true;
				}
			}
			return false;
		}
		else if (m_transferMode == TransferMode::resumetest) {
			for (;;) {
				char tmp[2];
				int error;
				int numread = active_layer_->read(tmp, 2, error);
				if (numread < 0) {
					if (error != EAGAIN) {
						controlSocket_.log(logmsg::error, L"Could not read from transfer socket: %s", fz::socket_error_description(error));
						TransferEnd(TransferEndReason::transfer_failure);
					}
					return false;
				}

				if (!numread) {
					if (resumetest_ == 1) {
						TransferEnd(TransferEndReason::successful);
					}
					else {
						controlSocket_.log(logmsg::debug_warning, L"Server incorrectly sent %d bytes", resumetest_);
						TransferEnd(TransferEndReason::failed_resumetest);
					}
					return false;
				}
				resumetest_ += numread;

				if (resumetest_ > 1) {
					controlSocket_.log(logmsg::debug_warning, L"Server incorrectly sent %d bytes", resumetest_);
					TransferEnd(TransferEndReason::failed_resumetest);
					return false;
				}
			}
			return false;
		}
	}

	char discard[1024];
	int error;
	int numread = active_layer_->read(discard, 1024, error);

	if (m_transferEndReason == TransferEndReason::none) {
		// If we get here we're uploading

		if (numread > 0) {
			controlSocket_.log(logmsg::error, L"Received data from the server during an upload");
			TransferEnd(TransferEndReason::transfer_failure);
		}
		else if (numread < 0 && error != EAGAIN) {
			controlSocket_.log(logmsg::error, L"Could not read from transfer socket: %s", fz::socket_error_description(error));
			TransferEnd(TransferEndReason::transfer_failure);
		}
	}
	else {
		if (!numread || (numread < 0 && error != EAGAIN)) {
			ResetSocket();
		}
	}
	return false;
}

bool CTransferSocket::OnSend()
{
	if (!active_layer_) {
		controlSocket_.log(logmsg::debug_verbose, L"OnSend called without backend. Ignoring event.");
		return false;
	}

	if (activity_block_) {
		controlSocket_.log(logmsg::debug_verbose, L"Postponing send");
		m_postponedSend = true;
		return false;
	}

	if (m_transferMode != TransferMode::upload || m_transferEndReason != TransferEndReason::none) {
		return false;
	}

	if (!CheckGetNextReadBuffer()) {
		return false;
	}

	int error{};
	int written = active_layer_->write(buffer_->get(), static_cast<int>(buffer_->size()), error);
	if (written <= 0) {
		if (error == EAGAIN) {
			if (!m_madeProgress) {
				controlSocket_.log(logmsg::debug_debug, L"First EAGAIN in CTransferSocket::OnSend()");
				m_madeProgress = 1;
				engine_.transfer_status_.SetMadeProgress();
			}
		}
		else {
			controlSocket_.log(logmsg::error, L"Could not write to transfer socket: %s", fz::socket_error_description(error));
			TransferEnd(TransferEndReason::transfer_failure);
		}
		return false;
	}
	else {
		controlSocket_.SetAlive();
		if (m_madeProgress == 1) {
			controlSocket_.log(logmsg::debug_debug, L"Made progress in CTransferSocket::OnSend()");
			m_madeProgress = 2;
			engine_.transfer_status_.SetMadeProgress();
		}
		engine_.transfer_status_.Update(written);

		buffer_->consume(written);

		return true;
	}

}

void CTransferSocket::OnSocketError(int error)
{
	controlSocket_.log(logmsg::debug_verbose, L"CTransferSocket::OnSocketError(%d)", error);

	if (m_transferEndReason != TransferEndReason::none) {
		return;
	}

	controlSocket_.log(logmsg::error, _("Transfer connection interrupted: %s"), fz::socket_error_description(error));
	TransferEnd(TransferEndReason::transfer_failure);
}

bool CTransferSocket::SetupPassiveTransfer(std::wstring const& host, unsigned short port)
{
	passive_mode_data_.emplace();
	passive_mode_data_->host_ = fz::to_utf8(host);
	passive_mode_data_->port_ = port;
	return DoSetupPassiveTransfer();
}

bool CTransferSocket::DoSetupPassiveTransfer()
{
	// Do this before ResetSocket specifically to ensure different local port in case of repeat attempts.
	auto s = std::make_unique<fz::socket>(engine_.GetThreadPool(), nullptr);

	ResetSocket();

	socket_ = std::move(s);

	SetSocketBufferSizes(*socket_);

	// Try to bind the source IP of the data connection to the same IP as the control connection.
	// We can do so either if
	// 1) the destination IP of the data connection matches peer IP of the control connection or
	// 2) we are using a proxy.
	//
	// In case destination IPs of control and data connection are different, do not bind to the
	// same source.

	std::string bindAddress;
	if (controlSocket_.proxy_layer_) {
		bindAddress = controlSocket_.socket_->local_ip();
		controlSocket_.log(logmsg::debug_info, L"Binding data connection source IP to control connection source IP %s", bindAddress);
		socket_->bind(bindAddress);
	}
	else {
		if (controlSocket_.socket_->peer_ip(true) == passive_mode_data_->host_ || controlSocket_.socket_->peer_ip(false) == passive_mode_data_->host_) {
			bindAddress = controlSocket_.socket_->local_ip();
			controlSocket_.log(logmsg::debug_info, L"Binding data connection source IP to control connection source IP %s", bindAddress);
			socket_->bind(bindAddress);
		}
		else {
			controlSocket_.log(logmsg::debug_warning, L"Destination IP of data connection does not match peer IP of control connection. Not binding source address of data connection.");
		}
	}

	if (!InitLayers(false)) {
		ResetSocket();
		return false;
	}

	int res = active_layer_->connect(fz::to_native(passive_mode_data_->host_), passive_mode_data_->port_, fz::address_type::unknown);
	if (res) {
		ResetSocket();
		return false;
	}

	return true;
}

bool CTransferSocket::InitLayers(bool active)
{
	activity_logger_layer_ = std::make_unique<activity_logger_layer>(nullptr, *socket_, engine_.activity_logger_);
	ratelimit_layer_ = std::make_unique<fz::rate_limited_layer>(nullptr, *activity_logger_layer_, &engine_.GetRateLimiter());
	active_layer_ = ratelimit_layer_.get();

	if (controlSocket_.proxy_layer_ && !active) {
		fz::native_string proxy_host = controlSocket_.proxy_layer_->next().peer_host();
		int error;
		int proxy_port = controlSocket_.proxy_layer_->next().peer_port(error);

		if (proxy_host.empty() || proxy_port < 1) {
			controlSocket_.log(logmsg::debug_warning, L"Could not get peer address of control connection.");
			return false;
		}

		proxy_layer_ = CreateProxy(nullptr, *active_layer_, &controlSocket_, controlSocket_.proxy_layer_->GetProxyType(), proxy_host, proxy_port, controlSocket_.proxy_layer_->GetUser(), controlSocket_.proxy_layer_->GetPass());
		active_layer_ = proxy_layer_.get();
	}

	if (controlSocket_.m_protectDataChannel) {
		// Disable Nagle's algorithm during TLS handshake
		socket_->set_flags(fz::socket::flag_nodelay, true);

		tls_layer_ = std::make_unique<fz::tls_layer>(controlSocket_.event_loop_, nullptr, *active_layer_, nullptr, controlSocket_.logger_);
		active_layer_ = tls_layer_.get();
		tls_layer_->set_min_tls_ver(get_min_tls_ver(engine_.GetOptions()));

		if (controlSocket_.tls_layer_->get_alpn() == "x-filezilla-ftp"sv) {
			tls_layer_->set_alpn("ftp-data"sv);
		}
		if (!tls_layer_->client_handshake(controlSocket_.tls_layer_->get_raw_certificate(), controlSocket_.tls_layer_->get_session_parameters(), controlSocket_.tls_layer_->peer_host())) {
			return false;
		}
	}

#if HAVE_ASCII_TRANSFORM
	if (use_ascii_) {
		ascii_layer_ = std::make_unique<fz::ascii_layer>(event_loop_, nullptr, *active_layer_);
		active_layer_ = ascii_layer_.get();
	}
#endif

	active_layer_->set_event_handler(this);

	return true;
}

void CTransferSocket::SetActive()
{
	if (m_transferEndReason != TransferEndReason::none) {
		return;
	}

	if (!activity_block_) {
		return;
	}
	--activity_block_;
	if (!socket_) {
		return;
	}

	if (socket_->is_connected()) {
		TriggerPostponedEvents();
	}
}

void CTransferSocket::TransferEnd(TransferEndReason reason)
{
	controlSocket_.log(logmsg::debug_verbose, L"CTransferSocket::TransferEnd(%d)", reason);

	if (m_transferEndReason != TransferEndReason::none) {
		return;
	}
	m_transferEndReason = reason;

	if (reason != TransferEndReason::successful) {
		ResetSocket();
	}
	else {
		active_layer_->shutdown();
	}

	controlSocket_.send_event<TransferEndEvent>();
}

std::unique_ptr<fz::listen_socket> CTransferSocket::CreateSocketServer(int port)
{
	auto socket = std::make_unique<fz::listen_socket>(engine_.GetThreadPool(), this);
	int res = socket->listen(controlSocket_.socket_->address_family(), port);
	if (res) {
		controlSocket_.log(logmsg::debug_verbose, L"Could not listen on port %d: %s", port, fz::socket_error_description(res));
		socket.reset();
	}
	else {
		SetSocketBufferSizes(*socket);
	}

	return socket;
}

std::unique_ptr<fz::listen_socket> CTransferSocket::CreateSocketServer()
{
	if (!engine_.GetOptions().get_int(OPTION_LIMITPORTS)) {
		// Ask the systen for a port
		return CreateSocketServer(0);
	}

	// Try out all ports in the port range.
	// Upon first call, we try to use a random port fist, after that
	// increase the port step by step

	// Windows only: I think there's a bug in the socket implementation of
	// Windows: Even if using SO_REUSEADDR, using the same local address
	// twice will fail unless there are a couple of minutes between the
	// connection attempts. This may cause problems if transferring lots of
	// files with a narrow port range.

	static int start = 0;

	int low = engine_.GetOptions().get_int(OPTION_LIMITPORTS_LOW);
	int high = engine_.GetOptions().get_int(OPTION_LIMITPORTS_HIGH);
	if (low > high) {
		low = high;
	}

	if (start < low || start > high) {
		start = static_cast<decltype(start)>(fz::random_number(low, high));
	}

	std::unique_ptr<fz::listen_socket> server;

	int count = high - low + 1;
	while (count--) {
		server = CreateSocketServer(start++);
		if (server) {
			break;
		}
		if (start > high) {
			start = low;
		}
	}

	return server;
}

bool CTransferSocket::CheckGetNextWriteBuffer()
{
	auto res = fz::aio_result::ok;
	if (buffer_ && buffer_->size() >= buffer_->capacity()) {
		res = writer_->add_buffer(std::move(buffer_), *this);
	}
	if (res == fz::aio_result::ok && !buffer_) {
		buffer_ = controlSocket_.buffer_pool_->get_buffer(*this);
		if (!buffer_) {
			res = fz::aio_result::wait;
		}
	}
	if (res == fz::aio_result::wait) {
		return false;
	}
	else if (res == fz::aio_result::error) {
		TransferEnd(TransferEndReason::transfer_failure_critical);
		return false;
	}

	return true;
}

bool CTransferSocket::CheckGetNextReadBuffer()
{
	if (buffer_->empty()) {
		buffer_.release();
		fz::aio_result res;
		std::tie(res, buffer_) = reader_->get_buffer(*this);

		if (res == fz::aio_result::wait) {
			return false;
		}
		else if (res == fz::aio_result::error) {
			TransferEnd(TransferEndReason::transfer_failure_critical);
			return false;
		}

		if (buffer_->empty()) {
			int r = active_layer_->shutdown();
			if (r) {
				if (r != EAGAIN) {
					TransferEnd(TransferEndReason::transfer_failure);
				}
				return false;
			}
			TransferEnd(TransferEndReason::successful);
			return false;
		}
	}
	return true;
}

void CTransferSocket::OnBufferAvailability(fz::aio_waitable const* w)
{
	if (w == reader_.get()) {
		if (OnSend()) {
			send_event<fz::socket_event>(active_layer_, fz::socket_event_flag::write, 0);
		}
	}
	else if (w == writer_.get() || w == &*controlSocket_.buffer_pool_) {
		if (OnReceive()) {
			send_event<fz::socket_event>(active_layer_, fz::socket_event_flag::read, 0);
		}
	}
}

void CTransferSocket::FinalizeWrite()
{
	controlSocket_.log(logmsg::debug_debug, L"CTransferSocket::FinalizeWrite()");
	if (m_transferEndReason != TransferEndReason::none) {
		return;
	}

	auto res = fz::aio_result::ok;
	if (!buffer_->empty()) {
		res = writer_->add_buffer(std::move(buffer_), *this);
	}
	if (res == fz::aio_result::ok) {
		res = writer_->finalize(*this);
	}
	if (res == fz::aio_result::wait) {
		return;
	}

	if (res == fz::aio_result::ok) {
		TransferEnd(TransferEndReason::successful);
	}
	else {
		TransferEnd(TransferEndReason::transfer_failure_critical);
	}
}

void CTransferSocket::TriggerPostponedEvents()
{
	if (activity_block_) {
		return;
	}

	if (m_postponedReceive) {
		controlSocket_.log(logmsg::debug_verbose, L"Executing postponed receive");
		m_postponedReceive = false;
		if (OnReceive()) {
			send_event<fz::socket_event>(active_layer_, fz::socket_event_flag::read, 0);
		}
		if (m_transferEndReason != TransferEndReason::none) {
			return;
		}
	}
	if (m_postponedSend) {
		controlSocket_.log(logmsg::debug_verbose, L"Executing postponed send");
		m_postponedSend = false;
		if (OnSend()) {
			send_event<fz::socket_event>(active_layer_, fz::socket_event_flag::write, 0);
		}
	}
}

void CTransferSocket::SetSocketBufferSizes(fz::socket_base& socket)
{
	const int size_read = engine_.GetOptions().get_int(OPTION_SOCKET_BUFFERSIZE_RECV);
	const int size_write = -1;
	socket.set_buffer_sizes(size_read, size_write);
}

void CTransferSocket::operator()(fz::event_base const& ev)
{
	fz::dispatch<fz::socket_event, fz::aio_buffer_event, fz::timer_event>(ev, this,
		&CTransferSocket::OnSocketEvent,
		&CTransferSocket::OnBufferAvailability,
		&CTransferSocket::OnTimer);
}

void CTransferSocket::OnTimer(fz::timer_id)
{
#if FZ_WINDOWS
	if (socket_ && socket_->is_connected()) {
		int const ideal_send_buffer = socket_->ideal_send_buffer_size();
		if (ideal_send_buffer != -1) {
			socket_->set_buffer_sizes(-1, ideal_send_buffer);
		}
	}
#endif
}

void CTransferSocket::ContinueWithoutSesssionResumption()
{
	if (activity_block_) {
		--activity_block_;
		TriggerPostponedEvents();
	}
}
