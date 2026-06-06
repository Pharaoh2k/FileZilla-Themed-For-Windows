#include "filezilla.h"
#include "activity_logger_layer.h"
#include "controlsocket.h"
#include "directorycache.h"
#include "engineprivate.h"
#include "lookup.h"
#include "logging_private.h"
#include "proxy.h"
#include "servercapabilities.h"

#include "../include/local_path.h"
#include "../include/engine_options.h"
#include "../include/sizeformatting.h"

#include <libfilezilla/event_loop.hpp>
#include <libfilezilla/iputils.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/rate_limited_layer.hpp>

#include <string.h>

#ifndef FZ_WINDOWS
	#include <sys/stat.h>

	#define mutex mutex_override // Sadly on some platforms system headers include conflicting names
	#include <netdb.h>
	#undef mutex
	#ifndef AI_IDN
		#include <idna.h>
		extern "C" {
			#include <idn-free.h>
		}
	#endif
#endif

CControlSocket::CControlSocket(CFileZillaEnginePrivate & engine, bool use_shm)
	: event_handler(engine, fz::child_event_handler)
	, engine_(engine)
	, opLockManager_(engine.opLockManager_)
	, logger_(engine.GetLogger())
{
	InitBufferPool(use_shm);
}

CControlSocket::~CControlSocket()
{
	remove_handler();

	DoClose();
}

int CControlSocket::Disconnect()
{
	log(logmsg::status, _("Disconnected from server"));

	DoClose();
	return FZ_REPLY_OK;
}

Command CControlSocket::GetCurrentCommandId() const
{
	if (!operations_.empty()) {
		return operations_.back()->opId;
	}

	return Command::none;
}

