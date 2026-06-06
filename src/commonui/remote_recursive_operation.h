#ifndef FILEZILLA_COMMONUI_REMOTE_RECURSIVE_OPERATION_HEADER
#define FILEZILLA_COMMONUI_REMOTE_RECURSIVE_OPERATION_HEADER

#include "../include/commands.h"
#include "../include/local_path.h"
#include "../include/serverpath.h"
#include "../include/directorylisting.h"

#include "chmod_data.h"
#include "filter.h"
#include "recursive_operation.h"
#include "visibility.h"

#include <deque>
#include <memory>
#include <set>
#include <string>

class ChmodData;

class FZCUI_PUBLIC_SYMBOL recursion_root final
{
public:
	recursion_root() = default;
	recursion_root(CServerPath const& start_dir, bool allow_parent);

	void add_dir_to_visit(CServerPath const& path, std::wstring const& subdir, CLocalPath const& localDir = CLocalPath(), bool is_link = false, bool recurse = true);

	// Queue a directory but restrict processing to the named subdirectory
	void add_dir_to_visit_restricted(CServerPath const& path, std::wstring const& restricted, bool recurse);

	bool empty() const { return m_dirsToVisit.empty() || m_remoteStartDir.empty(); }

private:
	friend class remote_recursive_operation;

	class new_dir final
	{
	public:
		CServerPath parent;
		std::wstring subdir;
		CLocalPath localDir;
		fz::sparse_optional<std::wstring> restricted;

		// Symlink target might be outside actual start dir. Yet
		// sometimes user wants to download symlink target contents
		CServerPath start_dir;

		// 0 = not a link
		// 1 = link, added by class during the operation
		// 2 = link, added by user of class
		int link{};

		bool doVisit{true};
		bool recurse{true};
		bool second_try{};
	};

	CServerPath m_remoteStartDir;
	std::set<CServerPath> m_visitedDirs;
	std::deque<new_dir> m_dirsToVisit;
	bool m_allowParent{};
};

class FZCUI_PUBLIC_SYMBOL remote_recursive_operation : public recursive_operation
{
public:
	remote_recursive_operation();
	virtual ~remote_recursive_operation();

	void AddRecursionRoot(recursion_root&& root);
	void start_recursive_operation(OperationMode mode, ActiveFilters const& filters, bool refresh_listings);

	// Needed for recursive_chmod
	void SetChmodData(ChmodData const& chmodData);

	virtual void StopRecursiveOperation();

protected:
	// called by start_recursive_operation to do init by derived class and then it should call this based class func
	virtual void do_start_recursive_operation(OperationMode mode, ActiveFilters const& filters);

	// process engine command needed by the recursive operation (list, remove etc). Takes ownership of the pCommand.
	virtual void process_command(std::unique_ptr<CCommand>) = 0;

	// called when the whole recursive operation finished successfully to cleanup
	virtual void operation_finished() = 0;

	// make filename sane, so it can be used locally
	virtual std::wstring sanitize_filename(std::wstring const& name) = 0;

	// called for files when doing recursive_transfer(_flatten)
	virtual void handle_file(std::wstring const& sourceFile, CLocalPath const& localPath, CServerPath const& remotePath, int64_t size) = 0;

	// called when encountering empty directory for recursive_transfer
	virtual void handle_empty_directory(CLocalPath const& localPath) = 0;

	// called from LinkIsNotDir when not listing or deleting
	virtual void handle_invalid_dir_link(std::wstring const& sourceFile, CLocalPath const& localPath, CServerPath const& remotePath) = 0;

	// called after looping through directory (non-recursively) to allow status updates et al.
	virtual void handle_dir_listing_end() = 0;

	// called after non-recoverable listing failure
	virtual void handle_listing_failed() = 0;

	// Call this when engine indicates that link was tried to be listed as directory but is not one
	void LinkIsNotDir(Site const& site);

	// Call this when engine indicates listing failed, tries to recover
	void ListingFailed(int error);

	// Processes the directory listing in case of a recursive operation
	void ProcessDirectoryListing(CDirectoryListing const* pDirectoryListing);

protected:
	void process_entries(recursion_root& root, const CDirectoryListing* pDirectoryListing
		, recursion_root::new_dir const& dir, std::wstring const& remotePath);

	bool NextOperation();
	bool BelowRecursionRoot(CServerPath const& path, recursion_root::new_dir &dir);

	std::deque<recursion_root> recursion_roots_;

	// Needed for recursive_chmod
	std::optional<ChmodData> chmodData_;

	int listFlags_{};
};

#endif
