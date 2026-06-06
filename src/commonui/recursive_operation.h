#ifndef FILEZILLA_COMMONUI_RECURSIVE_OPERATION_HEADER
#define FILEZILLA_COMMONUI_RECURSIVE_OPERATION_HEADER

#include "filter.h"
#include "site.h"

class FZCUI_PUBLIC_SYMBOL recursive_operation
{
public:
	virtual ~recursive_operation() = default;

	enum OperationMode {
		recursive_none,
		recursive_transfer,
		recursive_transfer_flatten,
		recursive_delete,
		recursive_chmod,
		recursive_list,
	};

	bool IsActive() const { return GetOperationMode() != recursive_none; }
	OperationMode GetOperationMode() const { return m_operationMode; }
	int64_t GetProcessedFiles() const { return m_processedFiles; }
	int64_t GetProcessedDirectories() const { return m_processedDirectories; }

	virtual void StopRecursiveOperation() = 0;

	bool Failed() const { return m_failed; }

protected:
	uint64_t m_processedFiles{};
	uint64_t m_processedDirectories{};

	OperationMode m_operationMode{recursive_none};
	ActiveFilters m_filters;
	bool m_failed{};
};

#endif
