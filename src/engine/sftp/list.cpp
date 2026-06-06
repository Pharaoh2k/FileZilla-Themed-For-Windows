#include "../filezilla.h"

#include "../directorycache.h"
#include "list.h"

#include <assert.h>

namespace {
enum listStates
{
	list_init = 0,
	list_waitcwd,
	list_waitlock,
	list_list
};
}

int CSftpListOpData::Send()
{
	if (!sftp_) {
		return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
	}

	if (opState == list_init) {
		path_.SetType(currentServer_.GetType());
		refresh_ = (flags_ & LIST_FLAG_REFRESH) != 0;
		fallback_to_current_ = !path_.empty() && (flags_ & LIST_FLAG_FALLBACK_CURRENT) != 0;

		auto newPath = CServerPath::GetChanged(currentPath_, path_, subDir_);
		if (newPath.empty()) {
			log(logmsg::status, _("Retrieving directory listing..."));
		}
		else {
			log(logmsg::status, _("Retrieving directory listing of \"%s\"..."), newPath.GetPath());
		}

		controlSocket_.ChangeDir(path_, subDir_, (flags_ & LIST_FLAG_LINK) != 0);
		opState = list_waitcwd;
		return FZ_REPLY_CONTINUE;
	}
	else if (opState == list_waitlock) {
		assert(subDir_.empty()); // We did do ChangeDir before trying to lock

		// Check if we can use already existing listing
		CDirectoryListing listing;
		bool is_outdated = false;
		bool found = engine_.GetDirectoryCache().Lookup(listing, currentServer_, path_, false, is_outdated);
		if (found && !is_outdated &&
			(!refresh_ || (opLock_ && listing.m_firstListTime >= time_before_locking_)))
		{
			controlSocket_.SendDirectoryListingNotification(listing.path, false);
			return FZ_REPLY_OK;
		}

		if (!opLock_) {
			opLock_ = controlSocket_.Lock(locking_reason::list, currentPath_);
			time_before_locking_ = fz::monotonic_clock::now();
		}
		if (opLock_.waiting()) {
			return FZ_REPLY_WOULDBLOCK;
		}

		opState = list_list;
		return FZ_REPLY_CONTINUE;
	}
	else if (opState == list_list) {
		listing_parser_ = std::make_unique<CDirectoryListingParser>(&controlSocket_, currentServer_, listingEncoding::unknown);
		std::string path = controlSocket_.ConvToServer(currentPath_.GetPath());
		if (path.empty()) {
			return FZ_REPLY_ERROR;
		}
		sftp_->opendir(this, path);
		return FZ_REPLY_WOULDBLOCK;
	}

	log(logmsg::debug_warning, L"Unknown opState in CSftpListOpData::Send()");
	return FZ_REPLY_INTERNALERROR;
}

int CSftpListOpData::SubcommandResult(int prevResult, COpData const&)
{
	if (opState != list_waitcwd) {
		return FZ_REPLY_INTERNALERROR;
	}

	if (prevResult != FZ_REPLY_OK) {
		if (fallback_to_current_) {
			// List current directory instead
			fallback_to_current_ = false;
			path_.clear();
			subDir_.clear();
			controlSocket_.ChangeDir();
			return FZ_REPLY_CONTINUE;
		}
		else {
			return prevResult;
		}
	}

	path_ = currentPath_;
	subDir_.clear();
	opState = list_waitlock;
	return FZ_REPLY_CONTINUE;
}

int CSftpListOpData::Reset(int result)
{
	if (sftp_ && !handle_.empty()) {
		sftp_->close(nullptr, handle_);
	}
	handle_.clear();
	return result;
}

CSftpOpData::continuation CSftpListOpData::process_handle(std::string_view handle)
{
	handle_ = handle;

	// Pipeline requests
	for (size_t i = 0; i < 4; ++i) {
		sftp_->readdir(this, handle_);
	}

	return continuation::next;
}

CSftpOpData::continuation CSftpListOpData::do_process_status(fz::ssh::sftp::status_code code, std::wstring_view msg)
{
	if (code == fz::ssh::sftp::status_code::SSH_FX_EOF) {
		if (!listing_parser_) {
			log(logmsg::debug_warning, L"listing_parser_ is empty");
			trigger_reset(FZ_REPLY_INTERNALERROR);
			return continuation::error;
		}

		directoryListing_ = listing_parser_->Parse(currentPath_);
		engine_.GetDirectoryCache().Store(directoryListing_, currentServer_);
		controlSocket_.SendDirectoryListingNotification(currentPath_, false);
		trigger_reset(FZ_REPLY_OK);
		return continuation::next;
	}
	else if (handle_.empty()) {
		log(logmsg::error, _("Could not open directory: %s"), msg);
	}
	else {
		log(logmsg::error, _("Could not read directory: %s"), msg);
	}

	trigger_reset(FZ_REPLY_ERROR);

	return continuation::next;
}

CSftpOpData::continuation CSftpListOpData::process_name(fz::ssh::sftp::entry & e, bool more)
{
	std::wstring name = controlSocket_.ConvToLocal(e.name_.data(), e.name_.size());
	std::wstring longname = controlSocket_.ConvToLocal(e.longname_.data(), e.longname_.size());

	if (!more) {
		sftp_->readdir(this, handle_);
	}

	if (!listing_parser_) {
		log(logmsg::debug_warning, L"listing_parser_ is null");
		trigger_reset(FZ_REPLY_INTERNALERROR);
		return continuation::error;
	}

	std::optional<int> flags;
	if (e.perms_) {
		flags.emplace();
		if (e.is_directory()) {
			*flags |= CDirentry::flag_dir;
		}
		if (e.is_symlink()) {
			*flags |= CDirentry::flag_dir | CDirentry::flag_link;
		}
	}

	listing_parser_->AddLine(std::move(longname), std::move(name), e.modified_ ? *e.modified_ : fz::datetime(), e.size_, flags);

	return continuation::next;
}

