#include "../filezilla.h"

#include "../directorycache.h"
#include "file_transfer.h"

#include <libfilezilla/format.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/process.hpp>

namespace {
enum FileTransferStates
{
	filetransfer_init,
	filetransfer_checkfileexists,
	filetransfer_waitfileexists,
	filetransfer_delete,
	filetransfer_transfer
};
}

CStorjFileTransferOpData::~CStorjFileTransferOpData()
{
	remove_handler();
}

int CStorjFileTransferOpData::Send()
{
	switch (opState) {
	case filetransfer_init:
	{
		if (!remotePath_.SegmentCount()) {
			if (!download()) {
				log(logmsg::error, _("You cannot upload files into the root directory."));
			}
			return FZ_REPLY_CRITICALERROR | FZ_REPLY_NOTSUPPORTED;
		}

		if (download()) {
			std::wstring filename = remotePath_.FormatFilename(remoteFile_);
			log(logmsg::status, _("Starting download of %s"), filename);
		}
		else {
			log(logmsg::status, _("Starting upload of %s"), localName_);
		}

		remotePath_.SetType(currentServer_.GetType());

		bool needs_listing = false;

		// Get information about remote file
		CDirentry entry;
		bool dirDidExist;
		bool matchedCase;
		bool found = engine_.GetDirectoryCache().LookupFile(entry, currentServer_, remotePath_, remoteFile_, dirDidExist, matchedCase);
		if (found) {
			if (entry.is_unsure()) {
				needs_listing = true;
			}
			else {
				if (matchedCase) {
					remoteFileSize_ = entry.size;
					if (entry.has_date()) {
						remoteFileTime_ = entry.time;
					}
				}
			}
		}
		else {
			if (!dirDidExist) {
				needs_listing = true;
			}
		}

		if (needs_listing) {
			controlSocket_.List(remotePath_, L"", LIST_FLAG_REFRESH);
			return FZ_REPLY_CONTINUE;
		}

		opState = filetransfer_checkfileexists;
		return FZ_REPLY_CONTINUE;
	}
	case filetransfer_checkfileexists:
		{
			int res = controlSocket_.CheckOverwriteFile();
			if (res != FZ_REPLY_OK) {
				opState = filetransfer_waitfileexists;
				return res;
			}

			opState = filetransfer_transfer;
		}
		return FZ_REPLY_CONTINUE;

	case filetransfer_waitfileexists:
//		if (!download() && !fileId_.empty()) {
//			controlSocket_.Delete(remotePath_, std::vector<std::wstring>{remoteFile_});
//			opState = filetransfer_delete;
//		}
//		else {
			opState = filetransfer_transfer;
//		}
		return FZ_REPLY_CONTINUE;
	case filetransfer_transfer:
		{
		    uint64_t offset{};
			if (download()) {
				writer_ = controlSocket_.OpenWriter(writer_factory_, offset, true);
				if (!writer_) {
					return FZ_REPLY_CRITICALERROR;
				}
			}
			else {
				reader_ = reader_factory_->open(*controlSocket_.buffer_pool_, offset, fz::aio_base::nosize, controlSocket_.max_buffer_count());
				if (!reader_) {
					return FZ_REPLY_CRITICALERROR;
				}
			}
			auto info = controlSocket_.buffer_pool_->shared_memory_info();
#ifdef FZ_WINDOWS
			HANDLE target;
			if (!DuplicateHandle(GetCurrentProcess(), std::get<0>(info), controlSocket_.process_->handle(), &target, 0, false, DUPLICATE_SAME_ACCESS)) {
				DWORD error = GetLastError();
				log(logmsg::debug_warning, L"DuplicateHandle failed with %u", error);
				controlSocket_.ResetOperation(FZ_REPLY_ERROR);
				return FZ_REPLY_ERROR;
			}
#endif
			base_address_ = std::get<1>(info);

			if (download()) {
				engine_.transfer_status_.Init(remoteFileSize_, 0, false);
			}
			else {
				engine_.transfer_status_.Init(localFileSize_, 0, false);
			}

			engine_.transfer_status_.SetStartTime();
			transferInitiated_ = true;

			std::wstring cmd;
			if (download()) {
				cmd = L"get " + controlSocket_.QuoteFilename(remotePath_.FormatFilename(remoteFile_)) + L" " + controlSocket_.QuoteFilename(localName_);
			}
			else {
				cmd = L"put " + controlSocket_.QuoteFilename(localName_) + L" " + controlSocket_.QuoteFilename(remotePath_.FormatFilename(remoteFile_));
			}
			controlSocket_.log_raw(logmsg::command, cmd);
			controlSocket_.AddToStream(cmd);

#ifdef FZ_WINDOWS
			controlSocket_.AddToStream(fz::sprintf(" %u %u %u\n", reinterpret_cast<uintptr_t>(target), std::get<2>(info), offset));
#else
			controlSocket_.AddToStream(fz::sprintf(" %d %u %u\n", std::get<0>(info), std::get<2>(info), offset));
#endif

			return FZ_REPLY_WOULDBLOCK;
		}
	}

	log(logmsg::debug_warning, L"Unknown opState in CStorjFileTransferOpData::Send()");
	return FZ_REPLY_INTERNALERROR;
}

