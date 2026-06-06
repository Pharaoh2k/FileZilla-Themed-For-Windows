#include "filezilla.h"

#include "directorycache.h"
#include "lookup.h"

enum {
	lookup_init = 0,
	lookup_list
};

LookupOpData::LookupOpData(CControlSocket &controlSocket, CServerPath const& path, std::wstring const& file, CDirentry * entry)
    : COpData(Command::lookup, L"LookupOpData")
    , CProtocolOpData(controlSocket)
	, path_(path)
	, file_(file)
	, entry_(entry)
{
	if (!entry) {
		internal_entry_ = std::make_unique<CDirentry>();
		entry_ = internal_entry_.get();
	}
	entry_->clear();
}

int LookupOpData::Send()
{
	if (path_.empty() || file_.empty()) {
		return FZ_REPLY_INTERNALERROR;
	}
	else {
		log(logmsg::debug_info, L"Looking for '%s' in '%s'", file_, path_.GetPath());

		// look at the cache first

		LookupFlags flags{};
		if (opState == lookup_list) {
			// Can only make a difference if running on a literal potato, or if the developer
			// stepping through this code in a debugger late at night fell asleep
			flags |= LookupFlags::allow_outdated;
		}
		auto [results, entry] = engine_.GetDirectoryCache().LookupFile(currentServer_, path_, file_, flags);

		if (results & LookupResults::found) {
			if (!entry || entry.is_unsure()) {
				log(logmsg::debug_info, L"Found unsure entry for '%s': %d", file_, entry.flags);
			}
			else {
				*entry_ = std::move(entry);
				log(logmsg::debug_info, L"Found valid entry for '%s'", file_);
				return FZ_REPLY_OK;
			}
		}
		else if (results & LookupResults::direxists) {
			log(logmsg::debug_info, L"'%s' does not appear to exist", file_);
			return FZ_REPLY_ERROR_NOTFOUND;
		}

		if (opState == lookup_init) {
			opState = lookup_list;
			controlSocket_.List(path_, std::wstring(), LIST_FLAG_REFRESH);
			return FZ_REPLY_CONTINUE;
		}
		else {
			log(logmsg::debug_info, L"Directory %s not in cache after a successful listing", path_.GetPath());
			return FZ_REPLY_ERROR;
		}
	}
}

int LookupOpData::SubcommandResult(int prevResult, COpData const&)
{
	switch (opState) {
	case lookup_list:
		if (prevResult == FZ_REPLY_OK) {
			return FZ_REPLY_CONTINUE;
		}
		return prevResult;
	}

	log(logmsg::debug_warning, L"Unknown opState in LookupOpData::SubcommandResult()");
	return FZ_REPLY_INTERNALERROR;
}

int LookupManyOpData::Send()
{
	if (path_.empty() || files_.empty()) {
		return FZ_REPLY_INTERNALERROR;
	}
	else {
		log(logmsg::debug_info, L"Looking for %d items in '%s'", files_.size(), path_.GetPath());

		// look at the cache first

		LookupFlags flags{};
		if (opState == lookup_list) {
			// Can only make a difference if running on a literal potato, or if the developer
			// stepping through this code in a debugger late at night fell asleep
			flags |= LookupFlags::allow_outdated;
		}
		entries_ = engine_.GetDirectoryCache().LookupFiles(currentServer_, path_, files_, flags);

		if (!entries_.empty()) {
			bool unsure{};
			for (auto const& entry : entries_) {
				if (std::get<0>(entry) & LookupResults::found) {
					if (!std::get<1>(entry) || std::get<1>(entry).is_unsure()) {
						log(logmsg::debug_info, L"Found unsure entry for '%s': %d", std::get<1>(entry).name, std::get<1>(entry).flags);
						unsure = true;
						break;
					}
				}
			}
			if (!unsure) {
				return FZ_REPLY_OK;
			}
		}

		if (opState == lookup_init) {
			opState = lookup_list;
			controlSocket_.List(path_, std::wstring(), LIST_FLAG_REFRESH);
			return FZ_REPLY_CONTINUE;
		}
		else {
			log(logmsg::debug_warning, L"Directory %s not in cache after a successful listing", path_.GetPath());
			return FZ_REPLY_ERROR;
		}
	}
}

int LookupManyOpData::SubcommandResult(int prevResult, COpData const&)
{
	switch (opState) {
	case lookup_list:
		if (prevResult == FZ_REPLY_OK) {
			return FZ_REPLY_CONTINUE;
		}
		return prevResult;
	}

	log(logmsg::debug_warning, L"Unknown opState in LookupManyOpData::SubcommandResult()");
	return FZ_REPLY_INTERNALERROR;
}
