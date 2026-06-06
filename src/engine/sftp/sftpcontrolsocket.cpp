#include "../filezilla.h"

#include "chmod.h"
#include "connect.h"
#include "cwd.h"
#include "delete.h"
#include "filetransfer.h"
#include "list.h"
#include "mkd.h"
#include "rename.h"
#include "rmd.h"
#include "sftpcontrolsocket.h"

#include "../../include/engine_options.h"

#include <fzssh/client.hpp>

#include <libfilezilla/hash.hpp>

CSftpControlSocket::CSftpControlSocket(CFileZillaEnginePrivate & engine)
	: CRealControlSocket(engine)
{
}

CSftpControlSocket::~CSftpControlSocket()
{
	remove_handler();
	DoClose();
}

void CSftpControlSocket::Connect(CServer const& server, Credentials const& credentials)
{
	m_useUTF8 = server.GetEncodingType() == ENCODING_UTF8;
	if (!m_useUTF8) {
		log(logmsg::debug_info, L"Using custom encoding: %s", server.GetCustomEncoding());
	}

	currentServer_ = server;
	credentials_ = credentials;

	Push(std::make_unique<CSftpConnectOpData>(*this));
}

void CSftpControlSocket::FileTransfer(CFileTransferCommand const& cmd)
{
	Push(std::make_unique<CSftpFileTransferOpData>(*this, cmd));
}

void CSftpControlSocket::Delete(CServerPath const& path, std::vector<std::wstring>&& files)
{
	log(logmsg::debug_verbose, L"CSftpControlSocket::Delete");

	auto pData = std::make_unique<CSftpDeleteOpData>(*this);
	pData->path_ = path;
	pData->files_ = std::move(files);
	Push(std::move(pData));
}

void CSftpControlSocket::RemoveDir(CServerPath const& path, std::wstring const& subDir)
{
	log(logmsg::debug_verbose, L"CSftpControlSocket::RemoveDir");

	auto pData = std::make_unique<CSftpRemoveDirOpData>(*this);
	pData->path_ = path;
	pData->subDir_ = subDir;
	Push(std::move(pData));
}

void CSftpControlSocket::Chmod(CChmodCommand const& command)
{
	Push(std::make_unique<CSftpChmodOpData>(*this, command));
}

void CSftpControlSocket::Rename(CRenameCommand const& command)
{
	Push(std::make_unique<CSftpRenameOpData>(*this, command));
}

int CSftpControlSocket::DoClose(int nErrorCode)
{
	sftp_.reset();
	ssh_.reset();
	pending_global_async_request_.reset();
	return CRealControlSocket::DoClose(nErrorCode);
}

