#include "../filezilla.h"

#include "../directorycache.h"
#include "rmd.h"

#include <assert.h>

namespace {
enum mkdStates
{
	rmd_init = 0,
	rmd_rmbucket,
	rmd_rmdir
};
}

int CStorjRemoveDirOpData::Send()
{
	switch (opState) {
	case rmd_init:
		if (path_.SegmentCount() < 1) {
			log(logmsg::error, _("Invalid path"));
			return FZ_REPLY_CRITICALERROR;
		}
		if (path_.SegmentCount() == 1) {
			opState = rmd_rmbucket;
		}
		else {
			opState = rmd_rmdir;
		}
		return FZ_REPLY_CONTINUE;
	case rmd_rmbucket:
		engine_.GetDirectoryCache().InvalidateFile(currentServer_, CServerPath(L"/"), path_.GetFirstSegment());

		engine_.InvalidateCurrentWorkingDirs(path_);

		return controlSocket_.SendCommand(L"rmbucket " + controlSocket_.QuoteFilename(path_.GetFirstSegment()));
	case rmd_rmdir:
		engine_.GetDirectoryCache().InvalidateFile(currentServer_, path_.GetParent(), path_.GetLastSegment());
		return controlSocket_.SendCommand(L"rmd " + controlSocket_.QuoteFilename(path_.GetPath()));
	}

	log(logmsg::debug_warning, L"Unknown opState in CStorjRemoveDirOpData::Send()");
	return FZ_REPLY_INTERNALERROR;
}

int CStorjRemoveDirOpData::ParseResponse()
{
	switch (opState) {
	case rmd_rmbucket:
		if (controlSocket_.result_ == FZ_REPLY_OK) {
			engine_.GetDirectoryCache().RemoveDir(currentServer_, CServerPath(L"/"), path_.GetFirstSegment(), CServerPath());
			controlSocket_.SendDirectoryListingNotification(CServerPath(L"/"), false);
		}

		return controlSocket_.result_;
	case rmd_rmdir:
		if (controlSocket_.result_ == FZ_REPLY_OK) {
			engine_.GetDirectoryCache().RemoveDir(currentServer_, path_.GetParent(), path_.GetLastSegment(), CServerPath());
			controlSocket_.SendDirectoryListingNotification(path_.GetParent(), false);
		}
		return controlSocket_.result_;
	}

	log(logmsg::debug_warning, L"Unknown opState in CStorjRemoveDirOpData::ParseResponse()");
	return FZ_REPLY_INTERNALERROR;
}
