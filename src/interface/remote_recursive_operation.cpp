#include "filezilla.h"
#include "remote_recursive_operation.h"
#include "commandqueue.h"
#include "chmoddialog.h"
#include "filter_manager.h"
#include "Options.h"
#include "queue.h"

#include "../commonui/misc.h"

#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/recursive_remove.hpp>

CRemoteRecursiveOperation::CRemoteRecursiveOperation(COptionsBase & options, CState &state)
: CStateEventHandler(state)
, options_(options)
, m_state(state)
{
	state.RegisterHandler(this, STATECHANGE_REMOTE_DIR_OTHER);
	state.RegisterHandler(this, STATECHANGE_REMOTE_LINKNOTDIR);
}

CRemoteRecursiveOperation::~CRemoteRecursiveOperation()
{
}

void CRemoteRecursiveOperation::OnStateChange(t_statechange_notifications notification, std::wstring const&, const void* data)
{
	if (notification == STATECHANGE_REMOTE_DIR_OTHER && data) {
		std::shared_ptr<CDirectoryListing> const& listing = *reinterpret_cast<std::shared_ptr<CDirectoryListing> const*>(data);

		if (!m_state.IsRemoteConnected()) {
			StopRecursiveOperation();
			return;
		}
		if (!m_state.GetSite()) {
			StopRecursiveOperation();
			return;
		}
		ProcessDirectoryListing(listing.get());
	}
	else if (notification == STATECHANGE_REMOTE_LINKNOTDIR) {
		wxASSERT(data);
		LinkIsNotDir(m_state.GetSite());
	}
}

void CRemoteRecursiveOperation::StartRecursiveOperation(OperationMode mode, ActiveFilters const& filters, bool immediate)
{
	if (!m_state.IsRemoteConnected()) {
		assert(!"StartRecursiveOperation while disconnected");
		return;
	}
	if ((mode == recursive_transfer || mode == recursive_transfer_flatten) && !m_pQueue) {
		return;
	}
	m_immediate = immediate;
	m_failed = false;
	remote_recursive_operation::start_recursive_operation(mode, filters, options_.get_bool(OPTION_REMOTE_ROP_LISTING_REFRESH));
}

void CRemoteRecursiveOperation::do_start_recursive_operation(OperationMode mode, ActiveFilters const& filters)
{
	if ((mode == recursive_operation::recursive_transfer || mode == recursive_operation::recursive_transfer_flatten) && m_immediate) {
		m_actionAfterBlocker = m_pQueue->GetActionAfterBlocker();
	}

	m_state.NotifyHandlers(STATECHANGE_REMOTE_IDLE);
	m_state.NotifyHandlers(STATECHANGE_REMOTE_RECURSION_STATUS);

	remote_recursive_operation::do_start_recursive_operation(mode, filters);
}

void CRemoteRecursiveOperation::process_command(std::unique_ptr<CCommand> pCommand)
{
	m_state.m_pCommandQueue->ProcessCommand(pCommand.release(), CCommandQueue::recursiveOperation);
}

std::wstring CRemoteRecursiveOperation::sanitize_filename(std::wstring const& name)
{
	return CQueueView::ReplaceInvalidCharacters(options_, name);
}

void CRemoteRecursiveOperation::operation_finished()
{
	if (m_operationMode != OperationMode::recursive_list) {
		m_state.RefreshRemote();
	}
}

void CRemoteRecursiveOperation::handle_file(std::wstring const& sourceFile, CLocalPath const& localPath, CServerPath const& remotePath, int64_t size)
{
	std::wstring file = sanitize_filename(sourceFile);
	if (remotePath.GetType() == VMS && options_.get_int(OPTION_STRIP_VMS_REVISION)) {
		file = StripVMSRevision(file);
	}
	m_pQueue->QueueFile(!m_immediate, true, sourceFile, (sourceFile == file) ? std::wstring() : file, localPath, remotePath, m_state.GetSite(), size);
	added_to_queue_ = true;
}

void CRemoteRecursiveOperation::handle_dir_listing_end()
{
	if (added_to_queue_) {
		m_pQueue->QueueFile_Finish(m_immediate);
		added_to_queue_ = false;
	}
	m_state.NotifyHandlers(STATECHANGE_REMOTE_RECURSION_STATUS);
}

void CRemoteRecursiveOperation::handle_empty_directory(CLocalPath const& localPath)
{
	if (m_immediate) {
		fz::mkdir(fz::to_native(localPath.GetPath()), true);
		m_state.RefreshLocalFile(localPath.GetPath());
	}
	else {
		m_pQueue->QueueFile(true, true, _T(""), _T(""), localPath, CServerPath(), m_state.GetSite(), -1);
		m_pQueue->QueueFile_Finish(false);
	}
}

void CRemoteRecursiveOperation::handle_invalid_dir_link(std::wstring const& sourceFile, CLocalPath const& localPath, CServerPath const& remotePath)
{
	handle_file(sourceFile, localPath, remotePath, -1);
	m_pQueue->QueueFile_Finish(m_immediate);
	added_to_queue_ = false;
}

void CRemoteRecursiveOperation::handle_listing_failed()
{
	m_failed = true;
	m_state.NotifyHandlers(STATECHANGE_REMOTE_RECURSION_STATUS);
}

void CRemoteRecursiveOperation::StopRecursiveOperation()
{
	bool notify = m_operationMode != recursive_none;
	remote_recursive_operation::StopRecursiveOperation();
	if (notify) {
		m_state.NotifyHandlers(STATECHANGE_REMOTE_IDLE);
		m_state.NotifyHandlers(STATECHANGE_REMOTE_RECURSION_STATUS);
	}
	m_actionAfterBlocker.reset();
}

void CRemoteRecursiveOperation::SetImmediate(bool immediate)
{
	if (m_operationMode == recursive_transfer || m_operationMode == recursive_transfer_flatten) {
		m_immediate = immediate;
		if (!immediate) {
			m_actionAfterBlocker.reset();
		}
	}
}
