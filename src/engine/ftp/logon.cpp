#include "../filezilla.h"

#include "logon.h"
#include "../proxy.h"
#include "../servercapabilities.h"
#include "../tls.h"

#include "../../include/misc.h"

#include "../../include/engine_options.h"

using namespace std::literals;
using namespace ftpLogonStates;

CFtpLogonOpData::CFtpLogonOpData(CFtpControlSocket& controlSocket)
	: COpData(Command::connect, L"CFtpLogonOpData")
	, CFtpOpData(controlSocket)
{
	for (int i = 0; i < LOGON_DONE; ++i) {
		neededCommands[i] = 1;
	}

	if (currentServer_.GetProtocol() != FTPES && currentServer_.GetProtocol() != FTP) {
		neededCommands[LOGON_AUTH_TLS] = 0;
		neededCommands[LOGON_AUTH_SSL] = 0;
		neededCommands[LOGON_AUTH_WAIT] = 0;
		if (currentServer_.GetProtocol() != FTPS) {
			neededCommands[LOGON_PBSZ] = 0;
			neededCommands[LOGON_PROT] = 0;
		}
	}
	if (currentServer_.GetPostLoginCommands().empty()) {
		neededCommands[LOGON_CUSTOMCOMMANDS] = 0;
	}

	const CharsetEncoding encoding = currentServer_.GetEncodingType();
	controlSocket_.m_useUTF8 = encoding == ENCODING_UTF8;
}

