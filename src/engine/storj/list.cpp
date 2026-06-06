#include "../filezilla.h"

#include "../directorycache.h"
#include "list.h"

namespace {
enum listStates
{
	list_init = 0,
	list_waitlock,
	list_list
};
}

int CStorjListOpData::Send()
{
	switch (opState) {
	case list_init:
		path_ = CServerPath::GetChanged(currentPath_, path_, subDir_);
		subDir_.clear();
		if (path_.empty()) {
			path_ = CServerPath(L"/");
		}
		currentPath_ = path_;

		log(logmsg::status, _("Retrieving directory listing of \"%s\"..."), currentPath_.GetPath());

		if (currentPath_.GetType() != UNIX) {
			log(logmsg::debug_warning, L"CStorjListOpData::Send called with incompatible server type %d in path", currentPath_.GetType());
			return FZ_REPLY_INTERNALERROR;
		}

		opState = list_waitlock;
		if (!opLock_) {
			opLock_ = controlSocket_.Lock(locking_reason::list, path_);
			time_before_locking_ = fz::monotonic_clock::now();
		}
		if (opLock_.waiting()) {
			return FZ_REPLY_WOULDBLOCK;
		}

		opState = list_list;
		return FZ_REPLY_CONTINUE;
	case list_waitlock:
		if (!opLock_) {
			log(logmsg::debug_warning, L"Not holding the lock as expected");
			return FZ_REPLY_INTERNALERROR;
		}

		{
			// Check if we can use already existing listing
			CDirectoryListing listing;
			bool is_outdated = false;
			bool found = engine_.GetDirectoryCache().Lookup(listing, currentServer_, path_, false, is_outdated);
			if (found && !is_outdated &&
				listing.m_firstListTime >= time_before_locking_)
			{
				controlSocket_.SendDirectoryListingNotification(listing.path, false);
				return FZ_REPLY_OK;
			}
		}
		opState = list_list;
		return FZ_REPLY_CONTINUE;
	case list_list:
		return controlSocket_.SendCommand(L"list " + controlSocket_.QuoteFilename(path_.GetPath()));
	}

	log(logmsg::debug_warning, L"Unknown opState in CStorjListOpData::Send()");
	return FZ_REPLY_INTERNALERROR;
}

int CStorjListOpData::ParseResponse()
{
	if (opState == list_list) {
		if (controlSocket_.result_ != FZ_REPLY_OK) {
			return controlSocket_.result_;
		}
		CDirectoryListing listing;
		listing.path = path_;
		listing.m_firstListTime = fz::monotonic_clock::now();
		listing.Assign(std::move(entries_));

		engine_.GetDirectoryCache().Store(listing, currentServer_);
		controlSocket_.SendDirectoryListingNotification(listing.path, false);

		currentPath_ = path_;
		return FZ_REPLY_OK;
	}

	log(logmsg::debug_warning, L"CStorjListOpData::ParseResponse called at improper time: %d", opState);
	return FZ_REPLY_INTERNALERROR;
}

int CStorjListOpData::ParseEntry(std::wstring && name, std::wstring const& size, std::wstring const& created)
{
	if (opState != list_list) {
		log(logmsg::debug_warning, L"CStorjListOpData::ParseEntry called at improper time: %d", opState);
		return FZ_REPLY_INTERNALERROR;
	}

	CDirentry entry;
	entry.name = name;
	if (!path_.SegmentCount() ) {
		entry.flags = CDirentry::flag_dir;
	}
	else {
		if (!entry.name.empty() && entry.name.back() == '/') {
			entry.flags = CDirentry::flag_dir;
			entry.name.pop_back();
		}
		else {
			entry.flags = 0;
		}
	}

	if (entry.is_dir()) {
		entry.size = -1;
	}
	else {
		entry.size = fz::to_integral<int64_t>(size, -1);
	}

	time_t t = fz::to_integral<time_t>(created);
	if (t) {
		entry.time = fz::datetime(t, fz::datetime::seconds);
	}

	if (!entry.name.empty()) {
		entries_.emplace_back(std::move(entry));
	}

	return FZ_REPLY_WOULDBLOCK;
}
