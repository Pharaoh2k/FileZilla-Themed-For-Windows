#include "filezilla.h"
#include "local_recursive_operation.h"

#include <libfilezilla/local_filesys.hpp>

#include "QueueView.h"

BEGIN_EVENT_TABLE(CLocalRecursiveOperation, wxEvtHandler)
END_EVENT_TABLE()


CLocalRecursiveOperation::CLocalRecursiveOperation(CState& state)
: local_recursive_operation(state.pool_)
, state_(state)
{
}

CLocalRecursiveOperation::~CLocalRecursiveOperation()
{
	thread_.join();
}

void CLocalRecursiveOperation::StartRecursiveOperation(OperationMode mode, ActiveFilters const& filters, bool immediate, bool ignore_links)
{
	m_failed = false;
	m_immediate = immediate;
	start_recursive_operation(mode, filters, ignore_links);
}

bool CLocalRecursiveOperation::do_start_recursive_operation(OperationMode mode, ActiveFilters const& filters, bool ignore_links)
{
	if (!m_pQueue) {
		return false;
	}

	Site const& site = state_.GetSite();
	if (site) {
		site_ = site;
	}
	else {
		if (mode != OperationMode::recursive_list) {
			return false;
		}

		site_ = Site();
	}

	if (!local_recursive_operation::do_start_recursive_operation(mode, filters, ignore_links)) {
		return false;
	}

	if ((mode == recursive_operation::recursive_transfer || mode == recursive_operation::recursive_transfer_flatten) && m_immediate) {
		m_actionAfterBlocker = m_pQueue->GetActionAfterBlocker();
	}

	state_.NotifyHandlers(STATECHANGE_LOCAL_RECURSION_STATUS);

	return true;
}

void CLocalRecursiveOperation::StopRecursiveOperation()
{
	local_recursive_operation::StopRecursiveOperation();

	state_.NotifyHandlers(STATECHANGE_LOCAL_RECURSION_STATUS);
	m_actionAfterBlocker.reset();
}

void CLocalRecursiveOperation::on_listed_directory()
{
	CallAfter(&CLocalRecursiveOperation::OnListedDirectory);
}

void CLocalRecursiveOperation::on_listing_failed()
{
	CallAfter(&CLocalRecursiveOperation::OnListingFailed);
}

void CLocalRecursiveOperation::OnListedDirectory()
{
	if (m_operationMode == recursive_none) {
		return;
	}

	bool const queue = m_operationMode == recursive_transfer || m_operationMode == recursive_transfer_flatten;

	listing d;

	bool stop = false;
	int64_t processed = 0;
	while (processed < 5000) {
		{
			fz::scoped_lock l(mutex_);
			if (m_listedDirectories.empty()) {
				break;
			}

			d = std::move(m_listedDirectories.front());
			m_listedDirectories.pop_front();
		}

		if (d.localPath.empty()) {
			stop = true;
		}
		else {
			if (queue) {
				m_pQueue->QueueFiles(!m_immediate, site_, d);
			}
			++m_processedDirectories;
			processed += d.files.size();
			state_.NotifyHandlers(STATECHANGE_LOCAL_RECURSION_LISTING, std::wstring(), &d);
		}
	}

	if (queue) {
		m_pQueue->QueueFile_Finish(m_immediate);
	}

	m_processedFiles += processed;
	if (stop) {
		StopRecursiveOperation();
	}
	else if (processed) {
		state_.NotifyHandlers(STATECHANGE_LOCAL_RECURSION_STATUS);

		if (processed >= 5000) {
			CallAfter(&CLocalRecursiveOperation::OnListedDirectory);
		}
	}
}

void CLocalRecursiveOperation::OnListingFailed()
{
	m_failed = true;
	state_.NotifyHandlers(STATECHANGE_LOCAL_RECURSION_STATUS);
}

void CLocalRecursiveOperation::SetImmediate(bool immediate)
{
	if (m_operationMode == recursive_transfer || m_operationMode == recursive_transfer_flatten) {
		m_immediate = immediate;
		if (!immediate) {
			m_actionAfterBlocker.reset();
		}
	}
}

