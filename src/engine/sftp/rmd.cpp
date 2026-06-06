#include "../filezilla.h"

#include "../directorycache.h"
#include "../pathcache.h"
#include "rmd.h"

int CSftpRemoveDirOpData::Send()
{
	if (!sftp_) {
		return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
	}

	CServerPath fullPath = engine_.GetPathCache().Lookup(currentServer_, path_, subDir_);
	if (fullPath.empty()) {
		fullPath = path_;

		if (!fullPath.AddSegment(subDir_)) {
			log(logmsg::error, _("Path cannot be constructed for directory %s and subdir %s"), path_.GetPath(), subDir_);
			return FZ_REPLY_ERROR;
		}
	}

	engine_.GetDirectoryCache().InvalidateFile(currentServer_, path_, subDir_);

	engine_.GetPathCache().InvalidatePath(currentServer_, path_, subDir_);

	engine_.InvalidateCurrentWorkingDirs(fullPath);
	sftp_->rmdir(this, controlSocket_.ConvToServer(fullPath.GetPath()));
	return FZ_REPLY_WOULDBLOCK;
}

CSftpOpData::continuation CSftpRemoveDirOpData::do_process_status(fz::ssh::sftp::status_code code, std::wstring_view msg)
{
	if (code != fz::ssh::sftp::status_code::SSH_FX_OK) {
		log(fz::logmsg::error, _("Could not rename file: %s"), msg);
		trigger_reset(FZ_REPLY_ERROR);
	}
	else {
		log(fz::logmsg::status, _("Directory deletion succeeded."));
		engine_.GetDirectoryCache().RemoveDir(currentServer_, path_, subDir_, engine_.GetPathCache().Lookup(currentServer_, path_, subDir_));
		controlSocket_.SendDirectoryListingNotification(path_, false);
		trigger_reset(FZ_REPLY_OK);
	}

	return continuation::next;
}