bool CSftpControlSocket::SetAsyncRequestReply(CAsyncRequestNotification *pNotification)
{
	log(logmsg::debug_verbose, L"CSftpControlSocket::SetAsyncRequestReply");

	if (!ssh_) {
		DoClose(FZ_REPLY_CRITICALERROR);
		return false;
	}

	RequestId const requestId = pNotification->GetRequestID();
	switch(requestId)
	{
	case reqId_fileexists:
		{
			auto *pFileExistsNotification = static_cast<CFileExistsNotification *>(pNotification);
			return SetFileExistsAction(pFileExistsNotification);
		}
	case reqId_hostkey:
		{
			auto & req = static_cast<CHostKeyNotification&>(*pNotification);
			if (req.trust_) {
				log_raw(logmsg::status, _("Hostkey is trusted"));
			}

			ssh_->hostkey_decision(req.trust_);
		}
		break;
	case reqId_interactiveLogin:
		{
			if (operations_.empty() || operations_.back()->opId != Command::connect) {
				log(logmsg::debug_info, L"No or invalid operation in progress, ignoring request reply %d", pNotification->GetRequestID());
				return false;
			}

			auto & req = static_cast<CInteractiveLoginNotification&>(*pNotification);
			if (!req.responses_) {
				DoClose(FZ_REPLY_CANCELED);
				return false;
			}

			ssh_->auth_keyboard_interactive_response(*req.responses_);
		}
		break;
	case reqId_password:
		{
			if (operations_.empty() || operations_.back()->opId != Command::connect) {
				log(logmsg::debug_info, L"No or invalid operation in progress, ignoring request reply %d", pNotification->GetRequestID());
				return false;
			}

			auto & req = static_cast<PasswordRequest&>(*pNotification);
			if (!req.password_) {
				DoClose(FZ_REPLY_CANCELED);
				return false;
			}

			CSftpConnectOpData& op = static_cast<CSftpConnectOpData&>(*operations_.back().get());
			op.set_password(fz::to_utf8(*req.password_));
		}
		break;
	case reqId_otp:
		{
			if (operations_.empty() || operations_.back()->opId != Command::connect) {
				log(logmsg::debug_info, L"No or invalid operation in progress, ignoring request reply %d", pNotification->GetRequestID());
				return false;
			}

			auto & req = static_cast<OtpRequest&>(*pNotification);
			if (req.otp_.empty()) {
				DoClose(FZ_REPLY_CANCELED);
				return false;
			}

			std::vector<std::string> responses;
			responses.emplace_back(req.otp_);
			ssh_->auth_keyboard_interactive_response(responses);
		}
		break;
	case reqId_keyfile_password:
		{
			if (operations_.empty() || operations_.back()->opId != Command::connect) {
				log(logmsg::debug_info, L"No or invalid operation in progress, ignoring request reply %d", pNotification->GetRequestID());
				return false;
			}

			auto & req = static_cast<KeyfilePasswordRequest&>(*pNotification);
			if (!req.password_) {
				DoClose(FZ_REPLY_CANCELED);
				return false;
			}

			CSftpConnectOpData& op = static_cast<CSftpConnectOpData&>(*operations_.back().get());
			op.set_keyfile_password(*req.password_);
		}
		break;
	default:
		log(logmsg::debug_warning, L"Unknown async request reply id: %d", requestId);
		return false;
	}

	return true;
}

void CSftpControlSocket::OnConnect()
{
	SetAlive();
	log(logmsg::debug_info, L"TCP connection established"sv);
	SendNextCommand();
}

std::wstring CSftpControlSocket::QuoteFilename(std::wstring const& filename)
{
	return L"\"" + fz::replaced_substrings(filename, L"\"", L"\"\"") + L"\"";
}

void CSftpControlSocket::operator()(fz::event_base const& ev)
{
	if (!operations_.empty() && operations_.back()->opId == Command::connect) {
		CSftpConnectOpData* op = static_cast<CSftpConnectOpData*>(operations_.back().get());
		fz::dispatch<
				fz::ssh::auth_requested_event,
				fz::ssh::auth_done_event,
				fz::ssh::auth_public_key_okay_event,
				fz::ssh::auth_signature_failure_event,
				fz::ssh::auth_keyboard_interactive_prompt_event,
				fz::ssh::sftp::sftp_client::ready_event
			>(
				ev, op,
				&CSftpConnectOpData::on_auth_requested,
				&CSftpConnectOpData::on_auth_done,
				&CSftpConnectOpData::on_auth_pubkey_ok,
				&CSftpConnectOpData::on_auth_signature_failed,
				&CSftpConnectOpData::on_auth_keyboard_interactive_prompt,
				&CSftpConnectOpData::on_sftp_ready
			);
	}

	fz::dispatch<fz::ssh::hostkey_verification_event, fz::ssh::session_done_event, fz::ssh::sftp::sftp_client::done_event>(
		ev, this,
		&CSftpControlSocket::on_hostkey_verification,
		&CSftpControlSocket::on_session_done,
		&CSftpControlSocket::on_sftp_done
	);


	CRealControlSocket::operator()(ev);
}

void CSftpControlSocket::Push(std::unique_ptr<COpData> && pNewOpData)
{
	CControlSocket::Push(std::move(pNewOpData));
	if (operations_.size() == 1 && operations_.back()->opId != Command::connect) {
		if (!ssh_) {
			std::unique_ptr<COpData> connOp = std::make_unique<CSftpConnectOpData>(*this);
			connOp->topLevelOperation_ = true;
			CControlSocket::Push(std::move(connOp));
		}
	}
}

