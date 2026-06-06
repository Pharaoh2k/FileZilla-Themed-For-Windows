#include "../filezilla.h"

#include "../directorycache.h"
#include "../servercapabilities.h"
#include "../../include/engine_options.h"
#include "list.h"
#include "transfersocket.h"

#include <assert.h>

namespace {
enum listStates
{
        list_init,
        list_waitcwd,
        list_waitlock,
        list_waittransfer,
        list_mdtm
};

// Some servers are broken. Instead of an empty listing, some MVS servers
// for example they return "550 no members found"
// Other servers return "550 No files found."
bool IsMisleadingListResponse(std::wstring const& response)
{
	// Some servers are broken. Instead of an empty listing, some MVS servers
	// for example they return "550 no members found"
	// Other servers return "550 No files found."

	if (!fz::stricmp(response, L"550 No members found.")) {
		return true;
	}

	if (!fz::stricmp(response, L"550 No data sets found.")) {
		return true;
	}

	if (fz::str_tolower_ascii(response) == L"550 no files found.") {
		return true;
	}

	return false;
}
}

CFtpListOpData::CFtpListOpData(CFtpControlSocket & controlSocket, CServerPath const& path, std::wstring const& subDir, int flags)
    : COpData(Command::list, L"CFtpListOpData")
    , CFtpOpData(controlSocket)
    , path_(path)
    , subDir_(subDir)
    , flags_(flags)
{
	path_.SetType(currentServer_.GetType());
	refresh_ = (flags & LIST_FLAG_REFRESH) != 0;
	fallback_to_current_ = !path.empty() && (flags & LIST_FLAG_FALLBACK_CURRENT) != 0;
}

int CFtpListOpData::Send()
{
	if (opState == list_init) {
		auto newPath = CServerPath::GetChanged(currentPath_, path_, subDir_);
		if (newPath.empty()) {
			log(logmsg::status, _("Retrieving directory listing..."));
		}
		else {
			log(logmsg::status, _("Retrieving directory listing of \"%s\"..."), newPath.GetPath());
		}

		controlSocket_.ChangeDir(path_, subDir_, (flags_ & LIST_FLAG_LINK));
		opState = list_waitcwd;
		return FZ_REPLY_CONTINUE;
	}
	if (opState == list_waitlock) {
		assert(subDir_.empty()); // We did do ChangeDir before trying to lock

		// Check if we can use already existing listing
		CDirectoryListing listing;
		bool is_outdated = false;
		bool found = engine_.GetDirectoryCache().Lookup(listing, currentServer_, currentPath_, false, is_outdated);
		if (found && !is_outdated &&
			(!refresh_ || (opLock_ && listing.m_firstListTime >= time_before_locking_)))
		{
			controlSocket_.SendDirectoryListingNotification(currentPath_, false);
			return FZ_REPLY_OK;
		}

		if (!opLock_) {
			opLock_ = controlSocket_.Lock(locking_reason::list, currentPath_);
			time_before_locking_ = fz::monotonic_clock::now();
		}
		if (opLock_.waiting()) {
			return FZ_REPLY_WOULDBLOCK;
		}

		controlSocket_.m_pTransferSocket.reset();
		controlSocket_.m_pTransferSocket = std::make_unique<CTransferSocket>(engine_, controlSocket_, TransferMode::list);

		// Assume that a server supporting UTF-8 does not send EBCDIC listings.
		listingEncoding::type encoding = listingEncoding::unknown;
		if (CServerCapabilities::GetCapability(currentServer_, utf8_command) == yes) {
			encoding = listingEncoding::normal;
		}

		listing_parser_ = std::make_unique<CDirectoryListingParser>(&controlSocket_, currentServer_, encoding);

		listing_parser_->SetTimezoneOffset(controlSocket_.GetInferredTimezoneOffset());
		controlSocket_.m_pTransferSocket->m_pDirectoryListingParser = listing_parser_.get();

		engine_.transfer_status_.Init(-1, 0, true);

		opState = list_waittransfer;
		if (CServerCapabilities::GetCapability(currentServer_, mlsd_command) == yes) {
			controlSocket_.Transfer(L"MLSD", this);
		}
		else {
			if (options_.get_int(OPTION_VIEW_HIDDEN_FILES)) {
				capabilities cap = CServerCapabilities::GetCapability(currentServer_, list_hidden_support);
				if (cap == unknown) {
					viewHiddenCheck_ = true;
				}
				else if (cap == yes) {
					viewHidden_ = true;
				}
				else {
					log(logmsg::debug_info, _("View hidden option set, but unsupported by server"));
				}
			}

			if (viewHidden_) {
				controlSocket_.Transfer(L"LIST -a", this);
			}
			else {
				controlSocket_.Transfer(L"LIST", this);
			}
		}
		return FZ_REPLY_CONTINUE;
	}
	if (opState == list_mdtm) {
		log(logmsg::status, _("Calculating timezone offset of server..."));
		std::wstring cmd = L"MDTM " + currentPath_.FormatFilename(directoryListing_[mdtm_index_].name, true);
		return controlSocket_.SendCommand(cmd);
	}

	log(logmsg::debug_warning, L"invalid opstate %d", opState);
	return FZ_REPLY_INTERNALERROR;
}


