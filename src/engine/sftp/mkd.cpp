#include "../filezilla.h"

#include "../directorycache.h"
#include "mkd.h"

/* Directory creation works like this: First find a parent directory into
 * which we can CWD, then create the subdirs one by one. If either part
 * fails, try MKD with the full path directly.
 */

int CSftpMkdirOpData::Send()
{
	if (!sftp_) {
		return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
	}

	if (!opLock_) {
		opLock_ = controlSocket_.Lock(locking_reason::mkdir, path_);
	}
	if (opLock_.waiting()) {
		return FZ_REPLY_WOULDBLOCK;
	}

	if (controlSocket_.operations_.size() == 1) {
		log(logmsg::status, _("Creating directory '%s'..."), path_.GetPath());
	}

	if (!currentPath_.empty()) {
		// Unless the server is broken, a directory already exists if current directory is a subdir of it.
		if (currentPath_ == path_ || currentPath_.IsSubdirOf(path_, false)) {
			return FZ_REPLY_OK;
		}

		if (currentPath_.IsParentOf(path_, false)) {
			commonParent_ = currentPath_;
		}
		else {
			commonParent_ = path_.GetCommonParent(currentPath_);
		}
	}

	auto p = path_;
	while (p.HasParent() && p != commonParent_) {
		paths_.push_back(p);
		p.MakeParent();
	}

	fz::ssh::sftp::attributes attrs;
	for (auto it = paths_.rbegin(); it != paths_.rend(); ++it) {
		auto p = controlSocket_.ConvToServer(it->GetPath());
		if (p.empty()) {
			return FZ_REPLY_ERROR;
		}
		sftp_->mkdir(this, p, attrs);
	}

	return FZ_REPLY_WOULDBLOCK;
}

CSftpOpData::continuation CSftpMkdirOpData::do_process_status(fz::ssh::sftp::status_code code, std::wstring_view msg)
{
	bool success = code == fz::ssh::sftp::status_code::SSH_FX_OK;

	if (success) {
		auto p = paths_.back();
		if (p.HasParent()) {
			engine_.GetDirectoryCache().UpdateFile(currentServer_, p.GetParent(), p.GetLastSegment(), true, CDirectoryCache::dir);
			controlSocket_.SendDirectoryListingNotification(p.GetParent(), false);
		}
	}
	if (paths_.size() == 1) {
		if (success) {
			log(fz::logmsg::status, _("Directory creation succeeded."), msg);
			trigger_reset(FZ_REPLY_OK);
		}
		else {
			log(fz::logmsg::error, _("Could not create directory: %s"), msg);
			trigger_reset(FZ_REPLY_ERROR);
		}
	}

	paths_.pop_back();
	return continuation::next;
}
