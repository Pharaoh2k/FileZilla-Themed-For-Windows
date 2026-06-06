#include "../filezilla.h"
#include "../../include/engine_options.h"
#include <fzssh/agent.hpp>

#include "connect.h"

#include <fzssh/client.hpp>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

namespace sftpConnectStates {
enum type {
	init,
	connection
};
}

CSftpConnectOpData::~CSftpConnectOpData()
{
	remove_handler();
	agent_.reset();
}

int CSftpConnectOpData::Send()
{
	switch (opState) {
	case sftpConnectStates::init:
		{
			opState = sftpConnectStates::connection;
			int ret = controlSocket_.DoConnect(currentServer_.GetHost(), currentServer_.GetPort());
			if (ret == FZ_REPLY_WOULDBLOCK) {
				// Enable TCP_NODELAY, speeds things up a bit.
				controlSocket_.socket_->set_flags(fz::socket::flag_nodelay | fz::socket::flag_keepalive, true);
			}
			return ret;
		}
	case sftpConnectStates::connection:
		{
			fz::ssh::client_parameters params;
			params.kex_ += ",diffie-hellman-group1-sha1,diffie-hellman-group-exchange-sha1"sv;
			params.cipher_ += ",aes256-cbc,aes192-cbc,aes128-cbc"sv;
			params.hostkey_signatures_ += ",ssh-rsa"sv;
			params.single_channel_ = true;
			params.softwareversion_ = "FileZilla"sv;
			params.softwareversion_ += ' ';
			params.softwareversion_ += PACKAGE_VERSION;
			for (auto & c : params.softwareversion_) {
				if (c == ' ' || c == '-') {
					c = '_';
				}
			}

			if (currentServer_.GetExtraParameter("allow_non_crlf_identification_string"sv) == L"1") {
				params.compatibility_flags_ |= fz::ssh::compatibility_flags::identification_string_not_terminated_by_crlf;
			}

			std::string user = (controlSocket_.credentials_.logonType_ == LogonType::anonymous) ? "anonymous" : fz::to_utf8(currentServer_.GetUser());
			controlSocket_.ssh_ = std::make_unique<fz::ssh::client>(params, user, *controlSocket_.active_layer_, controlSocket_, controlSocket_.logger_);
			opState = sftpConnectStates::connection;
			return FZ_REPLY_WOULDBLOCK;
		}
	default:
		break;
	}
	return FZ_REPLY_INTERNALERROR;
}

namespace {
bool method_available(std::string_view const& methods, std::string_view method)
{
	for (auto m : fz::strtokenizer(methods, ","sv, true)) {
		if (m == method) {
			return true;
		}
	}
	return false;
}
}

bool CSftpConnectOpData::load_keys()
{
	if (keys_loaded_) {
		return false;
	}

	auto names = controlSocket_.credentials_.keyFile_;
	names += '\n';
	names += options_.get_string(OPTION_SFTP_KEYFILES);
	std::set<std::wstring> seen_names;
	for (auto k : fz::strtokenizer(names, L"\r\n"sv, true)) {
		if (!seen_names.emplace(k).second) {
			continue;
		}
		fz::buffer b;
		if (!fz::read_file(fz::to_native(k), b, 128*1024)) {
			log(logmsg::error, _("Could not read key file '%s'."), k);
			continue;
		}

		auto infos = fz::ssh::load_private_key_infos(b.to_view(), controlSocket_.logger_, {});
		if (infos.empty()) {
			log(logmsg::error, _("The file '%s' does not contain any private keys"), k);
			continue;
		}

		for (auto & i : infos) {
			if (i.pubkey_ && !used_keys_.emplace(i.pubkey_->pubkey_blob()).second) {
				continue;
			}
			i.name_ = fz::to_utf8(k);
			keys_.emplace_back(std::move(i));
		}
	}

#ifndef USE_MAC_SANDBOX
	fz::ssh::agent_compatibility_flags flags{};
	if (currentServer_.GetExtraParameter("allow_agent_keys_of_unknown_type"sv) == L"1") {
		flags |= fz::ssh::agent_compatibility_flags::allow_keys_with_unknown_types;
	}
	agent_ = std::make_unique<fz::ssh::agent_connection>(engine_.GetThreadPool(), *this, controlSocket_.logger_, flags);
	agent_->get_keys(*this);
	return true;
#else
	set_keys_loaded();
	return false;
#endif
}

bool CSftpConnectOpData::auth_with_key()
{
	if (load_keys()) {
		return true;
	}

	while (!keys_.empty()) {
		method_ = "publickey"sv;

		auto & key = keys_.back();

		if (key.pubkey_) {
			tried_key_ = true;
			used_keys_.emplace(key.pubkey_->pubkey_blob());
			log(logmsg::command, _("Authenticating with public key"));
			controlSocket_.ssh_->auth_with_key(key.pubkey_);
			return true;
		}

		if (!key.privkey_) {
			ask_key_password(false);
			return true;
		}

		log(logmsg::debug_warning, _("Bad key info"));
		keys_.pop_back();
	}

	return false;
}

