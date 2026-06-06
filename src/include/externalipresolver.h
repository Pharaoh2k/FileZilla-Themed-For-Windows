#ifndef FILEZILLA_ENGINE_EXTERNALIPRESOLVER_HEADER
#define FILEZILLA_ENGINE_EXTERNALIPRESOLVER_HEADER

#include "visibility.h"

#include <libfilezilla/socket.hpp>
#include <libfilezilla/http/client.hpp>

struct external_ip_resolve_event_type;
typedef fz::simple_event<external_ip_resolve_event_type> CExternalIPResolveEvent;

class FZC_PUBLIC_SYMBOL CExternalIPResolver final : public fz::event_handler, public fz::http::client::client
{
public:
	CExternalIPResolver(fz::thread_pool & pool, fz::event_handler & handler);
	virtual ~CExternalIPResolver();

	CExternalIPResolver(CExternalIPResolver const&) = delete;
	CExternalIPResolver& operator=(CExternalIPResolver const&) = delete;

	std::string GetIP() const;

	fz::http::continuation GetExternalIP(std::wstring const& resolver, fz::address_type protocol, bool force = false);

protected:

	virtual fz::socket_interface* create_socket(fz::native_string const& host, unsigned short, bool tls) override;
	virtual void destroy_socket() override;

	fz::http::continuation OnHeader(std::shared_ptr<fz::http::client::request_response_interface> const&);

	void on_request_done(uint64_t, bool success);

	fz::http::client::shared_request_response srr_;

	virtual void operator()(fz::event_base const& ev) override;

	fz::thread_pool & thread_pool_;
	fz::event_handler & handler_;

	std::unique_ptr<fz::socket> socket_;

	uint64_t redirect_count_{};
};

#endif
