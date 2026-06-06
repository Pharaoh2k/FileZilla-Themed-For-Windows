#include "../filezilla.h"

#include "../directorycache.h"
#include "mkd.h"

namespace {
enum mkdStates
{
	mkd_init = 0,
	mkd_mkbucket,
	mkd_put
};
}

int CStorjMkdirOpData::Send()
{
	switch (opState) {
	case mkd_init:
		if (path_.SegmentCount() < 1) {
			log(logmsg::error, _("Invalid path"));
			return FZ_REPLY_CRITICALERROR;
		}

		if (controlSocket_.operations_.size() == 1) {
			log(logmsg::status, _("Creating directory '%s'..."), path_.GetPath());
		}

		opState = mkd_mkbucket;
		return FZ_REPLY_CONTINUE;
	case mkd_mkbucket:
		return controlSocket_.SendCommand(L"mkbucket " + controlSocket_.QuoteFilename(path_.GetFirstSegment()));
	case mkd_put:
		return controlSocket_.SendCommand(L"mkd " + controlSocket_.QuoteFilename(path_.GetPath()));
	}

	log(logmsg::debug_warning, L"Unknown opState in CStorjMkdirOpData::Send()");
	return FZ_REPLY_INTERNALERROR;
}

int CStorjMkdirOpData::ParseResponse()
{
	switch (opState) {
	case mkd_mkbucket:
		if (controlSocket_.result_ == FZ_REPLY_OK) {
			engine_.GetDirectoryCache().UpdateFile(currentServer_, CServerPath(L"/"), path_.GetFirstSegment(), true, CDirectoryCache::dir);
			controlSocket_.SendDirectoryListingNotification(CServerPath(L"/"), false);
		}

		if (path_.SegmentCount() > 1) {
			opState = mkd_put;
			return FZ_REPLY_CONTINUE;
		}
		else {
			return controlSocket_.result_;
		}
	case mkd_put:
		if (controlSocket_.result_ == FZ_REPLY_OK) {
			CServerPath path = path_;
			while (path.SegmentCount() > 1) {
				CServerPath parent = path.GetParent();
				engine_.GetDirectoryCache().UpdateFile(currentServer_, parent, path.GetLastSegment(), true, CDirectoryCache::dir);
				controlSocket_.SendDirectoryListingNotification(parent, false);
				path = parent;
			}
		}
		return controlSocket_.result_;
	}

	log(logmsg::debug_warning, L"Unknown opState in CStorjMkdirOpData::ParseResponse()");
	return FZ_REPLY_INTERNALERROR;
}
