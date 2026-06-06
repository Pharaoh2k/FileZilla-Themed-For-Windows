#include "filezilla.h"
#include "commandqueue.h"
#include "Mainfrm.h"
#include "state.h"
#include "remote_recursive_operation.h"
#include "loginmanager.h"
#include "queue.h"
#include "RemoteListView.h"

#include <algorithm>

CCommandQueue::CCommandQueue(CFileZillaEngine *pEngine, CMainFrame* pMainFrame, CState& state)
	: m_pEngine(pEngine)
	, m_pMainFrame(pMainFrame)
	, m_state(state)
{
}

bool CCommandQueue::Idle(command_origin origin) const
{
	if (exclusive_lock_) {
		return false;
	}

	if (origin == any) {
		return m_CommandList.empty();
	}

	return std::find_if(m_CommandList.begin(), m_CommandList.end(), [origin](CommandInfo const& c) { return c.origin == origin; }) == m_CommandList.end();
}

void CCommandQueue::ProcessCommand(CCommand *pCommand, CCommandQueue::command_origin origin)
{
	wxASSERT(origin != any);
	if (m_quit) {
		delete pCommand;
		return;
	}

	m_CommandList.emplace_back(origin, std::unique_ptr<CCommand>(pCommand));
	if (m_CommandList.size() == 1) {
		m_state.NotifyHandlers(STATECHANGE_REMOTE_IDLE);
		ProcessNextCommand();
	}
}

void CCommandQueue::ProcessNextCommand()
{
	if (m_inside_commandqueue) {
		return;
	}

	if (exclusive_lock_) {
		return;
	}

	if (m_pEngine->IsBusy()) {
		return;
	}

	++m_inside_commandqueue;

	if (m_CommandList.empty()) {
		// Possible sequence of events:
		// - Engine emits listing and operation finished
		// - Connection gets terminated
		// - Interface cannot obtain listing since not connected
		// - Yet getting operation successful
		// To keep things flowing, we need to advance the recursive operation.
		m_state.GetRemoteRecursiveOperation()->NextOperation();
	}

	while (!m_CommandList.empty()) {
		auto const& commandInfo = m_CommandList.front();

		int res = m_pEngine->Execute(*commandInfo.command);
		ProcessReply(res, commandInfo.command->GetId());
		if (res == FZ_REPLY_WOULDBLOCK) {
			break;
		}
	}

	--m_inside_commandqueue;

	if (m_CommandList.empty()) {
		if (exclusive_requests_.empty()) {
			m_state.NotifyHandlers(STATECHANGE_REMOTE_IDLE);
		}
		else {
			GrantExclusiveEngineRequest();
		}

		if (!m_state.SuccessfulConnect()) {
			m_state.SetSite(Site());
		}
	}
}

bool CCommandQueue::Cancel()
{
	if (exclusive_lock_) {
		return false;
	}

	if (m_CommandList.empty()) {
		return true;
	}

	m_CommandList.erase(++m_CommandList.begin(), m_CommandList.end());

	if (!m_pEngine)	{
		m_CommandList.clear();
		m_state.NotifyHandlers(STATECHANGE_REMOTE_IDLE);
		return true;
	}

	int res = m_pEngine->Cancel();
	if (res == FZ_REPLY_WOULDBLOCK) {
		return false;
	}
	else {
		m_CommandList.clear();
		m_state.NotifyHandlers(STATECHANGE_REMOTE_IDLE);
		return true;
	}
}

void CCommandQueue::Finish(std::unique_ptr<COperationNotification> && pNotification)
{
	if (exclusive_lock_) {
		if (!exclusive_requests_.empty()) {
			exclusive_requests_.front()->ProcessNotification(m_pEngine, std::move(pNotification));
		}
		return;
	}

	ProcessReply(pNotification->replyCode_, pNotification->commandId_);
}