void CControlSocket::LogTransferResultMessage(int nErrorCode, CFileTransferOpData *pData)
{
	bool tmp{};

	CTransferStatus const status = engine_.transfer_status_.Get(tmp);
	if (!status.empty() && (nErrorCode == FZ_REPLY_OK || status.madeProgress)) {
		int elapsed = static_cast<int>((fz::datetime::now() - status.started).get_seconds());
		if (elapsed <= 0) {
			elapsed = 1;
		}
		std::wstring time = fz::sprintf(fztranslate("%d second", "%d seconds", elapsed), elapsed);

		int64_t transferred = status.currentOffset - status.startOffset;
		std::wstring size = engine_.GetContext().size_formatter().Format(transferred, SizeFormatPurpose::in_line);

		logmsg::type msgType = logmsg::error;
		std::wstring msg;
		if (nErrorCode == FZ_REPLY_OK) {
			msgType = logmsg::status;
			msg = _("File transfer successful, transferred %s in %s");
		}
		else if ((nErrorCode & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
			msg = _("File transfer aborted by user after transferring %s in %s");
		}
		else if ((nErrorCode & FZ_REPLY_CRITICALERROR) == FZ_REPLY_CRITICALERROR) {
			msg = _("Critical file transfer error after transferring %s in %s");
		}
		else {
			msg = _("File transfer failed after transferring %s in %s");
		}
		log(msgType, msg, size, time);
	}
	else {
		if ((nErrorCode & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
			log(logmsg::error, _("File transfer aborted by user"));
		}
		else if (nErrorCode == FZ_REPLY_OK) {
			if (pData->transferInitiated_) {
				log(logmsg::status, _("File transfer successful"));
			}
			else {
				log(logmsg::status, _("File transfer skipped"));
			}
		}
		else if ((nErrorCode & FZ_REPLY_CRITICALERROR) == FZ_REPLY_CRITICALERROR) {
			log(logmsg::error, _("Critical file transfer error"));
		}
		else {
			log(logmsg::error, _("File transfer failed"));
		}
	}
}

void CControlSocket::Push(std::unique_ptr<COpData> && operation)
{
	operations_.emplace_back(std::move(operation));
}

int CControlSocket::ResetOperation(int nErrorCode)
{
	log(logmsg::debug_verbose, L"CControlSocket::ResetOperation(%d)", nErrorCode);

	if (nErrorCode & FZ_REPLY_WOULDBLOCK) {
		log(logmsg::debug_warning, L"ResetOperation with FZ_REPLY_WOULDBLOCK in nErrorCode (%d)", nErrorCode);
	}

	std::unique_ptr<COpData> oldOperation;
	if (!operations_.empty()) {
		oldOperation = std::move(operations_.back());
		operations_.pop_back();

		log(logmsg::debug_verbose, L"%s::Reset(%d) in state %d", oldOperation->name_, nErrorCode, oldOperation->opState);
		nErrorCode = oldOperation->Reset(nErrorCode);
	}
	if (!operations_.empty()) {
		if (nErrorCode == FZ_REPLY_OK ||
			nErrorCode == FZ_REPLY_ERROR ||
			nErrorCode == FZ_REPLY_CRITICALERROR ||
			nErrorCode == FZ_REPLY_ERROR_NOTFOUND)
		{
			if (!oldOperation->topLevelOperation_) {
				return ParseSubcommandResult(nErrorCode, std::move(oldOperation));
			}
		}
		else {
			oldOperation.reset();
			return ResetOperation(nErrorCode);
		}
	}

	std::wstring prefix;
	if ((nErrorCode & FZ_REPLY_CRITICALERROR) == FZ_REPLY_CRITICALERROR &&
		(!oldOperation || oldOperation->opId != Command::transfer))
	{
		prefix = _("Critical error:") + L" ";
	}

	if (oldOperation) {
		const Command commandId = oldOperation->opId;
		switch (commandId)
		{
		case Command::none:
			if (!prefix.empty()) {
				log(logmsg::error, _("Critical error"));
			}
			break;
		case Command::connect:
			if ((nErrorCode & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
				log(logmsg::error, prefix + _("Connection attempt interrupted by user"));
			}
			else if (nErrorCode != FZ_REPLY_OK) {
				log(logmsg::error, prefix + _("Could not connect to server"));
			}
			break;
		case Command::list:
			if ((nErrorCode & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
				log(logmsg::error, prefix + _("Directory listing aborted by user"));
			}
			else if (nErrorCode != FZ_REPLY_OK) {
				log(logmsg::error, prefix + _("Failed to retrieve directory listing"));
			}
			else {
				if (currentPath_.empty()) {
					log(logmsg::status, _("Directory listing successful"));
				}
				else {
					log(logmsg::status, _("Directory listing of \"%s\" successful"), currentPath_.GetPath());
				}
			}
			break;
		case Command::transfer:
			{
				auto & data = static_cast<CFileTransferOpData &>(*oldOperation);
				if (!data.download() && data.transferInitiated_) {
					if (!currentServer_) {
						log(logmsg::debug_warning, L"currentServer_ is empty");
					}
					else {
						UpdateCache(data, data.remotePath_, data.remoteFile_, (nErrorCode == FZ_REPLY_OK) ? data.localFileSize_ : -1);
					}
				}
				LogTransferResultMessage(nErrorCode, &data);
			}
			break;
		default:
			if ((nErrorCode & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
				log(logmsg::error, prefix + _("Interrupted by user"));
			}
			break;
		}

		oldOperation.reset();
	}

	engine_.transfer_status_.Reset();

	if (m_invalidateCurrentPath) {
		currentPath_.clear();
		m_invalidateCurrentPath = false;
	}

	if (operations_.empty()) {
		SetWait(false);
		return engine_.ResetOperation(nErrorCode);
	}
	else {
		return SendNextCommand();
	}
}

void CControlSocket::UpdateCache(COpData const &, CServerPath const& serverPath, std::wstring const& remoteFile, int64_t fileSize)
{
	bool updated = engine_.GetDirectoryCache().UpdateFile(currentServer_, serverPath, remoteFile, true, CDirectoryCache::file, fileSize);
	if (updated) {
		SendDirectoryListingNotification(serverPath, false);
	}
}

int CControlSocket::DoClose(int nErrorCode)
{
	log(logmsg::debug_debug, L"CControlSocket::DoClose(%d)", nErrorCode);
	currentPath_.clear();
	return ResetOperation(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED | nErrorCode);
}

std::wstring CControlSocket::ConvertDomainName(std::wstring const& domain)
{
#ifdef FZ_WINDOWS
	if (domain.size() > 1204 * 1024) {
		return domain;
	}

	int const inlen = static_cast<int>(domain.size() + 1);
	int len = IdnToAscii(IDN_ALLOW_UNASSIGNED, domain.c_str(), inlen, nullptr, 0);
	if (!len) {
		log(logmsg::debug_warning, L"Could not convert domain name");
		return domain;
	}

	wchar_t* output = new wchar_t[len];
	int res = IdnToAscii(IDN_ALLOW_UNASSIGNED, domain.c_str(), inlen, output, len);
	if (!res) {
		delete[] output;
		log(logmsg::debug_warning, L"Could not convert domain name");
		return domain;
	}

	std::wstring ret(output);
	delete [] output;
	return ret;
#elif defined(AI_IDN)
	return domain;
#else
	std::string const utf8 = fz::to_utf8(domain);

	char *output = 0;
	if (idna_to_ascii_8z(utf8.c_str(), &output, IDNA_ALLOW_UNASSIGNED)) {
		log(logmsg::debug_warning, L"Could not convert domain name");
		return domain;
	}

	std::wstring result = fz::to_wstring(std::string(output));
	idn_free(output);
	return result;
#endif
}

void CControlSocket::Cancel()
{
	if (GetCurrentCommandId() != Command::none) {
		if (GetCurrentCommandId() == Command::connect) {
			DoClose(FZ_REPLY_CANCELED);
		}
		else {
			ResetOperation(FZ_REPLY_CANCELED);
		}
	}
}

CServer const& CControlSocket::GetCurrentServer() const
{
	return currentServer_;
}

CServerPath CControlSocket::ParsePath(std::wstring reply)
{
	if (reply.empty()) {
		log(logmsg::error, _("Server returned empty path."));
		return {};
	}

	CServerPath ret = currentPath_;
	ret.SetType(currentServer_.GetType());

	if (!ret.SetPath(reply)) {
		log(logmsg::error, _("Failed to parse returned path."));
		return {};
	}

	return ret;
}

bool CControlSocket::ParsePwdReply(std::wstring reply, CServerPath const& defaultPath, bool quoted)
{
	if (quoted) {
		size_t pos1 = reply.find('"');
		size_t pos2 = reply.rfind('"');
		// Due to searching the same character, pos1 is npos iff pos2 is npos

		if (pos1 == std::wstring::npos || pos1 >= pos2) {
			pos1 = reply.find('\'');
			pos2 = reply.rfind('\'');

			if (pos1 != std::wstring::npos && pos1 < pos2) {
				log(logmsg::debug_info, L"Broken server sending single-quoted path instead of double-quoted path.");
			}
		}
		if (pos1 == std::wstring::npos || pos1 >= pos2) {
			log(logmsg::debug_info, L"Broken server, no quoted path found in pwd reply, trying first token as path");
			pos1 = reply.find(' ');
			if (pos1 != std::wstring::npos) {
				reply = reply.substr(pos1 + 1);
				pos2 = reply.find(' ');
				if (pos2 != std::wstring::npos)
					reply = reply.substr(0, pos2);
			}
			else {
				reply.clear();
			}
		}
		else {
			reply = reply.substr(pos1 + 1, pos2 - pos1 - 1);
			fz::replace_substrings(reply, L"\"\"", L"\"");
		}
	}

	currentPath_ = ParsePath(reply);
	if (currentPath_.empty()) {
		if (defaultPath.empty()) {
			return false;
		}

		log(logmsg::debug_warning, L"Assuming path is '%s'.", defaultPath.GetPath());
		currentPath_ = defaultPath;
	}

	return true;
}

int CControlSocket::CheckOverwriteFile()
{
	log(logmsg::debug_debug, L"CControlSocket::CheckOverwriteFile()");
	if (operations_.empty() || operations_.back()->opId != Command::transfer) {
		log(logmsg::debug_info, L"CheckOverwriteFile called without active transfer.");
		return FZ_REPLY_INTERNALERROR;
	}

	auto & data = static_cast<CFileTransferOpData &>(*operations_.back());
	data.localFileSize_ = data.download() ? data.writer_factory_.size() : data.reader_factory_.size();
	data.localFileTime_ = data.download() ? data.writer_factory_.mtime() : data.reader_factory_.mtime();

	if (data.download()) {
		if (data.localFileSize_ == fz::aio_base::nosize && data.localFileTime_.empty()) {
			return FZ_REPLY_OK;
		}
	}

	CDirentry entry;
	bool dirDidExist;
	bool matchedCase;
	CServerPath remotePath;
	if (data.tryAbsolutePath_ || currentPath_.empty()) {
		remotePath = data.remotePath_;
	}
	else {
		remotePath = currentPath_;
	}
	bool found = engine_.GetDirectoryCache().LookupFile(entry, currentServer_, remotePath, data.remoteFile_, dirDidExist, matchedCase);

	// Ignore entries with wrong case
	if (found && !matchedCase) {
		found = false;
	}

	if (found) {
		if (data.remoteFileTime_.empty() && entry.has_date()) {
			data.remoteFileTime_ = entry.time;
		}
	}

	if (!data.download()) {
		if (!found && data.remoteFileSize_ < 0 && data.remoteFileTime_.empty()) {
			return FZ_REPLY_OK;
		}
	}



	auto notification = std::make_unique<CFileExistsNotification>();

	notification->download = data.download();
	notification->localFile = data.localName_;
	notification->remoteFile = data.remoteFile_;
	notification->remotePath = data.remotePath_;
	notification->localSize = static_cast<int64_t>(data.localFileSize_);
	notification->localTime = data.localFileTime_;
	notification->remoteSize = data.remoteFileSize_;
	notification->remoteTime = data.remoteFileTime_;

	if (currentServer_.HasFeature(ProtocolFeature::DataTypeConcept)) {
		notification->ascii = data.flags_ & ftp_transfer_flags::ascii;
	}

	if (data.download() && notification->localSize >= 0) {
		notification->canResume = true;
	}
	else if (!data.download() && notification->remoteSize >= 0) {
		notification->canResume = true;
	}
	else {
		notification->canResume = false;
	}

	SendAsyncRequest(std::move(notification));

	return FZ_REPLY_WOULDBLOCK;
}

SleepOpData::SleepOpData(CControlSocket & controlSocket, fz::duration const& delay)
	: COpData(Command::sleep, L"SleepOpData")
	, fz::event_handler(controlSocket, fz::child_event_handler)
	, controlSocket_(controlSocket)
{
	add_timer(delay, true);
	controlSocket_.SetWait(false);
}

void SleepOpData::operator()(fz::event_base const&)
{
	controlSocket_.ResetOperation(FZ_REPLY_OK);
}

CFileTransferOpData::CFileTransferOpData(wchar_t const* name, CFileTransferCommand const& cmd)
	: COpData(Command::transfer, name)
	, flags_(cmd.GetFlags())
	, reader_factory_(cmd.GetReader())
	, writer_factory_(cmd.GetWriter())
	, localName_(reader_factory_ ? reader_factory_.name() : writer_factory_.name())
	, remoteFile_(cmd.GetRemoteFile())
	, remotePath_(cmd.GetRemotePath())
{
	localFileSize_ = download() ? writer_factory_.size() : reader_factory_.size();
	localFileTime_ = download() ? writer_factory_.mtime() : reader_factory_.mtime();
}

std::wstring CControlSocket::ConvToLocal(char const* buffer, size_t len)
{
	std::wstring ret;

	if (!len) {
		return ret;
	}

	if (m_useUTF8) {
		ret = fz::to_wstring_from_utf8(buffer, len);
		if (!ret.empty()) {
			return ret;
		}

		if (!shown_encoding_error_) {
			log(logmsg::error, _("Invalid character sequence received. Some or all files may not appear in directory listings. If the server you are connecting to does not support UTF-8, select a different character encoding in the Site Manager."));
			shown_encoding_error_ = true;
		}
	}
	else {
		ret = engine_.GetEncodingConverter().toLocal(currentServer_.GetCustomEncoding(), buffer, len);
		if (!ret.empty()) {
			return ret;
		}

		if (!shown_encoding_error_) {
			log(logmsg::error, _("Invalid character sequence received. The server does not appear to use the custom character encoding (%s) you have configured in the Site Manager."), currentServer_.GetCustomEncoding());
			shown_encoding_error_ = true;
		}
	}

	return ret;
}

std::string CControlSocket::ConvToServer(std::wstring const& str)
{
	if (m_useUTF8) {
		return fz::to_utf8(str);
	}

	std::string ret = engine_.GetEncodingConverter().toServer(currentServer_.GetCustomEncoding(), str.c_str(), str.size());
	if (ret.empty()) {
		if (!shown_encoding_error_) {
			log(logmsg::error, _("Cannot convert to the server's character set. You can only use characters that can be encoded using the custom character encoding (%s) you have configured in the Site Manager."), currentServer_.GetCustomEncoding());
			shown_encoding_error_ = true;
		}
	}
	return ret;
}

void CControlSocket::OnTimer(fz::timer_id)
{
	m_timer = 0; // It's a one-shot timer, no need to stop it

	int const timeout = engine_.GetOptions().get_int(OPTION_TIMEOUT);
	if (timeout > 0) {
		fz::duration elapsed = fz::monotonic_clock::now() - m_lastActivity;

		if (!pending_global_async_request_ && (operations_.empty() || !operations_.back()->pending_async_request_) && !opLockManager_.Waiting(this)) {
			if (elapsed > fz::duration::from_seconds(timeout)) {
				log(logmsg::error, fztranslate("Connection timed out after %d second of inactivity", "Connection timed out after %d seconds of inactivity", timeout), timeout);
				DoClose(FZ_REPLY_TIMEOUT);
				return;
			}
		}
		else {
			elapsed = fz::duration();
		}

		m_timer = add_timer(fz::duration::from_milliseconds(timeout * 1000) - elapsed, true);
	}
}

void CControlSocket::SetAlive()
{
	m_lastActivity = fz::monotonic_clock::now();
}

void CControlSocket::SetWait(bool wait)
{
	if (wait) {
		if (m_timer) {
			return;
		}

		m_lastActivity = fz::monotonic_clock::now();

		int timeout = engine_.GetOptions().get_int(OPTION_TIMEOUT);
		if (!timeout) {
			return;
		}

		m_timer = add_timer(fz::duration::from_milliseconds(timeout * 1000 + 100), true); // Add a bit of slack
	}
	else {
		stop_timer(m_timer);
		m_timer = 0;
	}
}

int CControlSocket::SendNextCommand()
{
	log(logmsg::debug_verbose, L"CControlSocket::SendNextCommand()");
	if (operations_.empty()) {
		log(logmsg::debug_warning, L"SendNextCommand called without active operation");
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	while (!operations_.empty()) {
		auto & data = *operations_.back();
		if (data.pending_async_request_ && data.pending_async_request_->second == async_request_type::waiting) {
			log(logmsg::debug_info, L"Waiting for async request, ignoring SendNextCommand...");
			return FZ_REPLY_WOULDBLOCK;
		}

		if (!CanSendNextCommand()) {
			SetWait(true);
			return FZ_REPLY_WOULDBLOCK;
		}

		log(data.sendLogLevel_, L"%s::Send() in state %d", data.name_, data.opState);
		int res = data.Send();
		if (res != FZ_REPLY_CONTINUE) {
			if (res == FZ_REPLY_OK) {
				return ResetOperation(res);
			}
			else if (res & FZ_REPLY_DISCONNECTED) {
				return DoClose(res);
			}
			else if (res & FZ_REPLY_ERROR) {
				return ResetOperation(res);
			}
			else if (res == FZ_REPLY_WOULDBLOCK) {
				return FZ_REPLY_WOULDBLOCK;
			}
			else if (res != FZ_REPLY_CONTINUE) {
				log(logmsg::debug_warning, L"Unknown result %d returned by COpData::Send()", res);
				return ResetOperation(FZ_REPLY_INTERNALERROR);
			}
		}
	}

	return FZ_REPLY_OK;
}

int CControlSocket::ParseSubcommandResult(int prevResult, std::unique_ptr<COpData> && previousOperation)
{
	if (operations_.empty()) {
		log(logmsg::debug_warning, L"CControlSocket::ParseSubcommandResult(%d) called without active operation", prevResult);
		previousOperation.reset();
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	auto & data = *operations_.back();
	log(logmsg::debug_verbose, L"%s::SubcommandResult(%d) in state %d", data.name_, prevResult, data.opState);
	int res = data.SubcommandResult(prevResult, *previousOperation);

	previousOperation.reset();

	if (res == FZ_REPLY_WOULDBLOCK) {
		return FZ_REPLY_WOULDBLOCK;
	}
	else if (res == FZ_REPLY_CONTINUE) {
		return SendNextCommand();
	}
	else {
		return ResetOperation(res);
	}
}

OpLock CControlSocket::Lock(locking_reason reason, CServerPath const& path, bool inclusive)
{
	return opLockManager_.Lock(this, reason, path, inclusive);
}

void CControlSocket::OnObtainLock()
{
	if (opLockManager_.ObtainWaiting(this)) {
		SendNextCommand();
	}
}

void CControlSocket::InvalidateCurrentWorkingDir(CServerPath const& path)
{
	if (path.empty() || currentPath_.empty()) {
		return;
	}

	if (path.IsParentOf(currentPath_, false, true)) {
		if (!operations_.empty()) {
			m_invalidateCurrentPath = true;
		}
		else {
			currentPath_.clear();
		}
	}
}

fz::duration CControlSocket::GetInferredTimezoneOffset() const
{
	fz::duration ret;
	if (currentServer_) {
		int seconds = 0;
		if (CServerCapabilities::GetCapability(currentServer_, inferred_timezone_offset, &seconds) == yes) {
			ret = fz::duration::from_seconds(seconds);
		}
	}
	return ret;
}

void CControlSocket::SendAsyncRequest(std::unique_ptr<CAsyncRequestNotification> && notification, async_request_type type)
{
	if (!notification || (operations_.empty() && type != async_request_type::global)) {
		return;
	}

	auto requestNumber = std::make_shared<unsigned int>(engine_.GetNextAsyncRequestNumber());

	notification->requestNumber_ = requestNumber;

	if (type == async_request_type::global) {
		pending_global_async_request_ = std::move(requestNumber);
	}
	else {
		operations_.back()->pending_async_request_ = std::make_pair(std::move(requestNumber), (type == async_request_type::waiting) ? async_request_type::waiting : async_request_type::parallel);
	}
	engine_.AddNotification(std::move(notification));
}

// ------------------
// CRealControlSocket
// ------------------

CRealControlSocket::CRealControlSocket(CFileZillaEnginePrivate & engine)
	: CControlSocket(engine)
{
}

CRealControlSocket::~CRealControlSocket()
{
	ResetSocket();
}

int CRealControlSocket::Send(unsigned char const* buffer, unsigned int len)
{
	if (!active_layer_) {
		log(logmsg::debug_warning, L"Called internal CRealControlSocket::Send without m_pBackend");
		return FZ_REPLY_INTERNALERROR;
	}

	SetWait(true);
	if (send_buffer_) {
		send_buffer_.append(buffer, len);
	}
	else {
		int error;
		int written = active_layer_->write(buffer, len, error);
		if (written < 0) {
			if (error != EAGAIN) {
				log(logmsg::error, _("Could not write to socket: %s"), fz::socket_error_description(error));
				log(logmsg::error, _("Disconnected from server"));
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
			written = 0;
		}

		if (written) {
			SetAlive();
		}

		if (static_cast<unsigned int>(written) < len) {
			send_buffer_.append(buffer + written, len - written);
		}
	}

	return FZ_REPLY_WOULDBLOCK;
}

void CRealControlSocket::operator()(fz::event_base const& ev)
{
	if (!fz::dispatch<fz::socket_event, fz::hostaddress_event>(ev, this,
		&CRealControlSocket::OnSocketEvent,
		&CRealControlSocket::OnHostAddress))
	{
		CControlSocket::operator()(ev);
	}
}

void CRealControlSocket::OnSocketEvent(fz::socket_event_source*, fz::socket_event_flag t, int error)
{
	if (!active_layer_) {
		return;
	}

	switch (t)
	{
	case fz::socket_event_flag::connection_next:
		if (error) {
			log(logmsg::status, _("Connection attempt failed with \"%s\", trying next address."), fz::socket_error_description(error));
		}
		SetAlive();
		break;
	case fz::socket_event_flag::connection:
		if (error) {
			log(logmsg::status, _("Connection attempt failed with \"%s\"."), fz::socket_error_description(error));
			OnSocketError(error);
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
			OnReceive();
		}
		break;
	case fz::socket_event_flag::write:
		if (error) {
			OnSocketError(error);
		}
		else {
			OnSend();
		}
		break;
	default:
		log(logmsg::debug_warning, L"Unhandled socket event %d", t);
		break;
	}
}

void CRealControlSocket::OnHostAddress(fz::socket_event_source*, std::string const& address)
{
	if (!active_layer_) {
		return;
	}

	log(logmsg::status, _("Connecting to %s..."), address);
}

void CRealControlSocket::OnConnect()
{
}

void CRealControlSocket::OnReceive()
{
}

int CRealControlSocket::OnSend()
{
	while (send_buffer_) {
		int error;
		int written = active_layer_->write(send_buffer_.get(), send_buffer_.size(), error);
		if (written < 0) {
			if (error != EAGAIN) {
				log(logmsg::error, _("Could not write to socket: %s"), fz::socket_error_description(error));
				if (GetCurrentCommandId() != Command::connect) {
					log(logmsg::error, _("Disconnected from server"));
				}
				DoClose();
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
			return FZ_REPLY_WOULDBLOCK;
		}

		if (written) {
			SetAlive();
			send_buffer_.consume(static_cast<size_t>(written));
		}
	}

	return FZ_REPLY_CONTINUE;
}

void CRealControlSocket::OnSocketError(int error)
{
	log(logmsg::debug_verbose, L"CRealControlSocket::OnSocketError(%d)", error);

	auto cmd = GetCurrentCommandId();
	if (cmd != Command::connect) {
		auto messageType = (cmd == Command::none) ? logmsg::status : logmsg::error;
		log(messageType, _("Disconnected from server: %s"), fz::socket_error_description(error));
	}
	DoClose();
}

void CRealControlSocket::CreateSocket(std::wstring const& host)
{
	ResetSocket();
	socket_ = std::make_unique<fz::socket>(engine_.GetThreadPool(), nullptr);
	activity_logger_layer_ = std::make_unique<activity_logger_layer>(nullptr, *socket_, engine_.activity_logger_);
	ratelimit_layer_ = std::make_unique<fz::rate_limited_layer>(nullptr, *activity_logger_layer_, &engine_.GetRateLimiter());
	active_layer_ = ratelimit_layer_.get();

	const int proxy_type = engine_.GetOptions().get_int(OPTION_PROXY_TYPE);
	if (proxy_type > static_cast<int>(ProxyType::NONE) && proxy_type < static_cast<int>(ProxyType::count) && !currentServer_.GetBypassProxy()) {
		log(logmsg::status, _("Connecting to %s through %s proxy"), currentServer_.Format(ServerFormat::with_optional_port), ProxyBase::Name(static_cast<ProxyType>(proxy_type)));

		fz::native_string proxy_host = fz::to_native(engine_.GetOptions().get_string(OPTION_PROXY_HOST));
		proxy_layer_ = CreateProxy(nullptr, *active_layer_, this, static_cast<ProxyType>(proxy_type),
			proxy_host, engine_.GetOptions().get_int(OPTION_PROXY_PORT),
			engine_.GetOptions().get_string(OPTION_PROXY_USER),
			engine_.GetOptions().get_string(OPTION_PROXY_PASS));
		active_layer_ = proxy_layer_.get();

		if (fz::get_address_type(proxy_host) == fz::address_type::unknown) {
			log(logmsg::status, _("Resolving address of %s"), proxy_host);
		}
	}
	else {
		if (fz::get_address_type(host) == fz::address_type::unknown) {
			log(logmsg::status, _("Resolving address of %s"), host);
		}
	}

	SetSocketBufferSizes();
}

int CRealControlSocket::DoConnect(std::wstring const& host, unsigned int port)
{
	SetWait(true);

	if (currentServer_.GetEncodingType() == ENCODING_CUSTOM) {
		log(logmsg::debug_info, L"Using custom encoding: %s", currentServer_.GetCustomEncoding());
	}

	CreateSocket(host);

	active_layer_->set_event_handler(this);
	int res = active_layer_->connect(fz::to_native(ConvertDomainName(host)), port);

	if (res) {
		log(logmsg::error, _("Could not connect to server: %s"), fz::socket_error_description(res));
		return FZ_REPLY_DISCONNECTED | FZ_REPLY_ERROR;
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CRealControlSocket::DoClose(int nErrorCode)
{
	log(logmsg::debug_debug, L"CRealControlSocket::DoClose(%d)", nErrorCode);
	ResetSocket();

	return CControlSocket::DoClose(nErrorCode);
}

void CRealControlSocket::ResetSocket()
{
	active_layer_ = nullptr;

	// Destroy in reverse order
	proxy_layer_.reset();
	ratelimit_layer_.reset();
	activity_logger_layer_.reset();
	socket_.reset();

	send_buffer_.clear();
}

bool CControlSocket::SetFileExistsAction(CFileExistsNotification *pFileExistsNotification)
{
	if (!pFileExistsNotification) {
		return false;
	}

	if (operations_.empty() || operations_.back()->opId != Command::transfer) {
		log(logmsg::debug_info, L"SetFileExistsAction: No or invalid operation in progress, ignoring request reply %f", pFileExistsNotification->GetRequestID());
		return false;
	}

	auto & data = static_cast<CFileTransferOpData &>(*operations_.back());

	switch (pFileExistsNotification->overwriteAction)
	{
	case CFileExistsNotification::overwrite:
		SendNextCommand();
		break;
	case CFileExistsNotification::overwriteNewer:
		if (pFileExistsNotification->localTime.empty() || pFileExistsNotification->remoteTime.empty()) {
			SendNextCommand();
		}
		else if (pFileExistsNotification->download && pFileExistsNotification->localTime.earlier_than(pFileExistsNotification->remoteTime)) {
			SendNextCommand();
		}
		else if (!pFileExistsNotification->download && pFileExistsNotification->localTime.later_than(pFileExistsNotification->remoteTime)) {
			SendNextCommand();
		}
		else {
			if (data.download()) {
				std::wstring filename = data.remotePath_.FormatFilename(data.remoteFile_);
				log(logmsg::status, _("Skipping download of %s"), filename);
			}
			else {
				log(logmsg::status, _("Skipping upload of %s"), data.localName_);
			}
			ResetOperation(FZ_REPLY_OK);
		}
		break;
	case CFileExistsNotification::overwriteSize:
		// First compare flags both size known but different, one size known and the other not (obviously they are different).
		// Second compare flags the remaining case in which we need to send command : both size unknown
		if ((pFileExistsNotification->localSize != pFileExistsNotification->remoteSize) || (pFileExistsNotification->localSize < 0)) {
			SendNextCommand();
		}
		else {
			if (data.download()) {
				std::wstring filename = data.remotePath_.FormatFilename(data.remoteFile_);
				log(logmsg::status, _("Skipping download of %s"), filename);
			}
			else {
				log(logmsg::status, _("Skipping upload of %s"), data.localName_);
			}
			ResetOperation(FZ_REPLY_OK);
		}
		break;
	case CFileExistsNotification::overwriteSizeOrNewer:
		if (pFileExistsNotification->localTime.empty() || pFileExistsNotification->remoteTime.empty()) {
			SendNextCommand();
		}
		// First compare flags both size known but different, one size known and the other not (obviously they are different).
		// Second compare flags the remaining case in which we need to send command : both size unknown
		else if ((pFileExistsNotification->localSize != pFileExistsNotification->remoteSize) || (pFileExistsNotification->localSize < 0)) {
			SendNextCommand();
		}
		else if (pFileExistsNotification->download && pFileExistsNotification->localTime.earlier_than(pFileExistsNotification->remoteTime)) {
			SendNextCommand();
		}
		else if (!pFileExistsNotification->download && pFileExistsNotification->localTime.later_than(pFileExistsNotification->remoteTime)) {
			SendNextCommand();
		}
		else {
			if (data.download()) {
				auto const filename = data.remotePath_.FormatFilename(data.remoteFile_);
				log(logmsg::status, _("Skipping download of %s"), filename);
			}
			else {
				log(logmsg::status, _("Skipping upload of %s"), data.localName_);
			}
			ResetOperation(FZ_REPLY_OK);
		}
		break;
	case CFileExistsNotification::resume:
		if (data.download() && data.localFileSize_ != fz::aio_base::nosize) {
			data.resume_ = true;
		}
		else if (!data.download() && data.remoteFileSize_ >= 0) {
			data.resume_ = true;
		}
		SendNextCommand();
		break;
	case CFileExistsNotification::rename:
		if (data.download()) {
			{
				if (!pFileExistsNotification->new_writer_factory_) {
					ResetOperation(FZ_REPLY_INTERNALERROR);
					return false;
				}

				data.writer_factory_ = std::move(pFileExistsNotification->new_writer_factory_);
				data.localName_ = data.writer_factory_.name();
			}

			data.localFileSize_ = data.writer_factory_.size();
			data.localFileTime_ = data.writer_factory_.mtime();

			if (CheckOverwriteFile() == FZ_REPLY_OK) {
				SendNextCommand();
			}
		}
		else {
			data.remoteFile_ = pFileExistsNotification->newName;
			data.remoteFileSize_ = -1;
			data.remoteFileTime_ = fz::datetime();

			CDirentry entry;
			bool dir_did_exist;
			bool matched_case;
			if (engine_.GetDirectoryCache().LookupFile(entry, currentServer_, data.tryAbsolutePath_ ? data.remotePath_ : currentPath_, data.remoteFile_, dir_did_exist, matched_case) &&
				matched_case)
			{
				data.remoteFileSize_ = entry.size;
				if (entry.has_date()) {
					data.remoteFileTime_ = entry.time;
				}

				if (CheckOverwriteFile() != FZ_REPLY_OK) {
					break;
				}
			}

			SendNextCommand();
		}
		break;
	case CFileExistsNotification::skip:
		if (data.download()) {
			std::wstring filename = data.remotePath_.FormatFilename(data.remoteFile_);
			log(logmsg::status, _("Skipping download of %s"), filename);
		}
		else {
			log(logmsg::status, _("Skipping upload of %s"), data.localName_);
		}
		ResetOperation(FZ_REPLY_OK);
		break;
	default:
		log(logmsg::debug_warning, L"Unknown file exists action: %d", pFileExistsNotification->overwriteAction);
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return false;
	}

	return true;
}

void CControlSocket::List(CServerPath const&, std::wstring const&, int)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::RawCommand(std::wstring const&)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::Delete(CServerPath const&, std::vector<std::wstring>&&)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::RemoveDir(CServerPath const&, std::wstring const&)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::Mkdir(CServerPath const&, transfer_flags const&)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::Rename(CRenameCommand const&)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::Chmod(CChmodCommand const&)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::Lookup(CServerPath const& path, std::wstring const& file, CDirentry * entry)
{
	Push(std::make_unique<LookupOpData>(*this, path, file, entry));
}

void CControlSocket::Lookup(CServerPath const& path, std::vector<std::wstring> const& files)
{
	Push(std::make_unique<LookupManyOpData>(*this, path, files));
}

void CControlSocket::operator()(fz::event_base const& ev)
{
	fz::dispatch<fz::timer_event, CObtainLockEvent>(ev, this,
		&CControlSocket::OnTimer,
		&CControlSocket::OnObtainLock);
}

void CControlSocket::RecordActivity(activity_logger::_direction direction, uint64_t amount)
{
	SetAlive();
	engine_.activity_logger_.record(direction, amount);
}

void CControlSocket::SendDirectoryListingNotification(CServerPath const& path, bool failed)
{
	if (!currentServer_) {
		return;
	}

	engine_.AddNotification(std::make_unique<CDirectoryListingNotification>(path, operations_.size() == 1 && operations_.back()->opId == Command::list, failed));
}

void CControlSocket::CallSetAsyncRequestReply(CAsyncRequestNotification *pNotification)
{
	if (!pNotification) {
		return;
	}
	auto sReqNr = pNotification->requestNumber_.lock();
	if (!sReqNr) {
		log(logmsg::debug_info, L"Ignoring request replywith request id %d that isn't pending.", pNotification->GetRequestID());
		return;
	}

	if (pending_global_async_request_ && *pending_global_async_request_ == *sReqNr) {
		log(logmsg::debug_info, L"Got reply to pending global async request %u with request id %d", *sReqNr, pNotification->GetRequestID());
		pending_global_async_request_.reset();
	}
	else if (!operations_.empty() && operations_.back()->pending_async_request_) {
		log(logmsg::debug_info, L"Got reply to pending operation async request %u with request id %d", *sReqNr, pNotification->GetRequestID());
		operations_.back()->pending_async_request_.reset();
	}
	else {
		log(logmsg::debug_info, L"Not waiting for request reply, ignoring request reply %u with request id %d", *sReqNr, pNotification->GetRequestID());
		return;
	}

	SetAlive();
	SetAsyncRequestReply(pNotification);
}

void CControlSocket::Sleep(fz::duration const& delay)
{
	Push(std::make_unique<SleepOpData>(*this, delay));
}

bool CControlSocket::InitBufferPool(bool use_shm)
{
	if (!buffer_pool_) {
		buffer_pool_.emplace(engine_.GetThreadPool(), logger_, 8, 0, use_shm);
	}
	return *buffer_pool_;
}

std::unique_ptr<fz::writer_base> CControlSocket::OpenWriter(fz::writer_factory_holder & factory, uint64_t resumeOffset, bool withProgress)
{
	if (!factory || !buffer_pool_) {
		return {};
	}

	auto file_writer = dynamic_cast<fz::file_writer_factory*>(&*factory);
	if (file_writer) {
		std::wstring tmp;
		CLocalPath local_path(file_writer->name(), &tmp);
		if (local_path.HasParent()) {
			fz::native_string last_created;
			fz::mkdir(fz::to_native(local_path.GetPath()), true, fz::mkdir_permissions::normal, &last_created);
			if (!last_created.empty()) {
				// Send out notification
				auto n = std::make_unique<CLocalDirCreatedNotification>();
				if (n->dir.SetPath(fz::to_wstring(last_created))) {
					engine_.AddNotification(std::move(n));
				}
			}
		}
	}

	fz::writer_base::progress_cb_t status_update;
	if (withProgress) {
		status_update = [&s = engine_.transfer_status_](fz::writer_base const*, uint64_t written) {
			s.SetMadeProgress();
			s.Update(written);
		};
	}
	return factory->open(*buffer_pool_, resumeOffset, status_update, max_buffer_count());
}

int64_t CalculateNextChunkSize(int64_t remaining, int64_t lastChunkSize, fz::duration const& lastChunkDuration, int64_t minChunkSize, int64_t multiple, int64_t partCount, int64_t maxPartCount, int64_t maxChunkSize)
{
	if (remaining <= 0) {
		return 0;
	}

	int64_t newChunkSize = minChunkSize;

	if (lastChunkDuration && lastChunkSize) {
		int64_t size = lastChunkSize * 30000 / lastChunkDuration.get_milliseconds();
		if (size > newChunkSize) {
			newChunkSize = size;
		}
	}

	if (maxPartCount) {
		if (newChunkSize * (maxPartCount - partCount) < remaining) {
			if (maxPartCount - partCount <= 1) {
				newChunkSize = remaining;
			}
			else {
				newChunkSize = remaining / (maxPartCount - partCount - 1);
			}
		}
	}

	if (multiple) {
		// Round up
		auto modulus = newChunkSize % multiple;
		if (modulus) {
			newChunkSize += multiple - modulus;
		}
	}

	if (maxChunkSize && newChunkSize > maxChunkSize) {
		newChunkSize = maxChunkSize;
	}

	if (newChunkSize > remaining) {
		newChunkSize = remaining;
	}

	return newChunkSize;
}

int64_t CalculateNextChunkSize(int64_t remaining, int64_t lastChunkSize, fz::monotonic_clock const& lastChunkStart, int64_t minChunkSize, int64_t multiple, int64_t partCount, int64_t maxPartCount, int64_t maxChunkSize)
{
	return CalculateNextChunkSize(remaining, lastChunkSize, fz::monotonic_clock::now() - lastChunkStart, minChunkSize, multiple, partCount, maxPartCount, maxChunkSize);
}

size_t CControlSocket::max_buffer_count() const
{
	return buffer_pool_->buffer_count();
}
