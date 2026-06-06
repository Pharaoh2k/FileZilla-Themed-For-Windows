#include "remote_recursive_operation.h"
#include "chmod_data.h"
#include "filter.h"

#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/recursive_remove.hpp>

recursion_root::recursion_root(CServerPath const& start_dir, bool allow_parent)
	: m_remoteStartDir(start_dir)
	, m_allowParent(allow_parent)
{
}

void recursion_root::add_dir_to_visit(CServerPath const& path, std::wstring const& subdir, CLocalPath const& localDir, bool is_link, bool recurse)
{
	new_dir dirToVisit;

	dirToVisit.localDir = localDir;
	dirToVisit.parent = path;
	dirToVisit.recurse = recurse;
	dirToVisit.subdir = subdir;
	dirToVisit.link = is_link ? 2 : 0;
	m_dirsToVisit.push_back(dirToVisit);
}

void recursion_root::add_dir_to_visit_restricted(CServerPath const& path, std::wstring const& restricted, bool recurse)
{
	new_dir dirToVisit;
	dirToVisit.parent = path;
	dirToVisit.recurse = recurse;
	if (!restricted.empty()) {
		dirToVisit.restricted = fz::sparse_optional<std::wstring>(restricted);
	}
	m_dirsToVisit.push_back(dirToVisit);
}

remote_recursive_operation::remote_recursive_operation()
{
}

remote_recursive_operation::~remote_recursive_operation()
{
}

void remote_recursive_operation::AddRecursionRoot(recursion_root && root)
{
	if (!root.empty()) {
		recursion_roots_.push_back(std::forward<recursion_root>(root));
	}
}

void remote_recursive_operation::start_recursive_operation(OperationMode mode, ActiveFilters const& filters, bool refrshListings)
{
	m_failed = false;

	if (m_operationMode != recursive_none) {
		return;
	}
	if (mode == recursive_chmod && !chmodData_) {
		return;
	}
	if (recursion_roots_.empty()) {
		// Nothing to do in this case
		return;
	}

	m_processedFiles = 0;
	m_processedDirectories = 0;

	m_operationMode = mode;

	listFlags_ = refrshListings ? LIST_FLAG_REFRESH : 0;

	do_start_recursive_operation(mode, filters);
}

void remote_recursive_operation::do_start_recursive_operation(OperationMode, ActiveFilters const& filters)
{
	m_filters = filters;
	NextOperation();
}

bool remote_recursive_operation::NextOperation()
{
	if (m_operationMode == recursive_none) {
		return false;
	}

	while (!recursion_roots_.empty()) {
		auto & root = recursion_roots_.front();
		while (!root.m_dirsToVisit.empty()) {
			const recursion_root::new_dir& dirToVisit = root.m_dirsToVisit.front();

			if (m_operationMode == recursive_delete && !dirToVisit.doVisit && dirToVisit.recurse) {
				process_command(std::make_unique<CRemoveDirCommand>(dirToVisit.parent, dirToVisit.subdir));
				root.m_dirsToVisit.pop_front();
				continue;
			}

			process_command(std::make_unique<CListCommand>(dirToVisit.parent, dirToVisit.subdir, listFlags_ | (dirToVisit.link ? LIST_FLAG_LINK : 0)));
			return true;
		}

		recursion_roots_.pop_front();
	}

	StopRecursiveOperation();
	operation_finished();

	return false;
}

bool remote_recursive_operation::BelowRecursionRoot(CServerPath const& path, recursion_root::new_dir &dir)
{
	if (!dir.start_dir.empty()) {
		if (path.IsSubdirOf(dir.start_dir, false)) {
			return true;
		}
		else {
			return false;
		}
	}

	auto & root = recursion_roots_.front();
	if (path.IsSubdirOf(root.m_remoteStartDir, false)) {
		return true;
	}

	// In some cases (chmod from tree for example) it is necessary to list the
	// parent first
	if (path == root.m_remoteStartDir && root.m_allowParent) {
		return true;
	}

	if (dir.link == 2) {
		dir.start_dir = path;
		return true;
	}

	return false;
}

void remote_recursive_operation::process_entries(recursion_root& root, CDirectoryListing const* pDirectoryListing
	, recursion_root::new_dir const& dir, std::wstring const& remotePath)
{
	std::vector<std::wstring> filesToDelete;
	bool const restricted = static_cast<bool>(dir.restricted);

	for (size_t i = pDirectoryListing->size(); i > 0; --i) {
		const CDirentry& entry = (*pDirectoryListing)[i - 1];

		if (restricted) {
			if (entry.name != *dir.restricted) {
				continue;
			}
		}
		else if (filter_manager::FilenameFiltered(m_filters.second, entry.name, remotePath, entry.is_dir(), entry.size, 0, entry.time)) {
			continue;
		}

		if (!entry.is_dir()) {
			++m_processedFiles;
		}

		if (entry.is_dir() && (!entry.is_link() || m_operationMode != recursive_delete)) {
			if (dir.recurse) {
				recursion_root::new_dir dirToVisit;
				dirToVisit.parent = pDirectoryListing->path;
				dirToVisit.subdir = entry.name;
				dirToVisit.localDir = dir.localDir;
				dirToVisit.start_dir = dir.start_dir;

				if (m_operationMode == recursive_transfer) {
					// Non-flatten case
					dirToVisit.localDir.AddSegment(sanitize_filename(entry.name));
				}
				if (entry.is_link()) {
					dirToVisit.link = 1;
					dirToVisit.recurse = false;
				}
				root.m_dirsToVisit.push_front(dirToVisit);
			}
		}
		else {
			switch (m_operationMode)
			{
			case recursive_transfer:
			case recursive_transfer_flatten:
				{
					handle_file(entry.name,	dir.localDir, pDirectoryListing->path, entry.size);
				}
				break;
			case recursive_delete:
				filesToDelete.push_back(entry.name);
				break;
			default:
				break;
			}
		}

		if (m_operationMode == recursive_chmod && chmodData_) {
			if (	(!entry.is_dir() && chmodData_->apply_files_) ||
				(entry.is_dir() && chmodData_->apply_dirs_))
			{
				posix_permissions newPerms = chmodData_->Apply(fz::to_utf8(*entry.permissions), entry.is_dir());
				process_command(std::make_unique<CChmodCommand>(pDirectoryListing->path, entry.name, fz::to_wstring_from_utf8(to_octal(newPerms, true))));
			}
		}
	}

	if (m_operationMode == recursive_delete && !filesToDelete.empty()) {
		process_command(std::make_unique<CDeleteCommand>(pDirectoryListing->path, std::move(filesToDelete)));
	}
}

