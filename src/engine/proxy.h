#ifndef FILEZILLA_ENGINE_PROXY_HEADER
#define FILEZILLA_ENGINE_PROXY_HEADER

#include <libfilezilla/buffer.hpp>
#include <libfilezilla/socket.hpp>

enum class ProxyType {
	NONE,
	HTTP,
	SOCKS5,
	SOCKS4,
	SSH,

	count
};

class CControlSocket;
class ProxyBase : public fz::event_handler, public fz::socket_layer
{
public:
	ProxyBase(fz::event_handler* pEvtHandler, fz::socket_interface & next_layer, CControlSocket* pOwner,
		ProxyType t, fz::native_string const& proxy_host, unsigned int proxy_port, std::wstring const& user, std::wstring const& pass);
	virtual ~ProxyBase();

	static std::wstring Name(ProxyType t);

	fz::socket_state get_state() const override { return state_; }

	ProxyType GetProxyType() const { return type_; }
	std::wstring GetUser() const;
	std::wstring GetPass() const;

	virtual fz::native_string peer_host() const override;
	virtual int peer_port(int& error)  const override;

protected:
	CControlSocket* m_pOwner;

	ProxyType type_{};
	fz::native_string proxy_host_;
	unsigned int proxy_port_{};
	std::string user_;
	std::string pass_;

	fz::native_string host_;
	unsigned int port_{};
	fz::address_type family_{};

	fz::socket_state state_{};
};

class SimpleProxy final : public ProxyBase
{
public:
	using ProxyBase::ProxyBase;

	virtual ~SimpleProxy();

	virtual int connect(fz::native_string const& host, unsigned int port, fz::address_type family = fz::address_type::unknown) override;
	virtual int read(void *buffer, unsigned int size, int& error) override;
	virtual int write(void const* buffer, unsigned int size, int& error) override;
	virtual int shutdown() override;

private:
	virtual void operator()(fz::event_base const& ev) override;
	void OnSocketEvent(socket_event_source* source, fz::socket_event_flag t, int error);

	void OnReceive();
	void OnSend();

	int m_handshakeState{};

	fz::buffer sendBuffer_;
	fz::buffer receiveBuffer_;

	bool m_can_write{};
	bool m_can_read{};
};

namespace fz::ssh {
class client;
}

class SSHProxy final : public ProxyBase
{
public:
	using ProxyBase::ProxyBase;

	virtual ~SSHProxy();

	virtual int connect(fz::native_string const& host, unsigned int port, fz::address_type family = fz::address_type::unknown) override;
	virtual int read(void *buffer, unsigned int size, int& error) override;
	virtual int write(void const* buffer, unsigned int size, int& error) override;
	virtual int shutdown() override;
	virtual void set_event_handler(event_handler* handler, fz::socket_event_flag retrigger_block) override;

private:
	virtual void operator()(fz::event_base const& ev) override;
	void OnSocketEvent(socket_event_source* s, fz::socket_event_flag t, int error);
	void on_hostkey_verification(fz::ssh::session*, std::unique_ptr<fz::ssh::public_key> & key, fz::ssh::algorithm_info & algs);
	void on_auth_requested(fz::ssh::session*, std::string const& methods, bool is_continuation);
	void on_auth_done(fz::ssh::session*);
	void on_auth_pubkey_ok(fz::ssh::session*);
	void on_auth_signature_failed(fz::ssh::session*);


	std::unique_ptr<fz::ssh::client> ssh_;
	std::unique_ptr<fz::socket_interface> channel_;
};

std::unique_ptr<ProxyBase> CreateProxy(fz::event_handler* pEvtHandler, fz::socket_interface & next_layer, CControlSocket* pOwner,
	ProxyType t, fz::native_string const& proxy_host, unsigned int proxy_port, std::wstring const& user, std::wstring const& pass);

#endif