int CFtpListOpData::ParseResponse()
{
	if (opState != list_mdtm) {
		log(logmsg::debug_warning, "CFtpListOpData::ParseResponse should never be called if opState != list_mdtm");
		return FZ_REPLY_INTERNALERROR;
	}

	std::wstring const& response = controlSocket_.m_Response;

	// First condition prevents problems with concurrent MDTM
	if (CServerCapabilities::GetCapability(currentServer_, inferred_timezone_offset) == unknown &&
	    response.substr(0, 4) == L"213 " && response.size() > 16)
	{
		fz::datetime date(response.substr(4), fz::datetime::utc);
		if (!date.empty()) {
			assert(directoryListing_[mdtm_index_].has_date());
			fz::datetime listTime = directoryListing_[mdtm_index_].time;
			listTime -= fz::duration::from_minutes(currentServer_.GetTimezoneOffset());

			int serveroffset = static_cast<int>((date - listTime).get_seconds());
			if (!directoryListing_[mdtm_index_].has_seconds()) {
				// Round offset to full minutes
				if (serveroffset < 0) {
					serveroffset -= 59;
				}
				serveroffset -= serveroffset % 60;
			}

			log(logmsg::status, L"Timezone offset of server is %d seconds.", -serveroffset);

			fz::duration span = fz::duration::from_seconds(serveroffset);
			size_t const count = directoryListing_.size();
			for (size_t i = 0; i < count; ++i) {
				CDirentry& entry = directoryListing_.get(i);
				entry.time += span;
			}

			// TODO: Correct cached listings

			CServerCapabilities::SetCapability(currentServer_, inferred_timezone_offset, yes, serveroffset);
		}
		else {
			CServerCapabilities::SetCapability(currentServer_, mdtm_command, no);
			CServerCapabilities::SetCapability(currentServer_, inferred_timezone_offset, no);
		}
	}
	else {
		CServerCapabilities::SetCapability(currentServer_, inferred_timezone_offset, no);
	}

	engine_.GetDirectoryCache().Store(directoryListing_, currentServer_);

	controlSocket_.SendDirectoryListingNotification(currentPath_, false);

	return FZ_REPLY_OK;
}


