#ifndef FILEZILLA_REMOTE_RECURSIVE_OPERATION_HEADER
#define FILEZILLA_REMOTE_RECURSIVE_OPERATION_HEADER

#include "state.h"
#include "../commonui/remote_recursive_operation.h"

class COptionsBase;
class CQueueView;
class CActionAfterBlocker;

class CRemoteRecursiveOperation final : public remote_recursive_operation, public CStateEventHandler
{
public:
	CRemoteRecursiveOperation(COptionsBase & options, CState& state);
	virtual ~CRemoteRecursiveOperation();

	void StartRecursiveOperation(OperationMode mode, ActiveFilters const& filters, bool immediate = true);
	void SetImmediate(bool immediate);

	void StopRecursiveOperation() override;

	void SetQueue(CQueueView* pQueue) { m_pQueue = pQueue; }

protected:
	void do_start_recursive_operation(OperationMode mode, ActiveFilters const& filters) override;
	void process_command(std::unique_ptr<CCommand>) override;
	void operation_finished() override;
	std::wstring sanitize_filename(std::wstring const& name) override;

	void handle_file(std::wstring const& sourceFile, CLocalPath const& localPath, CServerPath const& remotePath, int64_t size) override;
	void handle_empty_directory(CLocalPath const& localPath) override;
	void handle_invalid_dir_link(std::wstring const& sourceFile, CLocalPath const& localPath, CServerPath const& remotePath) override;
	void handle_dir_listing_end() override;
	void handle_listing_failed() override;

	void OnStateChange(t_statechange_notifications notification, std::wstring const&, const void* data) override;

	COptionsBase & options_;
	CState& m_state;
	CQueueView* m_pQueue{};
	std::shared_ptr<CActionAfterBlocker> m_actionAfterBlocker;
	bool m_immediate{true};
	bool added_to_queue_{};

	friend class CCommandQueue;
};

#endif
