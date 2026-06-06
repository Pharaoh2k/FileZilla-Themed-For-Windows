#ifndef FILEZILLA_ENGINE_FTP_TRANSFERSOCKET_HEADER
#define FILEZILLA_ENGINE_FTP_TRANSFERSOCKET_HEADER

#include "../controlsocket.h"

#include <optional>

class CFileZillaEnginePrivate;
class CFtpControlSocket;
class CDirectoryListingParser;

enum class TransferMode
{
	list,
	upload,
	download,
	resumetest
};

namespace fz {
class tls_layer;

#ifndef FZ_WINDOWS
#define HAVE_ASCII_TRANSFORM 1
class ascii_layer;
#endif
}

class CTransferSocket final : public fz::event_handler
{
public:
	CTransferSocket(CFileZillaEnginePrivate & engine, CFtpControlSocket & controlSocket, TransferMode transferMode);
	virtual ~CTransferSocket();

	std::wstring SetupActiveTransfer(std::string const& ip);
	bool SetupPassiveTransfer(std::wstring const& host, unsigned short port);

	void SetActive();

	CDirectoryListingParser *m_pDirectoryListingParser{};

	bool m_binaryMode{true};

	TransferEndReason GetTransferEndreason() const { return m_transferEndReason; }

	void set_reader(std::unique_ptr<fz::reader_base> && reader, bool ascii);
	void set_writer(std::unique_ptr<fz::writer_base> && writer, bool ascii);

	void ContinueWithoutSesssionResumption();

protected:
	bool DoSetupPassiveTransfer();

	bool CheckGetNextWriteBuffer();
	bool CheckGetNextReadBuffer();
	void FinalizeWrite();

	void TransferEnd(TransferEndReason reason);

	bool InitLayers(bool active);

	void ResetSocket();

	void OnSocketEvent(fz::socket_event_source* source, fz::socket_event_flag t, int error);
	void OnConnect();
	void OnAccept(int error);
	bool OnReceive();
	bool OnSend();
	void OnSocketError(int error);
	void OnTimer(fz::timer_id);

	// Create a socket server
	std::unique_ptr<fz::listen_socket> CreateSocketServer();
	std::unique_ptr<fz::listen_socket> CreateSocketServer(int port);

	void SetSocketBufferSizes(fz::socket_base & socket);

	virtual void operator()(fz::event_base const& ev);
	void OnBufferAvailability(fz::aio_waitable const* w);

	// Will be set only while creating active mode connections
	std::unique_ptr<fz::listen_socket> socketServer_;

	CFileZillaEnginePrivate & engine_;
	CFtpControlSocket & controlSocket_;

	int activity_block_{1};
	TransferEndReason m_transferEndReason{TransferEndReason::none};

	TransferMode const m_transferMode;

	bool m_postponedReceive{};
	bool m_postponedSend{};
	void TriggerPostponedEvents();

	struct passive_mode_data {
		std::string host_;
		uint16_t port_{};
		uint16_t attempts_;
	};
	std::optional<passive_mode_data> passive_mode_data_;

	std::unique_ptr<fz::socket> socket_;
	std::unique_ptr<activity_logger_layer> activity_logger_layer_;
	std::unique_ptr<fz::rate_limited_layer> ratelimit_layer_;
	std::unique_ptr<ProxyBase> proxy_layer_;
	std::unique_ptr<fz::tls_layer> tls_layer_;
#if HAVE_ASCII_TRANSFORM
	std::unique_ptr<fz::ascii_layer> ascii_layer_;
	bool use_ascii_{};
#endif

	fz::socket_layer* active_layer_{};

	// Needed for the madeProgress field in CTransferStatus
	// Initially 0, 2 if made progress
	// On uploads, 1 after first WSAE_WOULDBLOCK
	int m_madeProgress{};

	std::unique_ptr<fz::reader_base> reader_;
	std::unique_ptr<fz::writer_base> writer_;
	fz::buffer_lease buffer_;
	size_t resumetest_{};
};

#endif
