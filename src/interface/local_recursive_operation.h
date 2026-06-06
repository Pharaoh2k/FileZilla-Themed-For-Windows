#ifndef FILEZILLA_LOCAL_RECURSIVE_OPERATION_HEADER
#define FILEZILLA_LOCAL_RECURSIVE_OPERATION_HEADER

#include "../commonui/local_recursive_operation.h"

class CQueueView;
class CActionAfterBlocker;
class CState;

class CLocalRecursiveOperation final : public local_recursive_operation, public wxEvtHandler
{
public:
	CLocalRecursiveOperation(CState& state);
	virtual ~CLocalRecursiveOperation();

	void StopRecursiveOperation() override;
	void SetImmediate(bool immediate);
	void SetQueue(CQueueView* pQueue) { m_pQueue = pQueue; }

	void StartRecursiveOperation(OperationMode mode, ActiveFilters const& filters, bool immediate = true, bool ignore_links = true);

protected:
	bool do_start_recursive_operation(OperationMode mode, ActiveFilters const& filters, bool ignore_links) override;
	void on_listed_directory() override;
	void on_listing_failed() override;

	void OnListedDirectory();
	void OnListingFailed();

	bool m_immediate{true};
	CQueueView* m_pQueue{};
	CState& state_;
	Site site_;
	std::shared_ptr<CActionAfterBlocker> m_actionAfterBlocker;

	DECLARE_EVENT_TABLE()
};

#endif