void CSftpControlSocket::on_hostkey_verification(fz::ssh::session*, std::unique_ptr<fz::ssh::public_key> & key, fz::ssh::algorithm_info & algs)
{
	engine_.AddNotification(std::make_unique<CSftpEncryptionNotification>(algs, key->fingerprint(fz::hash_algorithm::sha256, true)));
	SendAsyncRequest(std::make_unique<CHostKeyNotification>(currentServer_, handle_, std::move(key), std::move(algs)));
}

void CSftpControlSocket::on_session_done(fz::ssh::session*)
{
	DoClose();
}

void CSftpControlSocket::on_sftp_done(fz::ssh::sftp::sftp_client*)
{
	DoClose();
}

void CSftpControlSocket::List(CServerPath const& path, std::wstring const& subDir, int flags)
{
	Push(std::make_unique<CSftpListOpData>(*this, path, subDir, flags));
}

void CSftpControlSocket::ChangeDir(CServerPath const& path, std::wstring const& subDir, bool link_discovery)
{
	auto pData = std::make_unique<CSftpChangeDirOpData>(*this);
	pData->path_ = path;
	pData->subDir_ = subDir;
	pData->link_discovery_ = link_discovery;

	if (!operations_.empty() && operations_.back()->opId == Command::transfer &&
		!static_cast<CSftpFileTransferOpData&>(*operations_.back()).download())
	{
		if (!subDir.empty()) {
			Push(std::make_unique<ResultOpData>(FZ_REPLY_INTERNALERROR));
			return;
		}
		pData->tryMkdOnFail_ = true;
	}

	Push(std::move(pData));
}

void CSftpControlSocket::Mkdir(CServerPath const& path, transfer_flags const&)
{
	Push(std::make_unique<CSftpMkdirOpData>(*this, path));
}

void CSftpControlSocket::SetSocketBufferSizes()
{
	if (socket_) {
		const int size_read = engine_.GetOptions().get_int(OPTION_SOCKET_BUFFERSIZE_RECV);
		socket_->set_buffer_sizes(size_read, -1);
		return;
	}
}

CSftpOpData::~CSftpOpData()
{
	remove_handler();
	if (sftp_) {
		sftp_->cancel(this);
	}
}

namespace {
std::string sanitize_status_message(std::string_view msg)
{
	std::string ret;
	for (auto s : fz::strtokenizer(msg, "\r\n\t ", true)) {
		if (!ret.empty()) {
			ret += ' ';
		}
		ret += s;
	}

	return ret;
}
}

CSftpOpData::continuation CSftpOpData::process_status(fz::ssh::sftp::status_code code, std::string_view description)
{
	if (code == fz::ssh::sftp::status_code::SSH_FX_OK) {
		return do_process_status(code, {});
	}

	std::wstring msg;

	auto desc = sanitize_status_message(description);
	if (desc.empty()) {
		msg = fz::sprintf(fztranslate("Received error %s without any further description"), fz::ssh::sftp::to_string(code));
	}
	else {
		msg = fz::sprintf(fztranslate("Received error %s with description '%s'"), fz::ssh::sftp::to_string(code), desc);
	}

	return do_process_status(code, msg);
}

void CSftpOpData::trigger_reset(int res)
{
	if (sftp_) {
		sftp_->cancel(this);
	}
	send_event<reset_event>(res);
}

void CSftpOpData::trigger_next()
{
	send_event<next_event>();
}

void CSftpOpData::operator()(fz::event_base const& ev)
{
	fz::dispatch<reset_event, next_event>(ev, this,
		&CSftpOpData::on_reset_event,
		&CSftpOpData::on_next_event);
}

void CSftpOpData::on_reset_event(int res)
{
	if (res & FZ_REPLY_DISCONNECTED) {
		controlSocket_.DoClose(res);
	}
	else {
		controlSocket_.ResetOperation(res);
	}
}

void CSftpOpData::on_next_event()
{
	controlSocket_.SendNextCommand();
}
