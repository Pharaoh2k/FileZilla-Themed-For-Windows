#include "../filezilla.h"

#include "filetransfer.h"
#include "transfersocket.h"

#include "../directorycache.h"
#include "../servercapabilities.h"
#include "../../include/engine_options.h"

#include <libfilezilla/file.hpp>
#include <libfilezilla/local_filesys.hpp>

#include <assert.h>

namespace {
enum filetransferStates
{
        filetransfer_init = 0,
        filetransfer_waitcwd,
        filetransfer_waitlist,
        filetransfer_size,
        filetransfer_mdtm,
        filetransfer_resumetest,
        filetransfer_transfer,
        filetransfer_waittransfer,
        filetransfer_waitresumetest,
        filetransfer_mfmt
};
}

CFtpFileTransferOpData::CFtpFileTransferOpData(CFtpControlSocket& controlSocket, CFileTransferCommand const& cmd)
	: CFileTransferOpData(L"CFtpFileTransferOpData", cmd)
	, CFtpOpData(controlSocket)
{
	binary = !(cmd.GetFlags() & ftp_transfer_flags::ascii);
}

int CFtpFileTransferOpData::Send()
{
	std::wstring cmd;
	switch (opState)
	{
	case filetransfer_init:
		if (download()) {
			std::wstring filename = remotePath_.FormatFilename(remoteFile_);
			log(logmsg::status, _("Starting download of %s"), filename);
		}
		else {
			log(logmsg::status, _("Starting upload of %s"), localName_);
		}

		localFileSize_ = download() ? writer_factory_.size() : reader_factory_.size();

		opState = filetransfer_waitcwd;

		remotePath_.SetType(currentServer_.GetType());

		controlSocket_.ChangeDir(remotePath_);
		return FZ_REPLY_CONTINUE;
	case filetransfer_size:
		cmd = L"SIZE ";
		cmd += remotePath_.FormatFilename(remoteFile_, !tryAbsolutePath_);
		break;
	case filetransfer_mdtm:
		cmd = L"MDTM ";
		cmd += remotePath_.FormatFilename(remoteFile_, !tryAbsolutePath_);
		break;
	case filetransfer_resumetest:
	case filetransfer_transfer:
		if (controlSocket_.m_pTransferSocket) {
			log(logmsg::debug_verbose, L"m_pTransferSocket != 0");
			controlSocket_.m_pTransferSocket.reset();
		}

		{
			resumeOffset = 0;
			if (download()) {
				// Potentially racy
				localFileSize_ = writer_factory_.size();
				fileDidExist_ = localFileSize_ != fz::aio_base::nosize;

				if (resume_) {
					resumeOffset = fileDidExist_ ? static_cast<int64_t>(localFileSize_) : 0;

					// Check resume capabilities
					if (opState == filetransfer_resumetest) {
						int res = TestResumeCapability();
						if (res != FZ_REPLY_CONTINUE || opState != filetransfer_resumetest) {
							return res;
						}
					}
				}
				else {
					localFileSize_ = 0;
				}

				engine_.transfer_status_.Init(remoteFileSize_, resumeOffset, false);
			}
			else {
				if (resume_) {
					if (remoteFileSize_ > 0) {
						resumeOffset = remoteFileSize_;

						if (localFileSize_ != fz::aio_base::nosize && resumeOffset >= static_cast<int64_t>(localFileSize_) && binary) {
							log(logmsg::debug_info, L"No need to resume, remote file size matches local file size.");

							if (options_.get_int(OPTION_PRESERVE_TIMESTAMPS) &&
								CServerCapabilities::GetCapability(currentServer_, mfmt_command) == yes)
							{
								localFileTime_ = reader_factory_.mtime();
								if (!localFileTime_.empty()) {
									opState = filetransfer_mfmt;
									return FZ_REPLY_CONTINUE;
								}
							}
							return FZ_REPLY_OK;
						}
					}
				}

				engine_.transfer_status_.Init(reader_factory_.size(), resumeOffset, false);
			}

			controlSocket_.m_pTransferSocket = std::make_unique<CTransferSocket>(engine_, controlSocket_, download() ? TransferMode::download : TransferMode::upload);
			controlSocket_.m_pTransferSocket->m_binaryMode = binary;
			if (download()) {
				auto writer = controlSocket_.OpenWriter(writer_factory_, resumeOffset, true);
				if (!writer) {
					return FZ_REPLY_CRITICALERROR;
				}
				if (options_.get_int(OPTION_PREALLOCATE_SPACE)) {
					if (remoteFileSize_ >= 0 && remoteFileSize_ > resumeOffset) {
						if (writer->preallocate(static_cast<uint64_t>(remoteFileSize_ - resumeOffset)) != fz::aio_result::ok) {
							return FZ_REPLY_ERROR;
						}
					}
				}
				controlSocket_.m_pTransferSocket->set_writer(std::move(writer), flags_ & ftp_transfer_flags::ascii);
			}
			else {
				auto reader = reader_factory_->open(*controlSocket_.buffer_pool_, resumeOffset, fz::aio_base::nosize, controlSocket_.max_buffer_count());
				if (!reader) {
					return FZ_REPLY_CRITICALERROR;
				}
				controlSocket_.m_pTransferSocket->set_reader(std::move(reader), flags_ & ftp_transfer_flags::ascii);
			}
		}

		if (download()) {
			cmd = L"RETR ";
		}
		else if (resume_ && resumeOffset != 0) {
			if (CServerCapabilities::GetCapability(currentServer_, rest_stream) == yes) {
				cmd = L"STOR "; // In this case REST gets sent since resume offset was set earlier
			}
			else {
				cmd = L"APPE ";
			}
		}
		else {
			cmd = L"STOR ";
		}
		cmd += remotePath_.FormatFilename(remoteFile_, !tryAbsolutePath_);

		opState = filetransfer_waittransfer;
		controlSocket_.Transfer(cmd, this);
		return FZ_REPLY_CONTINUE;
	case filetransfer_mfmt:
	{
		cmd = L"MFMT ";
		fz::datetime t = localFileTime_;
		t -= fz::duration::from_minutes(currentServer_.GetTimezoneOffset());
		cmd += t.format(L"%Y%m%d%H%M%S ", fz::datetime::utc);
		cmd += remotePath_.FormatFilename(remoteFile_, !tryAbsolutePath_);

		break;
	}
	default:
		log(logmsg::debug_warning, L"Unhandled opState: %d", opState);
		return FZ_REPLY_ERROR;
	}

	if (!cmd.empty()) {
		return controlSocket_.SendCommand(cmd);
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CFtpFileTransferOpData::TestResumeCapability()
{
	log(logmsg::debug_verbose, L"CFtpFileTransferOpData::TestResumeCapability()");

	if (!download()) {
		return FZ_REPLY_CONTINUE;
	}

	for (int i = 0; i < 2; ++i) {
		if (localFileSize_ >= (1ull << (i ? 31 : 32))) {
			switch (CServerCapabilities::GetCapability(currentServer_, i ? resume2GBbug : resume4GBbug))
			{
			case yes:
				if (static_cast<uint64_t>(remoteFileSize_) == localFileSize_) {
					log(logmsg::debug_info, _("Server does not support resume of files > %d GB. End transfer since file sizes match."), i ? 2 : 4);
					return FZ_REPLY_OK;
				}
				log(logmsg::error, _("Server does not support resume of files > %d GB."), i ? 2 : 4);
				return FZ_REPLY_CRITICALERROR;
			case unknown:
				if (static_cast<uint64_t>(remoteFileSize_) < localFileSize_) {
					// Don't perform test
					break;
				}
				if (static_cast<uint64_t>(remoteFileSize_) == localFileSize_) {
					log(logmsg::debug_info, _("Server may not support resume of files > %d GB. End transfer since file sizes match."), i ? 2 : 4);
					return FZ_REPLY_OK;
				}
				else if (static_cast<uint64_t>(remoteFileSize_) > localFileSize_) {
					log(logmsg::status, _("Testing resume capabilities of server"));

					opState = filetransfer_waitresumetest;
					resumeOffset = remoteFileSize_ - 1;

					controlSocket_.m_pTransferSocket = std::make_unique<CTransferSocket>(engine_, controlSocket_, TransferMode::resumetest);

					controlSocket_.Transfer(L"RETR " + remotePath_.FormatFilename(remoteFile_, !tryAbsolutePath_), this);
					return FZ_REPLY_CONTINUE;
				}
				break;
			case no:
				break;
			}
		}
	}

	return FZ_REPLY_CONTINUE;
}

int CFtpFileTransferOpData::ParseResponse()
{
	int code = controlSocket_.GetReplyCode();
	auto const& response = controlSocket_.m_Response;

	switch (opState)
	{
	case filetransfer_size:
		if (code != 2 && code != 3) {
			if (CServerCapabilities::GetCapability(currentServer_, size_command) == yes ||
				fz::str_tolower_ascii(response.substr(4)) == L"file not found" ||
				(fz::str_tolower_ascii(remotePath_.FormatFilename(remoteFile_)).find(L"file not found") == std::wstring::npos &&
					fz::str_tolower_ascii(response).find(L"file not found") != std::wstring::npos))
			{
				// Server supports SIZE command but command failed. Most likely MDTM will fail as well, so
				// skip it.
				opState = filetransfer_resumetest;

				int res = controlSocket_.CheckOverwriteFile();
				if (res != FZ_REPLY_OK) {
					return res;
				}
			}
			else {
				opState = filetransfer_mdtm;
			}
		}
		else {
			opState = filetransfer_mdtm;
			if (response.substr(0, 4) == L"213 " && response.size() > 4) {
				if (CServerCapabilities::GetCapability(currentServer_, size_command) == unknown) {
					CServerCapabilities::SetCapability(currentServer_, size_command, yes);
				}
				std::wstring str = response.substr(4);
				int64_t size = 0;
				for (auto c : str) {
					if (c < '0' || c > '9') {
						break;
					}

					size *= 10;
					size += c - '0';
				}
				remoteFileSize_ = size;
			}
			else {
				log(logmsg::debug_info, L"Invalid SIZE reply");
			}
		}
		break;
	case filetransfer_mdtm:
		opState = filetransfer_resumetest;
		if (response.substr(0, 4) == L"213 " && response.size() > 16) {
			remoteFileTime_ = fz::datetime(response.substr(4), fz::datetime::utc);
			if (!remoteFileTime_.empty()) {
				remoteFileTime_ += fz::duration::from_minutes(currentServer_.GetTimezoneOffset());
			}
		}

		{
			int res = controlSocket_.CheckOverwriteFile();
			if (res != FZ_REPLY_OK) {
				return res;
			}
		}

		break;
	case filetransfer_mfmt:
		return FZ_REPLY_OK;
	default:
		log(logmsg::debug_warning, L"Unknown op state");
		return FZ_REPLY_INTERNALERROR;
	}

	return FZ_REPLY_CONTINUE;
}

int CFtpFileTransferOpData::SubcommandResult(int prevResult, COpData const&)
{
	if (opState == filetransfer_waitcwd) {
		if (prevResult == FZ_REPLY_OK) {
			CDirentry entry;
			bool dirDidExist;
			bool matchedCase;
			bool found = engine_.GetDirectoryCache().LookupFile(entry, currentServer_, tryAbsolutePath_ ? remotePath_ : currentPath_, remoteFile_, dirDidExist, matchedCase);
			if (!found) {
				if (!dirDidExist) {
					opState = filetransfer_waitlist;
				}
				else if (download() && options_.get_int(OPTION_PRESERVE_TIMESTAMPS) && CServerCapabilities::GetCapability(currentServer_, mdtm_command) == yes) {
					opState = filetransfer_mdtm;
				}
				else {
					opState = filetransfer_resumetest;
				}
			}
			else {
				if (entry.is_unsure()) {
					opState = filetransfer_waitlist;
				}
				else {
					if (matchedCase) {
						remoteFileSize_ = entry.size;
						if (entry.has_date()) {
							remoteFileTime_ = entry.time;
						}

						if (download() &&
							!entry.has_time() &&
							options_.get_int(OPTION_PRESERVE_TIMESTAMPS) &&
							CServerCapabilities::GetCapability(currentServer_, mdtm_command) == yes)
						{
							opState = filetransfer_mdtm;
						}
						else {
							opState = filetransfer_resumetest;
						}
					}
					else {
						opState = filetransfer_size;
					}
				}
			}
			if (opState == filetransfer_waitlist) {
				controlSocket_.List(CServerPath(), L"", LIST_FLAG_REFRESH);
				return FZ_REPLY_CONTINUE;
			}
			else if (opState == filetransfer_resumetest) {
				int res = controlSocket_.CheckOverwriteFile();
				if (res != FZ_REPLY_OK) {
					return res;
				}
			}
		}
		else {
			tryAbsolutePath_ = true;
			opState = filetransfer_size;
		}
	}
	else if (opState == filetransfer_waitlist) {
		if (prevResult == FZ_REPLY_OK) {
			CDirentry entry;
			bool dirDidExist;
			bool matchedCase;
			bool found = engine_.GetDirectoryCache().LookupFile(entry, currentServer_, tryAbsolutePath_ ? remotePath_ : currentPath_, remoteFile_, dirDidExist, matchedCase);
			if (!found) {
				if (!dirDidExist) {
					opState = filetransfer_size;
				}
				else if (download() &&
					options_.get_int(OPTION_PRESERVE_TIMESTAMPS) &&
					CServerCapabilities::GetCapability(currentServer_, mdtm_command) == yes)
				{
					opState = filetransfer_mdtm;
				}
				else {
					opState = filetransfer_resumetest;
				}
			}
			else {
				if (matchedCase && !entry.is_unsure()) {
					remoteFileSize_ = entry.size;
					if (entry.has_date()) {
						remoteFileTime_ = entry.time;
					}

					if (download() &&
						!entry.has_time() &&
						options_.get_int(OPTION_PRESERVE_TIMESTAMPS) &&
						CServerCapabilities::GetCapability(currentServer_, mdtm_command) == yes)
					{
						opState = filetransfer_mdtm;
					}
					else {
						opState = filetransfer_resumetest;
					}
				}
				else {
					opState = filetransfer_size;
				}
			}

			if (opState == filetransfer_resumetest) {
				int res = controlSocket_.CheckOverwriteFile();
				if (res != FZ_REPLY_OK) {
					return res;
				}
			}
		}
		else {
			opState = filetransfer_size;
		}
	}
	else if (opState == filetransfer_waittransfer) {
		if (prevResult == FZ_REPLY_OK && options_.get_int(OPTION_PRESERVE_TIMESTAMPS)) {
			if (!download() &&
				CServerCapabilities::GetCapability(currentServer_, mfmt_command) == yes)
			{
				localFileTime_ = reader_factory_.mtime();
				if (!localFileTime_.empty()) {
					opState = filetransfer_mfmt;
					return FZ_REPLY_CONTINUE;
				}
			}
			else if (download() && !remoteFileTime_.empty()) {
				if (!writer_factory_->set_mtime(remoteFileTime_)) {
					log(logmsg::debug_warning, L"Could not set modification time");
				}
			}
		}
		return prevResult;
	}
	else if (opState == filetransfer_waitresumetest) {
		if (prevResult != FZ_REPLY_OK) {
			if (transferEndReason == TransferEndReason::failed_resumetest) {
				if (localFileSize_ > (1ll << 32)) {
					CServerCapabilities::SetCapability(currentServer_, resume4GBbug, yes);
					log(logmsg::error, _("Server does not support resume of files > 4GB."));
				}
				else {
					CServerCapabilities::SetCapability(currentServer_, resume2GBbug, yes);
					log(logmsg::error, _("Server does not support resume of files > 2GB."));
				}

				prevResult |= FZ_REPLY_CRITICALERROR;
			}
			return prevResult;
		}
		if (localFileSize_ > (1ll << 32)) {
			CServerCapabilities::SetCapability(currentServer_, resume4GBbug, no);
		}
		else {
			CServerCapabilities::SetCapability(currentServer_, resume2GBbug, no);
		}

		opState = filetransfer_transfer;
	}

	return FZ_REPLY_CONTINUE;
}