int CFtpLogonOpData::Send()
{
	switch (opState)
	{
	case LOGON_CONNECT:
		{
			if (!controlSocket_.buffer_pool_) {
				return FZ_REPLY_INTERNALERROR | FZ_REPLY_DISCONNECTED;
			}

			// Do not use FTP proxy if generic proxy is set
			int const generic_proxy_type = options_.get_int(OPTION_PROXY_TYPE);
			if ((generic_proxy_type <= static_cast<int>(ProxyType::NONE) || generic_proxy_type >= static_cast<int>(ProxyType::count)) && !currentServer_.GetBypassProxy()) {
				ftp_proxy_type_ = options_.get_int(OPTION_FTP_PROXY_TYPE);
			}

			if (!PrepareLoginSequence()) {
				return FZ_REPLY_INTERNALERROR;
			}

			if (ftp_proxy_type_) {
				host_ = options_.get_string(OPTION_FTP_PROXY_HOST);

				size_t pos = -1;
				if (!host_.empty() && host_[0] == '[') {
					// Probably IPv6 address
					pos = host_.find(']');
					if (pos == std::wstring::npos) {
						log(logmsg::error, _("Proxy host starts with '[' but no closing bracket found."));
						return FZ_REPLY_DISCONNECTED | FZ_REPLY_CRITICALERROR;
					}
					if (host_.size() > (pos + 1) && host_[pos + 1]) {
						if (host_[pos + 1] != ':') {
							log(logmsg::error, _("Invalid proxy host, after closing bracket only colon and port may follow."));
							return FZ_REPLY_DISCONNECTED | FZ_REPLY_CRITICALERROR;
						}
						++pos;
					}
					else {
						pos = std::wstring::npos;
					}
				}
				else {
					pos = host_.find(':');
				}

				if (pos != std::wstring::npos) {
					port_ = fz::to_integral<unsigned int>(host_.substr(pos + 1));
					host_ = host_.substr(0, pos);
				}
				else {
					port_ = 21;
				}

				if (host_.empty() || port_ < 1 || port_ > 65535) {
					log(logmsg::error, _("Proxy set but proxy host or port invalid"));
					return FZ_REPLY_DISCONNECTED | FZ_REPLY_CRITICALERROR;
				}

				log(logmsg::status, _("Connecting to %s through %s proxy"), currentServer_.Format(ServerFormat::with_optional_port, controlSocket_.credentials_), L"FTP"); // @translator: Connecting to ftp.example.com through SOCKS5 proxy
			}
			else {
				host_ = currentServer_.GetHost();
				port_ = currentServer_.GetPort();
			}

			opState = LOGON_WELCOME;
			int ret = controlSocket_.DoConnect(host_, port_);
			if (ret == FZ_REPLY_WOULDBLOCK) {
				// Enable TCP_NODELAY, speeds things up a bit.
				controlSocket_.socket_->set_flags(fz::socket::flag_nodelay | fz::socket::flag_keepalive, true);

				// Enable SO_KEEPALIVE, lots of clueless users have broken routers and
				// firewalls which terminate the control connection on long transfers.
				int v = options_.get_int(OPTION_TCP_KEEPALIVE_INTERVAL);
				if (v >= 1 && v < 10000) {
					controlSocket_.socket_->set_keepalive_interval(fz::duration::from_minutes(v));
				}
			}
			return ret;
		}
	case LOGON_AUTH_WAIT:
		log(logmsg::debug_info, L"LogonSend() called during LOGON_AUTH_WAIT, ignoring");
		return FZ_REPLY_WOULDBLOCK;
	case LOGON_AUTH_TLS:
		return controlSocket_.SendCommand(L"AUTH TLS", false, false);
	case LOGON_AUTH_SSL:
		return controlSocket_.SendCommand(L"AUTH SSL", false, false);
	case LOGON_SECURITY:
		if (currentServer_.GetProtocol() == INSECURE_FTP) {
			log(logmsg::status, _("Plain FTP is insecure. Please switch to FTP over TLS."));
		}
		opState = LOGON_LOGON;
		if (controlSocket_.tls_layer_) {
			return FZ_REPLY_CONTINUE;
		}
		else {
			controlSocket_.SendAsyncRequest(std::make_unique<CInsecureConnectionNotification>(currentServer_));
			return FZ_REPLY_WOULDBLOCK;
		}
	case LOGON_SYST:
		return controlSocket_.SendCommand(L"SYST");
	case LOGON_LOGON:
		{
			t_loginCommand cmd = loginSequence.front();
			switch (cmd.type)
			{
			case loginCommandType::user:
				if (controlSocket_.credentials_.logonType_ == LogonType::interactive) {
					challengeMode_ = always;
				}
				else if (controlSocket_.tls_layer_ && controlSocket_.tls_layer_->get_alpn() == "x-filezilla-ftp"sv) {
					challengeMode_ = mfa;
				}

				if (challengeMode_) {
					challenge_.clear();
				}

				if (cmd.command.empty()) {
					std::wstring const user = (controlSocket_.credentials_.logonType_ == LogonType::anonymous) ? L"anonymous" : currentServer_.GetUser();
					return controlSocket_.SendCommand(L"USER " + user);
				}
				else {
					return controlSocket_.SendCommand(cmd.command);
				}
			case loginCommandType::pass:
				if (otp_ && !controlSocket_.credentials_.HasExtraParameter("otp_code")) {
					controlSocket_.SendAsyncRequest(std::make_unique<OtpRequest>(currentServer_, controlSocket_.handle_));
					return FZ_REPLY_WOULDBLOCK;
				}
				else if (!challenge_.empty()) {
					auto notification = std::make_unique<CInteractiveLoginNotification>(currentServer_, controlSocket_.handle_);

					notification->instruction_ = fz::to_utf8(challenge_);
					notification->prompts_.emplace_back(fz::ssh::keyboard_interactive_prompt{fz::to_utf8(_("Password")), false});
					notification->server_ = currentServer_;
					notification->handle_ = controlSocket_.handle_;
					challenge_.clear();

					controlSocket_.SendAsyncRequest(std::move(notification));

					return FZ_REPLY_WOULDBLOCK;
				}
				else {
					std::wstring pass;
					if (controlSocket_.credentials_.logonType_ == LogonType::anonymous) {
						pass = L"anonymous@example.com";
					}
					else {
						pass = controlSocket_.credentials_.GetPass();
						if (controlSocket_.credentials_.HasExtraParameter("otp_code")) {
							pass = controlSocket_.credentials_.GetExtraParameter("otp_code") + L";" + pass;
						}
					}
					if (cmd.command.empty()) {
						return controlSocket_.SendCommand(L"PASS " + pass, true);
					}
					else {
						std::wstring c = cmd.command;
						fz::replace_substrings(pass, L"%", L"%%");
						fz::replace_substrings(c, L"%p", pass);
						fz::replace_substrings(c, L"%%", L"%");
						return controlSocket_.SendCommand(c, true);
					}
				}
				break;
			case loginCommandType::account:
				if (cmd.command.empty()) {
					return controlSocket_.SendCommand(L"ACCT " + controlSocket_.credentials_.account_);
				}
				else {
					return controlSocket_.SendCommand(cmd.command);
				}
				break;
			case loginCommandType::other:
				if (cmd.command.empty()) {
					return FZ_REPLY_INTERNALERROR;
				}
				return controlSocket_.SendCommand(cmd.command, cmd.hide_arguments);
			default:
				return FZ_REPLY_INTERNALERROR;
			}
		}
		break;
	case LOGON_FEAT:
		return controlSocket_.SendCommand(L"FEAT");
	case LOGON_CLNT:
		// Some servers refuse to enable UTF8 if client does not send CLNT command
		// to fix compatibility with Internet Explorer, but in the process breaking
		// compatibility with other clients.
		// Rather than forcing MS to fix Internet Explorer, letting other clients
		// suffer is a questionable decision in my opinion.
		return controlSocket_.SendCommand(L"CLNT FileZilla");
	case LOGON_OPTSUTF8:
		// Handle servers that disobey RFC 2640 by having UTF8 in their FEAT
		// response but do not use UTF8 unless OPTS UTF8 ON gets send.
		// However these servers obey a conflicting ietf draft:
		// http://www.ietf.org/proceedings/02nov/I-D/draft-ietf-ftpext-utf-8-option-00.txt
		// Example servers are, amongst others, G6 FTP Server and RaidenFTPd.
		return controlSocket_.SendCommand(L"OPTS UTF8 ON");
	case LOGON_PBSZ:
		return controlSocket_.SendCommand(L"PBSZ 0");
	case LOGON_PROT:
		return controlSocket_.SendCommand(L"PROT P");
	case LOGON_CUSTOMCOMMANDS:
		if (customCommandIndex >= currentServer_.GetPostLoginCommands().size()) {
			log(logmsg::debug_warning, L"pData->customCommandIndex >= m_pCurrentServer->GetPostLoginCommands().size()");
			return FZ_REPLY_INTERNALERROR | FZ_REPLY_DISCONNECTED;
		}
		return controlSocket_.SendCommand(currentServer_.GetPostLoginCommands()[customCommandIndex]);
	case LOGON_OPTSMLST:
		{
			std::wstring args;
			CServerCapabilities::GetCapability(currentServer_, opst_mlst_command, &args);
			return controlSocket_.SendCommand(L"OPTS MLST " + args);
		}
		break;
	default:
		log(logmsg::debug_warning, L"unknown op state: %d", opState);
		break;
	}

	return FZ_REPLY_INTERNALERROR;
}

