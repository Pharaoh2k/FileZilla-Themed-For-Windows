#ifndef FILEZILLA_COMMONUI_LOCAL_RECURSIVE_OPERATION_HEADER
#define FILEZILLA_COMMONUI_LOCAL_RECURSIVE_OPERATION_HEADER

#include "../include/local_path.h"
#include "../include/serverpath.h"

#include "recursive_operation.h"
#include "visibility.h"

#include <libfilezilla/mutex.hpp>
#include <libfilezilla/thread_pool.hpp>
#include <libfilezilla/time.hpp>

#include <deque>
#include <set>
#include <string>

class FZCUI_PUBLIC_SYMBOL local_recursion_root final
{
public:
	local_recursion_root() = default;

	void add_dir_to_visit(CLocalPath const& localPath, CServerPath const& remotePath = CServerPath(), bool recurse = true);

	bool empty() const { return m_dirsToVisit.empty(); }

private:
	friend class local_recursive_operation;

	class new_dir final
	{
	public:
		CLocalPath localPath;
		CServerPath remotePath;
		bool recurse{true};
	};

	std::set<CLocalPath> m_visitedDirs;
	std::deque<new_dir> m_dirsToVisit;
};

class FZCUI_PUBLIC_SYMBOL local_recursive_operation : public recursive_operation
{
public:
	class listing final
	{
	public:
		class entry
		{
		public:
			std::wstring name;
			int64_t size{};
			fz::datetime time;
			int attributes{};

			enum : uint8_t {
				flag_dir = 0x1u,
				flag_link = 0x2u
			};
			uint8_t flags{};
			bool is_dir() const { return flags & flag_dir; }
			bool is_link() const { return flags & flag_link; }
		};

		std::vector<entry> files;
		std::vector<entry> dirs;
		CLocalPath localPath;
		CServerPath remotePath;
	};

	// when default constructed, thread_entry must be called to process the files
	local_recursive_operation();
	// spawns async task when start_recursive_operation called to process the files
	local_recursive_operation(fz::thread_pool& pool);
	virtual ~local_recursive_operation();

	void AddRecursionRoot(local_recursion_root&& root);
	bool start_recursive_operation(OperationMode mode, ActiveFilters const& filters, bool ignore_links = true);

	virtual void StopRecursiveOperation() override;

	// thread entry point for processing files
	void thread_entry();

protected:
	// called by start_recursive_operation for the derived to initialise and call this base class func
	virtual bool do_start_recursive_operation(OperationMode mode, ActiveFilters const& filters, bool ignore_links);

	// called when there are new entries to be processed
	virtual void on_listed_directory() = 0;

	// called after non-recoverable listing failure
	virtual void on_listing_failed() = 0;

protected:
	void EnqueueEnumeratedListing(fz::scoped_lock& l, listing&& d, bool recurse);

	std::deque<local_recursion_root> recursion_roots_;

	fz::mutex mutex_;
	fz::thread_pool* pool_{};

	std::deque<listing> m_listedDirectories;
	bool m_ignoreLinks{};

	fz::async_task thread_;
};

#endif
