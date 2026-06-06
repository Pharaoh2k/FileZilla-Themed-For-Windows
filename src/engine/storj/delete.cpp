#include "../filezilla.h"

#include "../directorycache.h"
#include "delete.h"

namespace {
enum DeleteStates
{
	delete_init,
	delete_resolve,
	delete_delete
};
}

int CStorjDeleteOpData::Send()
{
	switch (opState) {
	case delete_init:
		if (files_.empty()) {
			return FZ_REPLY_CRITICALERROR;
		}

		opState = delete_delete;
		return FZ_REPLY_CONTINUE;
	case delete_delete:
		if (files_.empty()) {
			return deleteFailed_ ? FZ_REPLY_ERROR : FZ_REPLY_OK;
		}

		std::wstring const& file = files_.back();

		if (time_.empty()) {
			time_ = fz::datetime::now();
		}

		engine_.GetDirectoryCache().InvalidateFile(currentServer_, path_, file);

		return controlSocket_.SendCommand(L"rm " + controlSocket_.QuoteFilename(path_.FormatFilename(file)));
	}

	log(logmsg::debug_warning, L"Unknown opState in CStorjDeleteOpData::Send()");
	return FZ_REPLY_INTERNALERROR;
}

int CStorjDeleteOpData::ParseResponse()
{
	if (controlSocket_.result_ != FZ_REPLY_OK) {
		deleteFailed_ = true;
	}
	else {
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

	files_.pop_back();

	if (!files_.empty()) {
		return FZ_REPLY_CONTINUE;
	}

	return deleteFailed_ ? FZ_REPLY_ERROR : FZ_REPLY_OK;
}