int CFtpLogonOpData::ParseResponse()
{
	int code = controlSocket_.GetReplyCode();
	std::wstring const& response = controlSocket_.m_Response;

	if (opState == LOGON_WELCOME) {
		if (code != 2 && code != 3) {
			return FZ_REPLY_DISCONNECTED | (code == 5 ? FZ_REPLY_CRITICALERROR : FZ_REPLY_ERROR);
		}
	}
	else if (opState == LOGON_AUTH_TLS ||
	         opState == LOGON_AUTH_SSL)
	{
		if (code != 2 && code != 3) {
			CServerCapabilities::SetCapability(currentServer_, (opState == LOGON_AUTH_TLS) ? auth_tls_command : auth_ssl_command, no);
			if (opState == LOGON_AUTH_SSL) {
				if (currentServer_.GetProtocol() == FTP) {
					log(logmsg::status, _("Insecure server, it does not support FTP over TLS."));
					neededCommands[LOGON_PBSZ] = 0;
					neededCommands[LOGON_PROT] = 0;
					opState = LOGON_SECURITY;
					return FZ_REPLY_CONTINUE;
				}
				else {
					return FZ_REPLY_DISCONNECTED | (code == 5 ? FZ_REPLY_CRITICALERROR : FZ_REPLY_ERROR);
				}
			}
		}
		else {
			CServerCapabilities::SetCapability(currentServer_, (opState == LOGON_AUTH_TLS) ? auth_tls_command : auth_ssl_command, yes);

			log(logmsg::status, _("Initializing TLS..."));

			controlSocket_.tls_layer_ = std::make_unique<fz::tls_layer>(controlSocket_.event_loop_, &controlSocket_, *controlSocket_.active_layer_, &engine_.GetContext().GetTlsSystemTrustStore(), controlSocket_.logger_);
			controlSocket_.active_layer_ = controlSocket_.tls_layer_.get();

			controlSocket_.tls_layer_->set_alpn({"ftp", "x-filezilla-ftp"});
			controlSocket_.tls_layer_->set_min_tls_ver(get_min_tls_ver(options_));
			if (!controlSocket_.tls_layer_->client_handshake(&controlSocket_)) {
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}

			neededCommands[LOGON_AUTH_SSL] = 0;
			opState = LOGON_AUTH_WAIT;

			return FZ_REPLY_WOULDBLOCK;
		}
	}
	else if (opState == LOGON_LOGON) {
		t_loginCommand cmd = loginSequence.front();

		if (cmd.type == loginCommandType::user) {
			if (code == 3 && response.substr(0, 3) == L"336"sv) {
				if (controlSocket_.tls_layer_ && controlSocket_.tls_layer_->get_alpn() == "x-filezilla-ftp"sv) {
					size_t beg = challenge_.rfind('[');
					size_t end = challenge_.rfind(']');
					if (beg != std::string::npos && end != std::string::npos && beg < end) {
						auto tokens = fz::strtok_view(std::wstring_view(challenge_).substr(beg + 1, end - beg - 1), ',');
						if (tokens.size() == 1 && tokens[0] == L"alg:totp"sv) {
							otp_ = true;
							challenge_.clear();
						}
					}
				}
				else {
					if (challengeMode_ != always) {
						log(logmsg::status, _("Consider using the interactive login type."));
						challenge_.clear();
					}
				}
			}
			else if (challengeMode_ != always) {
				challenge_.clear();
			}
		}

		if (code != 2 && code != 3) {
			if (cmd.type == loginCommandType::user || cmd.type == loginCommandType::pass) {
				auto const user = currentServer_.GetUser();
				if (!user.empty() && (user.front() == ' ' || user.back() == ' ')) {
					log(logmsg::status, _("Check your login credentials. The entered username starts or ends with a space character."));
				}
				auto const pw = controlSocket_.credentials_.GetPass();
				if (!pw.empty() && (pw.front() == ' ' || pw.back() == ' ')) {
					log(logmsg::status, _("Check your login credentials. The entered password starts or ends with a space character."));
				}
			}

			int error = FZ_REPLY_DISCONNECTED | FZ_REPLY_ERROR;
			if (cmd.type == loginCommandType::pass && code == 5) {
				error |= FZ_REPLY_CRITICALERROR | FZ_REPLY_PASSWORDFAILED;
			}
			return error;
		}

		loginSequence.pop_front();
		if (code == 2) {
			while (!loginSequence.empty() && loginSequence.front().optional) {
				loginSequence.pop_front();
			}
		}
		else if (code == 3 && loginSequence.empty()) {
			log(logmsg::error, _("Login sequence fully executed yet not logged in, aborting."));
			if (cmd.type == loginCommandType::pass && controlSocket_.credentials_.account_.empty()) {
				log(logmsg::error, _("Server might require an account. Try specifying an account using the Site Manager"));
			}
			return FZ_REPLY_CRITICALERROR | FZ_REPLY_DISCONNECTED;
		}

		if (!loginSequence.empty()) {
			challengeMode_ = none;

			return FZ_REPLY_CONTINUE;
		}
	}
	else if (opState == LOGON_SYST) {
		if (code == 2) {
			CServerCapabilities::SetCapability(currentServer_, syst_command, yes, response.substr(4));
		}
		else {
			CServerCapabilities::SetCapability(currentServer_, syst_command, no);
		}

		if (response.find(L"FileZilla") != std::wstring::npos) {
			neededCommands[LOGON_CLNT] = 0;
			neededCommands[LOGON_OPTSUTF8] = 0;
		}
	}
	else if (opState == LOGON_FEAT) {
		if (code == 2) {
			CServerCapabilities::SetCapability(currentServer_, feat_command, yes);
			if (CServerCapabilities::GetCapability(currentServer_, utf8_command) != yes) {
				CServerCapabilities::SetCapability(currentServer_, utf8_command, no);
			}
			if (CServerCapabilities::GetCapability(currentServer_, clnt_command) != yes) {
				CServerCapabilities::SetCapability(currentServer_, clnt_command, no);
			}
		}
		else {
			CServerCapabilities::SetCapability(currentServer_, feat_command, no);
		}

		if (CServerCapabilities::GetCapability(currentServer_, tvfs_support) != yes) {
			CServerCapabilities::SetCapability(currentServer_, tvfs_support, no);
		}

		const CharsetEncoding encoding = currentServer_.GetEncodingType();
		if (encoding == ENCODING_UTF8 && CServerCapabilities::GetCapability(currentServer_, utf8_command) != yes) {
			log(logmsg::status, _("Server does not support non-ASCII characters."));
		}

		if (CServerCapabilities::GetCapability(currentServer_, tvfs_support) == yes && currentServer_.GetType() != ServerType::UNIX) {
			log(logmsg::error, _("The configured server type %s cannot be used on a server that identifies itself of type %s (via TVFS support)"), CServer::GetNameFromServerType(currentServer_.GetType()), CServer::GetNameFromServerType(UNIX));
			return FZ_REPLY_CRITICALERROR;
		}
	}
	else if (opState == LOGON_PROT) {
		if (code == 2 || code == 3) {
			controlSocket_.m_protectDataChannel = true;
		}
	}
	else if (opState == LOGON_CUSTOMCOMMANDS) {
		++customCommandIndex;
		if (customCommandIndex < currentServer_.GetPostLoginCommands().size()) {
			return FZ_REPLY_CONTINUE;
		}
	}

	for (;;) {
		++opState;

		if (opState == LOGON_DONE) {
			log(logmsg::status, _("Logged in"));
			log(logmsg::debug_info, L"Measured latency of %d ms", controlSocket_.m_rtt.GetLatency());
			return FZ_REPLY_OK;
		}

		if (!neededCommands[opState]) {
			continue;
		}
		else if (opState == LOGON_SYST) {
			std::wstring system;
			capabilities cap = CServerCapabilities::GetCapability(currentServer_, syst_command, &system);
			if (cap == unknown) {
				break;
			}
			else if (cap == yes) {
				if (system.find(L"FileZilla") != std::wstring::npos) {
					neededCommands[LOGON_CLNT] = 0;
					neededCommands[LOGON_OPTSUTF8] = 0;
				}
			}
		}
		else if (opState == LOGON_FEAT) {
			capabilities cap = CServerCapabilities::GetCapability(currentServer_, feat_command);
			if (cap == unknown) {
				break;
			}
			const CharsetEncoding encoding = currentServer_.GetEncodingType();
			if (encoding == ENCODING_UTF8 && CServerCapabilities::GetCapability(currentServer_, utf8_command) != yes) {
				log(logmsg::status, _("Server does not support non-ASCII characters."));
			}
		}
		else if (opState == LOGON_CLNT) {
			if (!controlSocket_.m_useUTF8) {
				continue;
			}

			if (CServerCapabilities::GetCapability(currentServer_, clnt_command) == yes) {
				break;
			}
		}
		else if (opState == LOGON_OPTSUTF8) {
			if (!controlSocket_.m_useUTF8) {
				continue;
			}

			if (CServerCapabilities::GetCapability(currentServer_, utf8_command) == yes) {
				break;
			}
		}
		else if (opState == LOGON_OPTSMLST) {
			std::wstring facts;
			if (CServerCapabilities::GetCapability(currentServer_, mlsd_command, &facts) != yes) {
				continue;
			}
			capabilities cap = CServerCapabilities::GetCapability(currentServer_, opst_mlst_command);
			if (cap == unknown) {
				facts = fz::str_tolower_ascii(facts);

				bool had_unset = false;
				std::wstring opts_facts;

				// Create a list of all facts understood by both FZ and the server.
				// Check if there's any supported fact not enabled by default, should that
				// be the case we need to send OPTS MLST
				while (!facts.empty()) {
					size_t delim = facts.find(';');
					if (delim == std::wstring::npos) {
						break;
					}

					if (!delim) {
						facts = facts.substr(1);
						continue;
					}

					bool enabled;
					std::wstring fact;

					if (facts[delim - 1] == '*') {
						if (delim == 1) {
							facts = facts.substr(delim + 1);
							continue;
						}
						enabled = true;
						fact = facts.substr(0, delim - 1);
					}
					else {
						enabled = false;
						fact = facts.substr(0, delim);
					}
					facts = facts.substr(delim + 1);

					if (fact == L"type" ||
					    fact == L"size" ||
					    fact == L"modify" ||
					    fact == L"perm" ||
					    fact == L"unix.mode" ||
					    fact == L"unix.owner" ||
					    fact == L"unix.ownername" ||
					    fact == L"unix.group" ||
					    fact == L"unix.groupname" ||
					    fact == L"unix.user" ||
					    fact == L"unix.uid" ||
					    fact == L"unix.gid" ||
					    fact == L"x.hidden")
					{
						had_unset |= !enabled;
						opts_facts += fact + L";";
					}
				}

				if (had_unset) {
					CServerCapabilities::SetCapability(currentServer_, opst_mlst_command, yes, opts_facts);
					break;
				}
				else {
					CServerCapabilities::SetCapability(currentServer_, opst_mlst_command, no);
				}
			}
			else if (cap == yes) {
				break;
			}
		}
		else {
			break;
		}
	}

	return FZ_REPLY_CONTINUE;
}

