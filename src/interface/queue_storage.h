#ifndef FILEZILLA_INTERFACE_QUEUE_STORAGE_HEADER
#define FILEZILLA_INTERFACE_QUEUE_STORAGE_HEADER

#include <vector>
#include <stdint.h>
#include <string>

class CFileItem;
class COptionsBase;
class CServerItem;
class Site;
class login_manager;

class CQueueStorage final
{
	class Impl;

public:
	CQueueStorage(COptionsBase & options, login_manager & lim);
	~CQueueStorage();

	CQueueStorage(CQueueStorage const&) = delete;
	CQueueStorage& operator=(CQueueStorage const&) = delete;

	// Call before loading
	bool BeginTransaction();

	// Call after finishing loading
	bool EndTransaction(bool rollback = false);

	bool Clear(); // Also clears caches

	bool Vacuum();

	bool SaveQueue(std::vector<CServerItem*> const& queue);

	// > 0 = server id
	//   0 = No server
	// < 0 = failure.
	int64_t GetServer(Site& site, bool fromBeginning);

	int64_t GetFile(CFileItem** pItem, int64_t server);

	std::wstring GetDatabaseFilename();

private:
	Impl* d_;
};

#endif
