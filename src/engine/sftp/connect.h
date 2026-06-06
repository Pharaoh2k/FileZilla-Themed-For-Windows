#ifndef FILEZILLA_ENGINE_SFTP_CONNECT_HEADER
#define FILEZILLA_ENGINE_SFTP_CONNECT_HEADER

#include "sftpcontrolsocket.h"

#include <fzssh/agent.hpp>
#include <fzssh/ssh.hpp>
#include <fzssh/privkey.hpp>

#include <set>
#include <variant>

namespace fz::ssh {
class session;
class agent_connection;
}

class CSftpConnectOpData final : public COpData, public CSftpOpData
{
public:
	CSftpConnectOpData(CSftpControlSocket & controlSocket)
		: COpData(Command::connect, L"CSftpConnectOpData")
		, CSftpOpData(controlSocket)
	{}

	virtual ~CSftpConnectOpData();

	virtual int Send() override;
	virtual int ParseResponse() override { return FZ_REPLY_INTERNALERROR; }

	void on_auth_requested(fz::ssh::session*, std::string const& methods, bool is_continuation);
	void on_auth_done(fz::ssh::session*);
	void on_auth_pubkey_ok(fz::ssh::session*);
	void on_auth_signature_failed(fz::ssh::session*);
	void on_auth_keyboard_interactive_prompt(fz::ssh::session*, std::string const&, std::string const&, std::vector<fz::ssh::keyboard_interactive_prompt> & prompts);
	void on_sftp_ready(fz::ssh::sftp::sftp_client*);

	void next_auth();
	bool load_keys();
	bool auth_with_key();
	void ask_key_password(bool repeated);
	void set_keyfile_password(std::string const& pw);
	void set_password(std::string const& pw);

private:
	void set_keys_loaded();

	virtual void operator()(fz::event_base const& ev) override;
	void on_agent_keys(fz::ssh::agent_connection* conn, std::vector<std::unique_ptr<fz::ssh::private_key>> & keys);

	std::vector<fz::ssh::private_key_info> keys_;
	std::unique_ptr<fz::ssh::agent_connection> agent_;

	std::string methods_;
	std::string_view method_;
	std::set<std::string> used_keys_{};

	uint8_t retry_counter_{};
	bool keys_loaded_{};
	bool tried_pw_{};
	bool tried_interactive_{};
	bool tried_key_{};
};

#endif