bool CFtpLogonOpData::PrepareLoginSequence()
{
	loginSequence.clear();

	if (!ftp_proxy_type_) {
		// User
		t_loginCommand cmd = {false, false, loginCommandType::user, L""};
		loginSequence.push_back(cmd);

		// Password
		cmd.optional = true;
		cmd.hide_arguments = true;
		cmd.type = loginCommandType::pass;
		loginSequence.push_back(cmd);

		// Optional account
		if (!controlSocket_.credentials_.account_.empty()) {
			cmd.hide_arguments = false;
			cmd.type = loginCommandType::account;
			loginSequence.push_back(cmd);
		}
	}
	else if (ftp_proxy_type_ == 1) {
		std::wstring const proxyUser = options_.get_string(OPTION_FTP_PROXY_USER);
		if (!proxyUser.empty()) {
			// Proxy logon (if credendials are set)
			t_loginCommand cmd = {false, false, loginCommandType::other, L"USER " + proxyUser};
			loginSequence.push_back(cmd);
			cmd.optional = true;
			cmd.hide_arguments = true;
			cmd.command = L"PASS " + options_.get_string(OPTION_FTP_PROXY_PASS);
			loginSequence.push_back(cmd);
		}
		// User@host
		std::wstring const user = (controlSocket_.credentials_.logonType_ == LogonType::anonymous) ? L"anonymous" : currentServer_.GetUser();
		t_loginCommand cmd = {false, false, loginCommandType::user, fz::sprintf(L"USER %s@%s", user, currentServer_.Format(ServerFormat::with_optional_port, controlSocket_.credentials_))};
		loginSequence.push_back(cmd);

		// Password
		cmd.optional = true;
		cmd.hide_arguments = true;
		cmd.type = loginCommandType::pass;
		cmd.command.clear();
		loginSequence.push_back(cmd);

		// Optional account
		if (!controlSocket_.credentials_.account_.empty()) {
			cmd.hide_arguments = false;
			cmd.type = loginCommandType::account;
			loginSequence.push_back(cmd);
		}
	}
	else if (ftp_proxy_type_ == 2 || ftp_proxy_type_ == 3) {
		std::wstring const proxyUser = options_.get_string(OPTION_FTP_PROXY_USER);
		if (!proxyUser.empty()) {
			// Proxy logon (if credendials are set)
			t_loginCommand cmd = {false, false, loginCommandType::other, L"USER " + proxyUser};
			loginSequence.push_back(cmd);
			cmd.optional = true;
			cmd.hide_arguments = true;
			cmd.command = L"PASS " + options_.get_string(OPTION_FTP_PROXY_PASS);
			loginSequence.push_back(cmd);
		}

		// Site or Open
		t_loginCommand cmd = {false, false, loginCommandType::user, L""};
		if (ftp_proxy_type_ == 2) {
			cmd.command = L"SITE " + currentServer_.Format(ServerFormat::with_optional_port, controlSocket_.credentials_);
		}
		else {
			cmd.command = L"OPEN " + currentServer_.Format(ServerFormat::with_optional_port, controlSocket_.credentials_);
		}
		loginSequence.push_back(cmd);

		// User
		cmd.type = loginCommandType::user;
		cmd.command.clear();
		loginSequence.push_back(cmd);

		// Password
		cmd.optional = true;
		cmd.hide_arguments = true;
		cmd.type = loginCommandType::pass;
		loginSequence.push_back(cmd);

		// Optional account
		if (!controlSocket_.credentials_.account_.empty()) {
			cmd.hide_arguments = false;
			cmd.type = loginCommandType::account;
			loginSequence.push_back(cmd);
		}
	}
	else if (ftp_proxy_type_ == 4) {
		std::wstring proxyUser = options_.get_string(OPTION_FTP_PROXY_USER);
		std::wstring proxyPass = options_.get_string(OPTION_FTP_PROXY_PASS);
		std::wstring host = currentServer_.Format(ServerFormat::with_optional_port, controlSocket_.credentials_);
		std::wstring user = (controlSocket_.credentials_.logonType_ == LogonType::anonymous) ? L"anonymous" : currentServer_.GetUser();
		std::wstring account = controlSocket_.credentials_.account_;
		fz::replace_substrings(proxyUser, L"%", L"%%");
		fz::replace_substrings(proxyPass, L"%", L"%%");
		fz::replace_substrings(host, L"%", L"%%");
		fz::replace_substrings(user, L"%", L"%%");
		fz::replace_substrings(account, L"%", L"%%");

		std::wstring const loginSequenceStr = options_.get_string(OPTION_FTP_PROXY_CUSTOMLOGINSEQUENCE);
		std::vector<std::wstring> const tokens = fz::strtok(loginSequenceStr, L"\r\n");

		for (auto token : tokens) {
			bool isHost = false;
			bool isUser = false;
			bool password = false;
			bool isProxyUser = false;
			bool isProxyPass = false;
			if (token.find(L"%h") != token.npos) {
				isHost = true;
			}
			if (token.find(L"%u") != token.npos) {
				isUser = true;
			}
			if (token.find(L"%p") != token.npos) {
				password = true;
			}
			if (token.find(L"%s") != token.npos) {
				isProxyUser = true;
			}
			if (token.find(L"%w") != token.npos) {
				isProxyPass = true;
			}

			// Skip account if empty
			bool isAccount = false;
			if (token.find(L"%a") != token.npos) {
				if (account.empty()) {
					continue;
				}
				else {
					isAccount = true;
				}
			}

			if (isProxyUser && !isHost && !isUser && proxyUser.empty()) {
				continue;
			}
			if (isProxyPass && !isHost && !isUser && proxyUser.empty()) {
				continue;
			}

			fz::replace_substrings(token, L"%s", proxyUser);
			fz::replace_substrings(token, L"%w", proxyPass);
			fz::replace_substrings(token, L"%h", host);
			fz::replace_substrings(token, L"%u", user);
			fz::replace_substrings(token, L"%a", account);
			// Pass will be replaced before sending to cope with interactve login

			if (!password) {
				fz::replace_substrings(token, L"%%", L"%");
			}

			t_loginCommand cmd;
			if (password || isProxyPass) {
				cmd.hide_arguments = true;
			}
			else {
				cmd.hide_arguments = false;
			}

			if (isUser && !password && !isAccount) {
				cmd.optional = false;
				cmd.type = loginCommandType::user;
			}
			else if (password && !isUser && !isAccount) {
				cmd.optional = true;
				cmd.type = loginCommandType::pass;
			}
			else if (isAccount && !isUser && !password) {
				cmd.optional = true;
				cmd.type = loginCommandType::account;
			}
			else {
				cmd.optional = false;
				cmd.type = loginCommandType::other;
			}

			cmd.command = token;

			loginSequence.push_back(cmd);
		}

		if (loginSequence.empty()) {
			log(logmsg::error, _("Could not generate custom login sequence."));
			return false;
		}
	}
	else {
		log(logmsg::error, _("Unknown FTP proxy type, cannot generate login sequence."));
		return false;
	}

	return true;
}