int CStorjFileTransferOpData::ParseResponse()
{
	if (opState == filetransfer_transfer) {
		return controlSocket_.result_;
	}

	log(logmsg::debug_warning, L"CStorjFileTransferOpData::ParseResponse called at improper time: %d", opState);
	return FZ_REPLY_INTERNALERROR;
}

int CStorjFileTransferOpData::SubcommandResult(int prevResult, COpData const&)
{
	switch (opState) {
	case filetransfer_init:
		if (prevResult == FZ_REPLY_OK) {
			// Get information about remote file
			CDirentry entry;
			bool dirDidExist;
			bool matchedCase;
			bool found = engine_.GetDirectoryCache().LookupFile(entry, currentServer_, remotePath_, remoteFile_, dirDidExist, matchedCase);
			if (found) {
				if (matchedCase) {
					remoteFileSize_ = entry.size;
					if (entry.has_date()) {
						remoteFileTime_ = entry.time;
					}
				}
			}
		}

		opState = filetransfer_checkfileexists;
		return FZ_REPLY_CONTINUE;
	case filetransfer_delete:
		opState = filetransfer_transfer;
		return FZ_REPLY_CONTINUE;
	}

	log(logmsg::debug_warning, L"Unknown opState in CStorjFileTransferOpData::SubcommandResult()");
	return FZ_REPLY_INTERNALERROR;
}

void CStorjFileTransferOpData::OnNextBufferRequested(uint64_t processed)
{
	if (reader_) {
		fz::aio_result r;
		std::tie(r, buffer_) = reader_->get_buffer(*this);
		if (r == fz::aio_result::wait) {
			return;
		}
		if (r == fz::aio_result::error) {
			controlSocket_.AddToStream("--1\n");
			return;
		}
		if (buffer_->size()) {
			controlSocket_.AddToStream(fz::sprintf("-%d %d\n", buffer_->get() - base_address_, buffer_->size()));
		}
		else {
			controlSocket_.AddToStream(fz::sprintf("-0\n"));
		}
	}
	else if (writer_) {
		controlSocket_.RecordActivity(activity_logger::recv, processed);
		buffer_->resize(processed);
		auto r = writer_->add_buffer(std::move(buffer_), *this);
		if (r == fz::aio_result::ok) {
			buffer_ = controlSocket_.buffer_pool_->get_buffer(*this);
			if (!buffer_) {
				r = fz::aio_result::wait;
			}
		}
		if (r == fz::aio_result::wait) {
			return;
		}

		if (r == fz::aio_result::error) {
			controlSocket_.AddToStream("--1\n");
			return;
		}

		controlSocket_.AddToStream(fz::sprintf("-%d %d\n", buffer_->get() - base_address_, buffer_->capacity()));
	}
	else {
		controlSocket_.AddToStream("--1\n");
		return;
	}
}

void CStorjFileTransferOpData::OnFinalizeRequested(uint64_t lastWrite)
{
	finalizing_ = true;
	buffer_->resize(lastWrite);
	auto r = writer_->add_buffer(std::move(buffer_), *this);
	if (r == fz::aio_result::ok) {
		r = writer_->finalize(*this);
	}
	if (r == fz::aio_result::wait) {
		return;
	}
	else if (r == fz::aio_result::ok) {
		controlSocket_.AddToStream(fz::sprintf("-1\n"));
	}
	else {
		controlSocket_.AddToStream(fz::sprintf("-0\n"));
	}
}

void CStorjFileTransferOpData::operator()(fz::event_base const& ev)
{
	fz::dispatch<fz::aio_buffer_event>(ev, this,
	    &CStorjFileTransferOpData::OnBufferAvailability
	);
}

void CStorjFileTransferOpData::OnBufferAvailability(fz::aio_waitable const* w)
{
	if (w == reader_.get()) {
		OnNextBufferRequested(0);
	}
	else if (w == writer_.get()) {
		if (finalizing_) {
			OnFinalizeRequested(0);
		}
		else {
			OnNextBufferRequested(0);
		}
	}
}
