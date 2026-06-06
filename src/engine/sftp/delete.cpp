#include "../filezilla.h"

#include "delete.h"
#include "../directorycache.h"

namespace {
size_t constexpr pipeline = 100;
}

int CSftpDeleteOpData::Send()
{
	if (!sftp_) {
		return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
	}

	if (time_.empty()) {
		time_ = fz::datetime::now();
	}

	for (size_t i = 0; i < std::min(pipeline, files_.size()); ++i) {
		std::wstring const& file = files_[files_.size() - 1 - i];

		std::wstring filename = path_.FormatFilename(file);
		if (filename.empty()) {
			log(logmsg::error, _("Filename cannot be constructed for directory %s and filename %s"), path_.GetPath(), file);
			return FZ_REPLY_ERROR;
		}

		engine_.GetDirectoryCache().InvalidateFile(currentServer_, path_, file);
		sftp_->remove(this, controlSocket_.ConvToServer(filename));
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CSftpDeleteOpData::Reset(int result)
{
	if (needSendListing_ && !(result & FZ_REPLY_DISCONNECTED)) {
		controlSocket_.SendDirectoryListingNotification(path_, false);
	}
	return result;
}

CSftpOpData::continuation CSftpDeleteOpData::do_process_status(fz::ssh::sftp::status_code code, std::wstring_view msg)
{
	if (code == fz::ssh::sftp::status_code::SSH_FX_OK) {
		std::wstring const& file = files_.back();
		engine_.GetDirectoryCache().RemoveFile(currentServer_, path_, file);

		auto const now = fz::datetime::now();
		if (!time_.empty() && (now - time_).get_seconds() >= 1) {
			controlSocket_.SendDirectoryListingNotification(path_, false);
			time_ = now;
			needSendListing_ = false;
		}
		else {
			needSendListing_ = true;
		}
	}
	else {
		log(logmsg::error, _("Could not delete %s: %s"), files_.back(), msg);
		deleteFailed_ = true;
	}

	files_.pop_back();

	if (files_.size() >= pipeline) {
		std::wstring const& file = files_[files_.size() - pipeline];

		std::wstring filename = path_.FormatFilename(file);
		if (filename.empty()) {
			log(logmsg::error, _("Filename cannot be constructed for directory %s and filename %s"), path_.GetPath(), file);
			trigger_reset(FZ_REPLY_ERROR);
		}
		else {
			engine_.GetDirectoryCache().InvalidateFile(currentServer_, path_, file);
			sftp_->remove(this, controlSocket_.ConvToServer(filename));
		}
	}
	else if (files_.empty()) {
		if (!deleteFailed_) {
			log(logmsg::status, _("Deletion succeeded."));
		}
		trigger_reset(deleteFailed_ ? FZ_REPLY_ERROR : FZ_REPLY_OK);
	}

	return continuation::next;
}