void remote_recursive_operation::ProcessDirectoryListing(CDirectoryListing const* pDirectoryListing)
{
	if (!pDirectoryListing) {
		StopRecursiveOperation();
		return;
	}

	if (m_operationMode == recursive_none || recursion_roots_.empty()) {
		return;
	}

	if (pDirectoryListing->failed()) {
		// Ignore this.
		// It will get handled by the failed command in ListingFailed
		return;
	}

	auto & root = recursion_roots_.front();
	if (root.m_dirsToVisit.empty()) {
		StopRecursiveOperation();
		return;
	}

	recursion_root::new_dir dir = root.m_dirsToVisit.front();
	root.m_dirsToVisit.pop_front();

	if (!BelowRecursionRoot(pDirectoryListing->path, dir)) {
		NextOperation();
		return;
	}

	if (m_operationMode == recursive_delete && dir.doVisit && dir.recurse && !dir.subdir.empty()) {
		// After recursing into directory to delete its contents, delete directory itself
		// Gets handled in NextOperation
		recursion_root::new_dir dir2 = dir;
		dir2.doVisit = false;
		root.m_dirsToVisit.push_front(dir2);
	}

	if (dir.link && !dir.recurse) {
		NextOperation();
		return;
	}

	// Check if we have already visited the directory
	if (!root.m_visitedDirs.insert(pDirectoryListing->path).second) {
		NextOperation();
		return;
	}
	++m_processedDirectories;

	if (!pDirectoryListing->size() && m_operationMode == recursive_transfer) {
		handle_empty_directory(dir.localDir);
	}
	else {
		// Is operation restricted to a single child?
		std::wstring const remotePath = pDirectoryListing->path.GetPath();
		process_entries(root, pDirectoryListing, dir, pDirectoryListing->path.GetPath());
	}

	handle_dir_listing_end();
	NextOperation();
}

void remote_recursive_operation::SetChmodData(ChmodData const& chmodData)
{
	chmodData_ = chmodData;
}

void remote_recursive_operation::StopRecursiveOperation()
{
	if (m_operationMode != recursive_none) {
		m_operationMode = recursive_none;
	}
	recursion_roots_.clear();
	chmodData_.reset();
}

void remote_recursive_operation::ListingFailed(int error)
{
	m_failed = true;

	if (m_operationMode == recursive_none || recursion_roots_.empty()) {
		return;
	}

	if ((error & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
		// User has cancelled operation
		StopRecursiveOperation();
		return;
	}

	auto & root = recursion_roots_.front();
	if (root.m_dirsToVisit.empty()) {
		StopRecursiveOperation();
		return;
	}

	recursion_root::new_dir dir = root.m_dirsToVisit.front();
	root.m_dirsToVisit.pop_front();
	if ((error & FZ_REPLY_CRITICALERROR) != FZ_REPLY_CRITICALERROR && !dir.second_try) {
		// Retry, could have been a temporary socket creating failure
		// (e.g. hitting a blocked port) or a disconnect (e.g. no-filetransfer-timeout)
		dir.second_try = true;
		root.m_dirsToVisit.push_front(dir);
	}
	else {
		if (m_operationMode == recursive_delete && dir.doVisit && dir.recurse && !dir.subdir.empty()) {
			// After recursing into directory to delete its contents, delete directory itself
			// Gets handled in NextOperation
			recursion_root::new_dir dir2 = dir;
			dir2.doVisit = false;
			root.m_dirsToVisit.push_front(dir2);
		}
		if (m_operationMode == recursive_list) {
			handle_listing_failed();
		}
	}

	NextOperation();
}

void remote_recursive_operation::LinkIsNotDir(Site const& site)
{
	if (m_operationMode == recursive_none || recursion_roots_.empty()) {
		return;
	}

	auto & root = recursion_roots_.front();
	if (root.m_dirsToVisit.empty()) {
		StopRecursiveOperation();
		return;
	}

	recursion_root::new_dir dir = root.m_dirsToVisit.front();
	root.m_dirsToVisit.pop_front();

	if (!site) {
		NextOperation();
		return;
	}

	if (m_operationMode == recursive_delete) {
		if (!dir.subdir.empty()) {
			std::vector<std::wstring> files;
			files.push_back(dir.subdir);
			process_command(std::make_unique<CDeleteCommand>(dir.parent, std::move(files)));
		}
	}
	else if (m_operationMode != recursive_list) {
		CLocalPath localPath = dir.localDir;
		std::wstring localFile = dir.subdir;
		if (m_operationMode != recursive_transfer_flatten) {
			localPath.MakeParent();
		}
		handle_invalid_dir_link(dir.subdir, localPath, dir.parent);
	}

	NextOperation();
}