int CFtpListOpData::SubcommandResult(int prevResult, COpData const&)
{
	if (opState == list_waitcwd) {
		if (prevResult != FZ_REPLY_OK) {
			if ((prevResult & FZ_REPLY_LINKNOTDIR) == FZ_REPLY_LINKNOTDIR) {
				return prevResult;
			}

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
	else if (opState == list_waittransfer) {
		if (prevResult == FZ_REPLY_OK) {
			CDirectoryListing listing = listing_parser_->Parse(currentPath_);

			if (viewHiddenCheck_) {
				if (!viewHidden_) {
					// Repeat with LIST -a
					viewHidden_ = true;
					directoryListing_ = listing;

					// Reset status
					transferEndReason = TransferEndReason::successful;
					tranferCommandSent = false;
					controlSocket_.m_pTransferSocket.reset();
					controlSocket_.m_pTransferSocket = std::make_unique<CTransferSocket>(engine_, controlSocket_, TransferMode::list);
					listing_parser_->Reset();
					controlSocket_.m_pTransferSocket->m_pDirectoryListingParser = listing_parser_.get();

					controlSocket_.Transfer(L"LIST -a", this);
					return FZ_REPLY_CONTINUE;
				}
				else {
					if (CheckInclusion(listing, directoryListing_)) {
						log(logmsg::debug_info, L"Server seems to support LIST -a");
						CServerCapabilities::SetCapability(currentServer_, list_hidden_support, yes);
					}
					else {
						log(logmsg::debug_info, L"Server does not seem to support LIST -a");
						CServerCapabilities::SetCapability(currentServer_, list_hidden_support, no);
						listing = directoryListing_;
					}
				}
			}

			controlSocket_.SetAlive();

			int res = CheckTimezoneDetection(listing);
			if (res != FZ_REPLY_OK) {
				return res;
			}

			engine_.GetDirectoryCache().Store(listing, currentServer_);

			controlSocket_.SendDirectoryListingNotification(currentPath_, false);

			return FZ_REPLY_OK;
		}
		else {
			if (tranferCommandSent && IsMisleadingListResponse(controlSocket_.m_Response)) {
				CDirectoryListing listing;
				listing.path = currentPath_;
				listing.m_firstListTime = fz::monotonic_clock::now();

				if (viewHiddenCheck_) {
					if (viewHidden_) {
						if (directoryListing_.size()) {
							// Less files with LIST -a
							// Not supported
							log(logmsg::debug_info, L"Server does not seem to support LIST -a");
							CServerCapabilities::SetCapability(currentServer_, list_hidden_support, no);
							listing = directoryListing_;
						}
						else {
							log(logmsg::debug_info, L"Server seems to support LIST -a");
							CServerCapabilities::SetCapability(currentServer_, list_hidden_support, yes);
						}
					}
					else {
						// Reset status
						transferEndReason = TransferEndReason::successful;
						tranferCommandSent = false;
						controlSocket_.m_pTransferSocket.reset();
						controlSocket_.m_pTransferSocket = std::make_unique<CTransferSocket>(engine_, controlSocket_, TransferMode::list);
						listing_parser_->Reset();
						controlSocket_.m_pTransferSocket->m_pDirectoryListingParser = listing_parser_.get();

						// Repeat with LIST -a
						viewHidden_ = true;
						directoryListing_ = listing;
						controlSocket_.Transfer(L"LIST -a", this);
						return FZ_REPLY_CONTINUE;
					}
				}

				int res = CheckTimezoneDetection(listing);
				if (res != FZ_REPLY_OK) {
					return res;
				}

				engine_.GetDirectoryCache().Store(listing, currentServer_);

				controlSocket_.SendDirectoryListingNotification(currentPath_, false);

				return FZ_REPLY_OK;
			}
			else {
				if (viewHiddenCheck_) {
					// If server does not support LIST -a, the server might reject this command
					// straight away. In this case, back to the previously retrieved listing.
					// On other failures like timeouts and such, return an error
					if (viewHidden_ &&
						transferEndReason == TransferEndReason::transfer_command_failure_immediate)
					{
						CServerCapabilities::SetCapability(currentServer_, list_hidden_support, no);

						int res = CheckTimezoneDetection(directoryListing_);
						if (res != FZ_REPLY_OK) {
							return res;
						}

						engine_.GetDirectoryCache().Store(directoryListing_, currentServer_);

						controlSocket_.SendDirectoryListingNotification(currentPath_, false);

						return FZ_REPLY_OK;
					}
				}

				if (prevResult & FZ_REPLY_ERROR) {
					controlSocket_.SendDirectoryListingNotification(currentPath_, true);
				}
			}

			return FZ_REPLY_ERROR;
		}
	}
	else {
		log(logmsg::debug_warning, L"Wrong opState: %d", opState);
		return FZ_REPLY_INTERNALERROR;
	}
}

int CFtpListOpData::CheckTimezoneDetection(CDirectoryListing& listing)
{
	if (CServerCapabilities::GetCapability(currentServer_, inferred_timezone_offset) == unknown) {
		if (CServerCapabilities::GetCapability(currentServer_, mdtm_command) != yes) {
			CServerCapabilities::SetCapability(currentServer_, inferred_timezone_offset, no);
		}
		else {
			size_t const count = listing.size();
			for (size_t i = 0; i < count; ++i) {
				if (!listing[i].is_dir() && listing[i].has_time()) {
					opState = list_mdtm;
					directoryListing_ = listing;
					mdtm_index_ = i;
					return FZ_REPLY_CONTINUE;
				}
			}
		}
	}

	return FZ_REPLY_OK;
}