namespace {
bool HasFeature(std::wstring const& line, std::wstring const& feature)
{
	if (line == feature) {
		return true;
	}
	return line.size() > feature.size() && line.substr(0, feature.size()) == feature && line[feature.size()] == ' ';
}
}

void CFtpLogonOpData::ParseFeat(std::wstring line)
{
	fz::trim(line);
	std::wstring up = fz::str_toupper_ascii(line);

	if (HasFeature(up, L"UTF8")) {
		CServerCapabilities::SetCapability(currentServer_, utf8_command, yes);
	}
	else if (HasFeature(up, L"CLNT")) {
		CServerCapabilities::SetCapability(currentServer_, clnt_command, yes);
	}
	else if (HasFeature(up, L"MLSD")) {
		std::wstring facts;
		// FEAT output for MLST overrides MLSD
		if (CServerCapabilities::GetCapability(currentServer_, mlsd_command, &facts) != yes || facts.empty()) {
			if (line.size() > 5) {
				facts = line.substr(5);
			}
			else {
				facts.clear();
			}
		}
		CServerCapabilities::SetCapability(currentServer_, mlsd_command, yes, facts);

		// MLST/MLSD specs require use of UTC
		CServerCapabilities::SetCapability(currentServer_, inferred_timezone_offset, no);
	}
	else if (HasFeature(up, L"MLST")) {
		std::wstring facts;
		if (line.size() > 5) {
			facts = line.substr(5);
		}
		// FEAT output for MLST overrides MLSD
		if (facts.empty()) {
			if (CServerCapabilities::GetCapability(currentServer_, mlsd_command, &facts) != yes) {
				facts.clear();
			}
		}
		CServerCapabilities::SetCapability(currentServer_, mlsd_command, yes, facts);

		// MLST/MLSD specs require use of UTC
		CServerCapabilities::SetCapability(currentServer_, inferred_timezone_offset, no);
	}
	else if (HasFeature(up, L"MODE Z")) {
		CServerCapabilities::SetCapability(currentServer_, mode_z_support, yes);
	}
	else if (HasFeature(up, L"MFMT")) {
		CServerCapabilities::SetCapability(currentServer_, mfmt_command, yes);
	}
	else if (HasFeature(up, L"MDTM")) {
		CServerCapabilities::SetCapability(currentServer_, mdtm_command, yes);
	}
	else if (HasFeature(up, L"SIZE")) {
		CServerCapabilities::SetCapability(currentServer_, size_command, yes);
	}
	else if (HasFeature(up, L"TVFS")) {
		CServerCapabilities::SetCapability(currentServer_, tvfs_support, yes);
	}
	else if (HasFeature(up, L"REST STREAM")) {
		CServerCapabilities::SetCapability(currentServer_, rest_stream, yes);
	}
	else if (HasFeature(up, L"EPSV")) {
		CServerCapabilities::SetCapability(currentServer_, epsv_command, yes);
	}
}

void CFtpLogonOpData::tls_handshake_finished()
{
	if (opState == LOGON_AUTH_WAIT) {
		if (controlSocket_.tls_layer_ && controlSocket_.tls_layer_->get_alpn() == "x-filezilla-ftp") {
			neededCommands[LOGON_SYST] = 0;
			neededCommands[LOGON_PBSZ] = 0;
			neededCommands[LOGON_PROT] = 0;
			neededCommands[LOGON_CLNT] = 0;
			neededCommands[LOGON_OPTSUTF8] = 0;
			controlSocket_.m_protectDataChannel = true;
		}
		opState = LOGON_LOGON;
	}
}
