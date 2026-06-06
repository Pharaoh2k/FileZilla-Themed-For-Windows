#include "../filezilla.h"

#include "../directorycache.h"
#include "mkd.h"

using namespace std::literals;

namespace {
enum mkdStates
{
	mkd_init = 0,
	mkd_findparent,
	mkd_mkdsub,
	mkd_cwdsub,
	mkd_tryfull
};
}

/* Directory creation works like this: First find a parent directory into
 * which we can CWD, then create the subdirs one by one. If either part
 * fails, try MKD with the full path directly.
 */

int CFtpMkdirOpData::Send()
{
	if (!opLock_) {
		opLock_ = controlSocket_.Lock(locking_reason::mkdir, path_);
	}
	if (opLock_.waiting()) {
		return FZ_REPLY_WOULDBLOCK;
	}

	switch (opState)
	{
	case mkd_init:
		if (controlSocket_.operations_.size() == 1 && !path_.empty()) {
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

		if (!path_.HasParent()) {
			opState = mkd_tryfull;
		}
		else {
			currentMkdPath_ = path_.GetParent();
			segments_.push_back(path_.GetLastSegment());

			if (currentMkdPath_ == currentPath_) {
				opState = mkd_mkdsub;
			}
			else {
				opState = mkd_findparent;
			}
		}
		return FZ_REPLY_CONTINUE;
	case mkd_findparent:
	case mkd_cwdsub:
		currentPath_.clear();
		return controlSocket_.SendCommand(L"CWD " + currentMkdPath_.GetPath());
	case mkd_mkdsub:
		return controlSocket_.SendCommand(L"MKD " + segments_.back());
	case mkd_tryfull:
		return controlSocket_.SendCommand(L"MKD " + path_.GetPath());
	default:
		log(logmsg::debug_warning, L"unknown op state: %d", opState);
		break;
	}

	return FZ_REPLY_INTERNALERROR;
}

namespace {
bool IsAlreadyExistsResponse(std::wstring const& raw_response, CServerPath const& raw_path)
{
	// This isn't an error if the error message is "already exists".
	// Case 1: Full response a known "already exists" message.
	// Case 2: Substring of response contains "already exists". path may not
	//         contain this substring as the path might be returned in the reply.
	// Case 3: Substring of response contains "file exists". path may not
	//         contain this substring as the path might be returned in the reply.

	std::wstring const response = fz::str_tolower_ascii(std::wstring_view(raw_response).substr(4));
	std::wstring const p = fz::str_tolower_ascii(raw_path.GetPath());
	if (response == L"directory already exists"sv || response == L"file or directory already exists"sv) {
		return true;
	}

	if (p.find(L"already exists"sv) == std::wstring::npos && response.find(L"already exists"sv) != std::wstring::npos) {
		return true;
	}

	if (p.find(L"file exists"sv) == std::wstring::npos && response.find(L"file exists"sv) != std::wstring::npos) {
		return true;
	}

	if (p.find(L"directory exists"sv) == std::wstring::npos && response.find(L"directory exists"sv) != std::wstring::npos) {
		return true;
	}

	return false;
}
}

int CFtpMkdirOpData::ParseResponse()
{
	int code = controlSocket_.GetReplyCode();
	switch (opState) {
	case mkd_findparent:
		if (code == 2 || code == 3) {
			currentPath_ = currentMkdPath_;
			opState = mkd_mkdsub;
		}
		else if (currentMkdPath_ == commonParent_) {
			opState = mkd_tryfull;
		}
		else if (currentMkdPath_.HasParent()) {
			CServerPath const parent = currentMkdPath_.GetParent();
			segments_.push_back(currentMkdPath_.GetLastSegment());
			currentMkdPath_ = parent;
		}
		else {
			opState = mkd_tryfull;
		}
		return FZ_REPLY_CONTINUE;
	case mkd_mkdsub:
		if (segments_.empty()) {
			log(logmsg::debug_warning, L"  segments is empty");
			return FZ_REPLY_INTERNALERROR;
		}

		if (code != 2 && code != 3) {
			if (!IsAlreadyExistsResponse(controlSocket_.m_Response, path_)) {
				return FZ_REPLY_ERROR;
			}

			CDirentry entry;
			bool tmp;
			if (engine_.GetDirectoryCache().LookupFile(entry, currentServer_, currentMkdPath_, segments_.back(), tmp, tmp) && !entry.is_dir()) {
				return FZ_REPLY_ERROR;
			}
		}

		engine_.GetDirectoryCache().UpdateFile(currentServer_, currentMkdPath_, segments_.back(), true, CDirectoryCache::dir);
		controlSocket_.SendDirectoryListingNotification(currentMkdPath_, false);

		currentMkdPath_.AddSegment(segments_.back());
		segments_.pop_back();

		if (segments_.empty()) {
			return FZ_REPLY_OK;
		}
		else {
			opState = mkd_cwdsub;
		}
		return FZ_REPLY_CONTINUE;
	case mkd_cwdsub:
		if (code == 2 || code == 3) {
			currentPath_ = currentMkdPath_;
			opState = mkd_mkdsub;
		}
		else {
			opState = mkd_tryfull;
		}
		return FZ_REPLY_CONTINUE;
	case mkd_tryfull:
		if (code != 2 && code != 3) {
			return FZ_REPLY_ERROR;
		}
		else {
			return FZ_REPLY_OK;
		}
		break;
	default:
		log(logmsg::debug_warning, L"unknown op state: %d", opState);
	}

	return FZ_REPLY_INTERNALERROR;
}
