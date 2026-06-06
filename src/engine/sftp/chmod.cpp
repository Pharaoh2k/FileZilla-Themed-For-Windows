#include "../filezilla.h"

#include "chmod.h"
#include "../directorycache.h"

#include "../../include/posix_chmod.h"

namespace {
enum state {
	getstat,
	setstat
};
}

int CSftpChmodOpData::Send()
{
	if (!sftp_) {
		return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
	}

	log(logmsg::status, _("Setting permissions of '%s' to '%s'"), command_.GetPath().FormatFilename(command_.GetFile()), command_.GetPermission());

	engine_.GetDirectoryCache().UpdateFile(currentServer_, command_.GetPath(), command_.GetFile(), false, CDirectoryCache::unknown);

	sftp_->stat(this, controlSocket_.ConvToServer(command_.GetPath().FormatFilename(command_.GetFile())));

	return FZ_REPLY_WOULDBLOCK;
}

CSftpOpData::continuation CSftpChmodOpData::do_process_status(fz::ssh::sftp::status_code code, std::wstring_view msg)
{
	if (opState == getstat) {
		log(logmsg::error, _("Could not get file attributes: %s"), msg);
		trigger_reset(FZ_REPLY_CRITICALERROR);
	}
	else {
		if (code == fz::ssh::sftp::status_code::SSH_FX_OK) {
			trigger_reset(FZ_REPLY_OK);
		}
		else {
			log(logmsg::error, _("Could not set file attributes: %s"), msg);
			trigger_reset(FZ_REPLY_CRITICALERROR);
		}
	}
	return continuation::next;
}

CSftpOpData::continuation CSftpChmodOpData::process_attributes(fz::ssh::sftp::attributes & attrs)
{
	if (!attrs.perms_) {
		log(logmsg::error, _("Server did not return old permissions to update"));
		trigger_reset(FZ_REPLY_CRITICALERROR);
		return continuation::next;
	}

	fz::ssh::sftp::attributes newAttrs;
	newAttrs.perms_ = *attrs.perms_ & ~07777;

	auto perms = parse_permissions(fz::to_utf8(command_.GetPermission()));
	if (!perms) {
		log(logmsg::error, _("Could not parse new permissions"));
		trigger_reset(FZ_REPLY_SYNTAXERROR);
		return continuation::next;
	}

	*newAttrs.perms_ |= static_cast<std::underlying_type_t<posix_permissions>>(*perms);
	sftp_->setstat(this, controlSocket_.ConvToServer(command_.GetPath().FormatFilename(command_.GetFile())), newAttrs);

	opState = setstat;

	return continuation::next;
}
