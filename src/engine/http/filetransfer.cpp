#include "../filezilla.h"

#include "filetransfer.h"

#include <libfilezilla/local_filesys.hpp>

#include <assert.h>
#include <string.h>

namespace {
enum filetransferStates
{
	filetransfer_init = 0,
	filetransfer_transfer,
	filetransfer_waittransfer
};
}

CHttpFileTransferOpData::CHttpFileTransferOpData(CHttpControlSocket & controlSocket, CFileTransferCommand const& cmd)
	: CFileTransferOpData(L"CHttpFileTransferOpData", cmd)
	, CHttpOpData(controlSocket)
{
	rr_.request_.uri_ = fz::uri(fz::to_utf8(currentServer_.Format(ServerFormat::url)) + fz::percent_encode(fz::to_utf8(remotePath_.FormatFilename(remoteFile_)), true));
	rr_.request_.verb_ = "GET";

}

CHttpFileTransferOpData::CHttpFileTransferOpData(CHttpControlSocket & controlSocket, CHttpRequestCommand const& cmd)
	: CFileTransferOpData(L"CHttpFileTransferOpData", CFileTransferCommand(fz::writer_factory_holder(), CServerPath(), std::wstring(), transfer_flags::download))
	, CHttpOpData(controlSocket)
{
	reader_factory_ = cmd.body_;
	writer_factory_ = cmd.output_;
	rr_.request_.uri_ = cmd.uri_;
	rr_.request_.verb_ = cmd.verb_;
	if (cmd.confidential_qs_) {
		rr_.request_.flags_ |= HttpRequest::flag_confidential_querystring;
	}
}


int CHttpFileTransferOpData::Send()
{
	switch (opState) {
	case filetransfer_init:
		if (!download()) {
			return FZ_REPLY_NOTSUPPORTED;
		}

		if (rr_.request_.uri_.empty()) {
			log(logmsg::error, _("Could not create URI for this transfer."));
			return FZ_REPLY_ERROR;
		}

		if (reader_factory_) {
			rr_.request_.body_ = reader_factory_->open(*controlSocket_.buffer_pool_, 0, fz::aio_base::nosize, controlSocket_.max_buffer_count());
			if (!rr_.request_.body_) {
				return FZ_REPLY_CRITICALERROR;
			}
		}

		opState = filetransfer_transfer;
		if (writer_factory_) {
			auto s = writer_factory_.size();
			if (s != fz::aio_base::nosize) {
				localFileSize_ = static_cast<int64_t>(s);
			}

			int res = controlSocket_.CheckOverwriteFile();
			if (res != FZ_REPLY_OK) {
				return res;
			}
		}
		return FZ_REPLY_CONTINUE;
	case filetransfer_transfer:
		if (resume_ && localFileSize_ != 0 && localFileSize_ != fz::aio_base::nosize) {
			rr_.request_.headers_["Range"] = fz::sprintf("bytes=%d-", localFileSize_);
		}

		rr_.set_on_header([this](auto const& srr) { return this->OnHeader(srr); });

		opState = filetransfer_waittransfer;
		controlSocket_.Request(make_simple_rr(&rr_));
		return FZ_REPLY_CONTINUE;
	default:
		break;
	}

	return FZ_REPLY_INTERNALERROR;
}

fz::http::continuation CHttpFileTransferOpData::OnHeader(std::shared_ptr<HttpRequestResponse> const&)
{
	log(logmsg::debug_verbose, L"CHttpFileTransferOpData::OnHeader");

	if (rr_.response_.code_ == 416 && resume_) {
		resume_ = false;
		opState = filetransfer_transfer;
		return fz::http::continuation::error;
	}

	if (rr_.response_.code_ < 200 || rr_.response_.code_ >= 400) {
		return fz::http::continuation::error;
	}

	// Handle any redirects
	if (rr_.response_.code_ >= 300) {

		if (++redirectCount_ >= 6) {
			log(logmsg::error, _("Too many redirects"));
			return fz::http::continuation::error;
		}

		if (rr_.response_.code_ == 305) {
			log(logmsg::error, _("Unsupported redirect"));
			return fz::http::continuation::error;
		}

		fz::uri location = fz::uri(rr_.response_.get_header("Location"));
		if (!location.empty()) {
			location.resolve(rr_.request_.uri_);
		}

		if (location.scheme_.empty() || location.host_.empty() || !location.is_absolute()) {
			log(logmsg::error, _("Redirection to invalid or unsupported URI: %s"), location.to_string());
			return fz::http::continuation::error;
		}

		ServerProtocol protocol = CServer::GetProtocolFromPrefix(fz::to_wstring_from_utf8(location.scheme_));
		if (protocol != HTTP && protocol != HTTPS) {
			log(logmsg::error, _("Redirection to invalid or unsupported address: %s"), location.to_string());
			return fz::http::continuation::error;
		}

		// International domain names
		std::wstring host = fz::to_wstring_from_utf8(location.host_);
		if (host.empty()) {
			log(logmsg::error, _("Invalid hostname: %s"), location.to_string());
			return fz::http::continuation::error;
		}

		rr_.request_.uri_ = std::move(location);
		controlSocket_.Request(make_simple_rr(&rr_));
		return fz::http::continuation::done;
	}

	// Check if the server disallowed resume
	if (resume_ && rr_.response_.code_ != 206) {
		resume_ = false;
	}

	if (writer_factory_) {
		auto writer = controlSocket_.OpenWriter(writer_factory_, resume_ ? localFileSize_ : 0, true);
		if (!writer) {
			return fz::http::continuation::error;
		}
		rr_.response_.writer_ = std::move(writer);
	}

	int64_t totalSize = fz::to_integral<int64_t>(rr_.response_.get_header("Content-Length"), -1);
	if (totalSize == -1) {
		if (remoteFileSize_ != -1) {
			totalSize = remoteFileSize_;
		}
	}

	if (engine_.transfer_status_.empty()) {
		engine_.transfer_status_.Init(totalSize, resume_ ? localFileSize_ : 0, false);
		engine_.transfer_status_.SetStartTime();
	}

	return fz::http::continuation::next;
}

int CHttpFileTransferOpData::SubcommandResult(int prevResult, COpData const&)
{
	if (opState == filetransfer_transfer) {
		return FZ_REPLY_CONTINUE;
	}

	return prevResult;
}