void CSftpConnectOpData::on_auth_requested(fz::ssh::session*, std::string const& methods, bool is_continuation)
{
	controlSocket_.SetAlive();
	methods_ = methods;

	if (!is_continuation) {
		if (tried_key_ || tried_pw_ || tried_interactive_) {
			log(logmsg::reply, _("Authentication failed"));
			if (tried_interactive_ && method_ == "keyboard-interactive"sv) {
				if (++retry_counter_ < 3) {
					tried_pw_ = tried_key_ = tried_interactive_ = false;
					log(logmsg::debug_warning, L"Server rejected entered response. Starting over authentication from scratch."sv);
				}
			}
			else if (tried_key_ && !keys_.empty()) {
				keys_.pop_back();
				if (!keys_.empty()) {
					log(logmsg::debug_info, L"Starting over authentication with next available public key"sv);
					tried_pw_ = tried_key_ = tried_interactive_ = false;
				}
			}
		}
		else {
			log(logmsg::reply, _("Authentication required"));
		}
	}
	else {
		log(logmsg::reply, _("Further authentication required"));
	}

	next_auth();
}

void CSftpConnectOpData::next_auth()
{
	if (method_available(methods_, "publickey"sv) && !tried_key_) {
		if (auth_with_key()) {
			return;
		}
	}

	if (method_available(methods_, "password"sv) && !tried_pw_) {
		if (controlSocket_.credentials_.logonType_ == LogonType::interactive) {
			if (!method_available(methods_, "keyboard-interactive"sv)) {
				auto req = std::make_unique<PasswordRequest>(currentServer_, controlSocket_.GetHandle(), false);
				controlSocket_.SendAsyncRequest(std::move(req));
				return;
			}
		}
		else if (!controlSocket_.credentials_.GetPass().empty()) {
			tried_pw_ = true;
			log(logmsg::command, _("Sending password"));
			method_ = "password"sv;
			controlSocket_.ssh_->auth_with_password(fz::to_utf8(controlSocket_.credentials_.GetPass()));
			return;
		}
	}

	if (controlSocket_.credentials_.logonType_ != LogonType::anonymous && method_available(methods_, "keyboard-interactive"sv) && !tried_interactive_) {
		tried_interactive_ = true;
		log(logmsg::command, _("Requesting keyboard-interactive authentication"));
		method_ = "keyboard-interactive"sv;
		controlSocket_.ssh_->auth_keyboard_interactive();
		return;
	}

	log(logmsg::error, fztranslate("No more authentication methods available"));
	trigger_reset(FZ_REPLY_CRITICALERROR | FZ_REPLY_DISCONNECTED | FZ_REPLY_PASSWORDFAILED);
}

void CSftpConnectOpData::on_auth_done(fz::ssh::session*)
{
	controlSocket_.SetAlive();

	log(logmsg::reply, _("Authentication successful"));
	log(logmsg::command, _("Requesting SFTP subsystem"));

	auto si = controlSocket_.ssh_->open_channel(fz::ssh::channel_type::subsystem, "sftp"sv);

	fz::ssh::sftp::compatibility_flags flags{};
	if (currentServer_.GetExtraParameter("ignore_unknown_flags_in_attributes"sv) == L"1") {
		flags |= fz::ssh::sftp::compatibility_flags::ignore_unknown_flags_in_attributes;
	}

	sftp_ = std::make_unique<fz::ssh::sftp::sftp_client>(std::move(si), controlSocket_, controlSocket_.logger_, flags);
}

void CSftpConnectOpData::on_sftp_ready(fz::ssh::sftp::sftp_client*)
{
	log(logmsg::reply, _("SFTP subsystem initialized"));
	trigger_reset(FZ_REPLY_OK);
}

void CSftpConnectOpData::operator()(fz::event_base const& ev)
{
	if (fz::dispatch<fz::ssh::available_keys_event>(ev, this,
		&CSftpConnectOpData::on_agent_keys
	)) {
		return;
	}

	CSftpOpData::operator()(ev);
}

void CSftpConnectOpData::on_agent_keys(fz::ssh::agent_connection* conn, std::vector<std::unique_ptr<fz::ssh::private_key>> & keys)
{
	if (!agent_ || conn != agent_.get()) {
		return;
	}

	for (auto & k : keys) {
		if (!used_keys_.emplace(k->pubkey_blob()).second) {
			continue;
		}
		fz::ssh::private_key_info i;
		i.privkey_ = std::move(k);
		i.pubkey_ = i.privkey_->pubkey();
		keys_.emplace_back(std::move(i));
	}

	set_keys_loaded();
	next_auth();
}

void CSftpConnectOpData::set_keys_loaded()
{
	if (keys_loaded_) {
		return;
	}

	keys_loaded_ = true;
	used_keys_.clear();
	log(logmsg::debug_info, _("Loaded %u distinct keys"), keys_.size());

	std::reverse(keys_.begin(), keys_.end());
}