void CCommandQueue::ProcessReply(int nReplyCode, Command commandId)
{
	if (nReplyCode == FZ_REPLY_WOULDBLOCK) {
		return;
	}
	if (nReplyCode & FZ_REPLY_DISCONNECTED) {
		if (nReplyCode & FZ_REPLY_PASSWORDFAILED) {
			m_pMainFrame->GetLoginManager().CachedPasswordFailed(m_state.GetSite().server);
		}
	}

	if (m_CommandList.empty()) {
		return;
	}

	auto & commandInfo = m_CommandList.front();

	if (commandId != Command::connect &&
		commandId != Command::disconnect &&
		(nReplyCode & FZ_REPLY_CANCELED) != FZ_REPLY_CANCELED)
	{
		if (nReplyCode & FZ_REPLY_DISCONNECTED) {
			if (!commandInfo.didReconnect) {
				// Try automatic reconnect
				commandInfo.didReconnect = true;
				Site const& site = m_state.GetSite();
				if (site) {
					m_CommandList.emplace_front(normal, std::make_unique<CConnectCommand>(site.server, site.Handle(), site.credentials));
					ProcessNextCommand();
					return;
				}
			}
		}
	}

	++m_inside_commandqueue;

	if (commandInfo.command->GetId() == Command::list && nReplyCode != FZ_REPLY_OK) {
		if ((nReplyCode & FZ_REPLY_LINKNOTDIR) == FZ_REPLY_LINKNOTDIR) {
			// Symbolic link does not point to a directory. Either points to file
			// or is completely invalid
			CListCommand* pListCommand = static_cast<CListCommand*>(commandInfo.command.get());
			wxASSERT(pListCommand->GetFlags() & LIST_FLAG_LINK);

			m_state.LinkIsNotDir(pListCommand->GetPath(), pListCommand->GetSubDir());
		}
		else {
			if (commandInfo.origin == recursiveOperation) {
				// Let the recursive operation handler know if a LIST command failed,
				// so that it may issue the next command in recursive operations.
				m_state.GetRemoteRecursiveOperation()->ListingFailed(nReplyCode);
			}
			else {
				m_state.ListingFailed(nReplyCode);
			}
		}
		m_CommandList.pop_front();
	}
	else if (nReplyCode == FZ_REPLY_ALREADYCONNECTED && commandInfo.command->GetId() == Command::connect) {
		m_CommandList.emplace_front(normal, std::make_unique<CDisconnectCommand>());
	}
	else if (commandInfo.command->GetId() == Command::connect && nReplyCode != FZ_REPLY_OK) {
		// Remove pending events
		auto it = ++m_CommandList.begin();
		while (it != m_CommandList.end() && it->command->GetId() != Command::connect) {
			++it;
		}
		m_CommandList.erase(m_CommandList.begin(), it);

		// If this was an automatic reconnect during a recursive
		// operation, stop the recursive operation
		m_state.GetRemoteRecursiveOperation()->StopRecursiveOperation();
	}
	else if (commandInfo.command->GetId() == Command::connect && nReplyCode == FZ_REPLY_OK) {
		m_state.SetSuccessfulConnect();
		m_CommandList.pop_front();
	}
	else {
		m_CommandList.pop_front();
	}

	--m_inside_commandqueue;

	ProcessNextCommand();
}

void CCommandQueue::RequestExclusiveEngine(CExclusiveHandler *exclusiveHandler)
{
	for (auto const* h : exclusive_requests_) {
		if (h == exclusiveHandler) {
			return;
		}
	}

	exclusive_requests_.emplace_back(exclusiveHandler);
	if (!exclusive_lock_ && m_CommandList.empty()) {
		m_state.NotifyHandlers(STATECHANGE_REMOTE_IDLE);
		GrantExclusiveEngineRequest();
	}
}

void CCommandQueue::GrantExclusiveEngineRequest()
{
	if (exclusive_lock_ || exclusive_requests_.empty()) {
		return;
	}

	exclusive_lock_ = true;
	++m_requestId;
	m_pMainFrame->CallAfter([this, id = m_requestId]() {
		exclusive_requests_.front()->OnExclusiveEngineRequestGranted(id);
	});
}

CFileZillaEngine* CCommandQueue::GetEngineExclusive(unsigned int requestId)
{
	if (!exclusive_lock_) {
		return 0;
	}

	if (requestId != m_requestId) {
		return 0;
	}

	return m_pEngine;
}

void CCommandQueue::ReleaseEngine(CExclusiveHandler *exclusiveHandler)
{
	auto it = std::find(exclusive_requests_.begin(), exclusive_requests_.end(), exclusiveHandler);
	if (it == exclusive_requests_.end()) {
			return;
	}
	bool first = it == exclusive_requests_.begin();
	exclusive_requests_.erase(it);

	if (first) {
		exclusive_lock_ = false;
		ProcessNextCommand();
	}
}

bool CCommandQueue::Quit()
{
	m_quit = true;
	return Cancel();
}

void CCommandQueue::ProcessDirectoryListing(CDirectoryListingNotification const& listingNotification)
{
	auto const firstListing = std::find_if(m_CommandList.begin(), m_CommandList.end(), [](CommandInfo const& v) { return v.command->GetId() == Command::list; });
	bool const listingIsRecursive = firstListing != m_CommandList.end() && firstListing->origin == recursiveOperation;

	std::shared_ptr<CDirectoryListing> pListing;
	if (!listingNotification.GetPath().empty()) {
		pListing = std::make_shared<CDirectoryListing>();
		if (listingNotification.Failed() ||
			m_state.engine_->CacheLookup(listingNotification.GetPath(), *pListing) != FZ_REPLY_OK)
		{
			pListing = std::make_shared<CDirectoryListing>();
			pListing->path = listingNotification.GetPath();
			pListing->m_flags |= CDirectoryListing::listing_failed;
			pListing->m_firstListTime = fz::monotonic_clock::now();
		}
	}

	if (listingIsRecursive) {
		if (listingNotification.Primary() && m_state.GetRemoteRecursiveOperation()->IsActive()) {
			m_state.NotifyHandlers(STATECHANGE_REMOTE_DIR_OTHER, std::wstring(), &pListing);
		}
	}
	else {
		m_state.SetRemoteDir(pListing, listingNotification.Primary());
	}

	if (pListing && !listingNotification.Failed() && m_state.GetSite()) {
		CContextManager::Get()->ProcessDirectoryListing(m_state.GetSite().server, pListing, listingIsRecursive ? 0 : &m_state);
	}
}
