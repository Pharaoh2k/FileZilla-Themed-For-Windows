#include "../filezilla.h"

#include "../directorycache.h"
#include "../pathcache.h"
#include "rename.h"
using namespace std::literals;

int CSftpRenameOpData::Send()
{
	if (!sftp_) {
		return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
	}

	auto const oldname = command_.GetFromPath().FormatFilename(command_.GetFromFile());
	auto const newname = command_.GetToPath().FormatFilename(command_.GetToFile());

	log(logmsg::status, _("Renaming '%s' to '%s'"), command_.GetFromPath().FormatFilename(command_.GetFromFile()), command_.GetToPath().FormatFilename(command_.GetToFile()));

	engine_.GetDirectoryCache().InvalidateFile(currentServer_, command_.GetFromPath(), command_.GetFromFile());
	engine_.GetDirectoryCache().InvalidateFile(currentServer_, command_.GetToPath(), command_.GetToFile());

	engine_.GetPathCache().InvalidatePath(currentServer_, command_.GetFromPath(), command_.GetFromFile());
	engine_.GetPathCache().InvalidatePath(currentServer_, command_.GetToPath(), command_.GetToFile());

	// Need to invalidate current working directories
	CServerPath path = engine_.GetPathCache().Lookup(currentServer_, command_.GetFromPath(), command_.GetFromFile());
	if (path.empty()) {
		path = command_.GetFromPath();
		path.AddSegment(command_.GetFromFile());
	}
	engine_.InvalidateCurrentWorkingDirs(path);

	sftp_->rename(this, controlSocket_.ConvToServer(oldname), controlSocket_.ConvToServer(newname));
	return FZ_REPLY_WOULDBLOCK;
}

CSftpOpData::continuation CSftpRenameOpData::do_process_status(fz::ssh::sftp::status_code code, std::wstring_view msg)
{
	if (code != fz::ssh::sftp::status_code::SSH_FX_OK) {
		log(fz::logmsg::error, _("Could not rename file or directory: %s"), msg);
		trigger_reset(FZ_REPLY_ERROR);
	}
	else {
		const CServerPath& fromPath = command_.GetFromPath();
		const CServerPath& toPath = command_.GetToPath();

		engine_.GetDirectoryCache().Rename(currentServer_, fromPath, command_.GetFromFile(), toPath, command_.GetToFile());

		controlSocket_.SendDirectoryListingNotification(fromPath, false);
		if (fromPath != toPath) {
			controlSocket_.SendDirectoryListingNotification(toPath, false);
		}

		log(logmsg::status, _("Rename succeeded."));

		trigger_reset(FZ_REPLY_OK);
	}
	return continuation::next;
}
