#include "../filezilla.h"

#include "cwd.h"
#include "../pathcache.h"

using namespace std::literals;

namespace {
enum cwdStates
{
	cwd_init = 0,
	cwd_pwd,
	cwd_cwd,
	cwd_cwd_subdir,
	cwd_stat,
	cwd_stat_subdir
};
}

int CSftpChangeDirOpData::Send()
{
	if (!sftp_) {
		return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
	}

	switch (opState)
	{
	case cwd_init:
		path_.SetType(currentServer_.GetType());

		if (path_.empty()) {
			if (currentPath_.empty()) {
				opState = cwd_pwd;
				sftp_->realpath(this, "."sv);
				return FZ_REPLY_WOULDBLOCK;
			}
			else {
				return FZ_REPLY_OK;
			}
		}
		else {
			if (!subDir_.empty()) {
				// Check if the target is in cache already
				target_ = engine_.GetPathCache().Lookup(currentServer_, path_, subDir_);
				if (!target_.empty()) {
					if (currentPath_ == target_) {
						return FZ_REPLY_OK;
					}

					path_ = target_;
					subDir_.clear();
					opState = cwd_cwd;
				}
				else {
					// Target unknown, check for the parent's target
					target_ = engine_.GetPathCache().Lookup(currentServer_, path_, L"");
					if (currentPath_ == path_ || (!target_.empty() && target_ == currentPath_)) {
						target_ = currentPath_;
						target_.ChangePath(subDir_);
						opState = cwd_cwd_subdir;
					}
					else {
						opState = cwd_cwd;
					}
				}
			}
			else {
				target_ = engine_.GetPathCache().Lookup(currentServer_, path_, L"");
				if (currentPath_ == path_ || (!target_.empty() && target_ == currentPath_)) {
					return FZ_REPLY_OK;
				}
				opState = cwd_cwd;
			}
		}
		return FZ_REPLY_CONTINUE;
	case cwd_cwd:
		if (tryMkdOnFail_ && !opLock_) {
			opLock_ = controlSocket_.Lock(locking_reason::mkdir, path_);
		}
		if (opLock_.waiting()) {
			// Some other engine is already creating this directory or
			// performing an action that will lead to its creation
			tryMkdOnFail_ = false;
			return FZ_REPLY_WOULDBLOCK;
		}
		sftp_->realpath(this, controlSocket_.ConvToServer(path_.GetPath()));
		currentPath_.clear();
		break;
	case cwd_cwd_subdir:
		if (subDir_.empty()) {
			return FZ_REPLY_INTERNALERROR;
		}
		else {
			sftp_->realpath(this, controlSocket_.ConvToServer(target_.GetPath()));
		}
		currentPath_.clear();
		break;
	case cwd_stat:
	case cwd_stat_subdir:
		sftp_->stat(this, controlSocket_.ConvToServer(target_.GetPath()));
		break;
	}

	return FZ_REPLY_WOULDBLOCK;
}

CSftpOpData::continuation CSftpChangeDirOpData::process_name(fz::ssh::sftp::entry & e, bool)
{
	std::wstring name = controlSocket_.ConvToLocal(e.name_.data(), e.name_.size());
	if (name.empty()) {
		trigger_reset(FZ_REPLY_ERROR);
		return continuation::next;
	}
	log(logmsg::debug_info, L"Canonicalized path: %s"sv, name);

	target_ = controlSocket_.ParsePath(name);
	if (target_.empty()) {
		trigger_reset(FZ_REPLY_ERROR);
		return continuation::next;
	}
	switch (opState) {
	case cwd_pwd:
		opState = cwd_stat;
		sftp_->stat(this, e.name_);
		break;
	case cwd_cwd:
		opState = cwd_stat;
		sftp_->stat(this, e.name_);
		break;
	case cwd_cwd_subdir:
		opState = cwd_stat_subdir;
		sftp_->stat(this, e.name_);
		break;
	default:
		trigger_reset(FZ_REPLY_INTERNALERROR);
		break;
	}

	return continuation::next;
}

CSftpOpData::continuation CSftpChangeDirOpData::process_attributes(fz::ssh::sftp::attributes & attrs)
{
	if (!attrs.perms_ || !attrs.is_directory()) {
		if (attrs.perms_) {
			log(fz::logmsg::debug_warning, "Mode bits of permissions in received attributes: 0%o", *attrs.perms_ & 0170000);
		}
		else {
			log(fz::logmsg::debug_warning, "Received attributes do not have SSH_FILEXFER_ATTR_PERMISSIONS");
		}
		log(fz::logmsg::error, _("Not a directory"));
		if (link_discovery_) {
			log(logmsg::debug_info, L"Symlink does not link to a directory, probably a file");
			trigger_reset(FZ_REPLY_LINKNOTDIR);
		}
		else {
			trigger_reset(FZ_REPLY_ERROR);
		}
		return continuation::next;
	}

	switch (opState) {
	case cwd_stat:
		currentPath_ = target_;
		if (!path_.empty()) {
			engine_.GetPathCache().Store(currentServer_, currentPath_, path_);
		}
		if (subDir_.empty()) {
			trigger_reset(FZ_REPLY_OK);
		}
		else {
			path_ = currentPath_;
			target_ = path_;
			target_.ChangePath(subDir_);
			sftp_->realpath(this, controlSocket_.ConvToServer(target_.GetPath()));
			currentPath_.clear();
			opState = cwd_cwd_subdir;
		}
		break;
	case cwd_stat_subdir:
		currentPath_ = target_;
		engine_.GetPathCache().Store(currentServer_, currentPath_, path_, subDir_);
		trigger_reset(FZ_REPLY_OK);
		break;
	default:
		trigger_reset(FZ_REPLY_INTERNALERROR);
		break;
	}

	return continuation::next;
}

CSftpOpData::continuation CSftpChangeDirOpData::do_process_status(fz::ssh::sftp::status_code /*code*/, std::wstring_view msg)
{
	switch (opState) {
	case cwd_pwd:
		log(logmsg::error, _("Could not get initial directory: %s"), msg);
		trigger_reset(FZ_REPLY_ERROR);
		break;
	case cwd_stat:
	case cwd_stat_subdir:
	case cwd_cwd:
		if (tryMkdOnFail_) {
			tryMkdOnFail_ = false;
			controlSocket_.Mkdir(path_);
			trigger_next();
		}
		else {
			log(logmsg::error, _("Could not get directory information: %s"), msg);
			trigger_reset(FZ_REPLY_ERROR);
		}
		break;
	case cwd_cwd_subdir:
		if (link_discovery_) {
			log(logmsg::debug_info, L"Symlink does not link to a directory, probably a file");
			trigger_reset(FZ_REPLY_LINKNOTDIR);
		}
		else {
			trigger_reset(FZ_REPLY_ERROR);
		}
		break;
	}
	return continuation::next;
}