void CSftpConnectOpData::on_auth_pubkey_ok(fz::ssh::session*)
{
	if (keys_.empty()) {
		trigger_reset(FZ_REPLY_INTERNALERROR|FZ_REPLY_DISCONNECTED);
		return;
	}

	auto & key = keys_.back();
	if (!key.privkey_) {
		ask_key_password(false);
		return;
	}

	controlSocket_.ssh_->auth_with_key(key.privkey_, true);
}

void CSftpConnectOpData::ask_key_password(bool repeated)
{
	if (keys_.empty()) {
		trigger_reset(FZ_REPLY_INTERNALERROR|FZ_REPLY_DISCONNECTED);
		return;
	}

	auto req = std::make_unique<KeyfilePasswordRequest>(currentServer_, controlSocket_.GetHandle());

	req->repeated_ = repeated;

	auto const& info = keys_.back();
	if (info.pubkey_) {
		req->fingerprint_ = info.pubkey_->fingerprint();
		req->comment_ = info.pubkey_->comment_;
	}

	req->file_ = info.name_;

	controlSocket_.SendAsyncRequest(std::move(req));
}

void CSftpConnectOpData::set_password(std::string const& pw)
{
	tried_pw_ = true;
	log(logmsg::command, _("Sending password"));
	method_ = "password"sv;
	controlSocket_.ssh_->auth_with_password(pw);
}

void CSftpConnectOpData::set_keyfile_password(std::string const& pw)
{
	if (keys_.empty() || keys_.back().privkey_) {
		trigger_reset(FZ_REPLY_INTERNALERROR|FZ_REPLY_DISCONNECTED);
		return;
	}

	auto & info = keys_.back();

	bool const try_pubkey = info.pubkey_ == nullptr;

	if (!info.decrypt(pw, &controlSocket_.logger())) {
		ask_key_password(true);
		return;
	}

	if (try_pubkey) {
		controlSocket_.ssh_->auth_with_key(info.pubkey_);
	}
	else {
		controlSocket_.ssh_->auth_with_key(info.privkey_, true);
	}
}

void CSftpConnectOpData::on_auth_signature_failed(fz::ssh::session*)
{
	if (!tried_key_ || keys_.empty()) {
		trigger_reset(FZ_REPLY_INTERNALERROR|FZ_REPLY_DISCONNECTED);
		return;
	}

	keys_.pop_back();
	if (!keys_.empty()) {
		log(logmsg::debug_info, L"Starting over authentication with next available public key");
		tried_pw_ = tried_key_ = tried_interactive_ = false;
	}
	next_auth();
}

void CSftpConnectOpData::on_auth_keyboard_interactive_prompt(fz::ssh::session*, std::string const& name, std::string const& instruction, std::vector<fz::ssh::keyboard_interactive_prompt> & prompts)
{
	if (!tried_interactive_) {
		trigger_reset(FZ_REPLY_INTERNALERROR|FZ_REPLY_DISCONNECTED);
		return;
	}

	if (prompts.size() == 1 && prompts[0].prompt_ == "TOTP: "sv && controlSocket_.ssh_->peer_identification().find("_FileZillaProEnterpriseServer_"sv) != std::string::npos) {
		auto req = std::make_unique<OtpRequest>(currentServer_, controlSocket_.GetHandle());
		controlSocket_.SendAsyncRequest(std::move(req));
		return;
	}

	if (controlSocket_.credentials_.logonType_ != LogonType::interactive && prompts.size() == 1 && !controlSocket_.credentials_.GetPass().empty() && fz::equal_insensitive_ascii(fz::trimmed(prompts[0].prompt_, " \r\n\t:"sv), "password"sv)) {
		if (tried_pw_) {
			log(logmsg::status, _("The server sent a single keyboard-interactive prompt named \"Password\", but we already have sent the password. Select interactive login type to force an interactive prompt."));
			log(logmsg::error, fztranslate("No more authentication methods available"));
			trigger_reset(FZ_REPLY_CRITICALERROR | FZ_REPLY_DISCONNECTED | FZ_REPLY_PASSWORDFAILED);
		}
		else {
			tried_pw_ = true;
			log(logmsg::status, _("The server does not support password authentication, but sent a single keyboard-interactive prompt named \"Password\". Sending password as response. Select interactive login type to force an interactive prompt."));
			log(logmsg::command, _("Sending password as response"));

			std::vector<std::string> responses;
			responses.emplace_back(fz::to_utf8(controlSocket_.credentials_.GetPass()));
			controlSocket_.ssh_->auth_keyboard_interactive_response(std::move(responses));
		}
		return;
	}

	auto req = std::make_unique<CInteractiveLoginNotification>(currentServer_, controlSocket_.GetHandle());
	req->name_ = name;
	req->instruction_ = instruction;
	req->prompts_ = std::move(prompts);
	controlSocket_.SendAsyncRequest(std::move(req));
}
