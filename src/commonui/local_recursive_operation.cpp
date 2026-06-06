#include "local_recursive_operation.h"

#include <libfilezilla/local_filesys.hpp>

local_recursive_operation::local_recursive_operation()
{}

local_recursive_operation::local_recursive_operation(fz::thread_pool& pool)
: pool_(&pool)
{
}

void local_recursion_root::add_dir_to_visit(CLocalPath const& localPath, CServerPath const& remotePath, bool recurse)
{
	new_dir dirToVisit;
	dirToVisit.localPath = localPath;
	dirToVisit.remotePath = remotePath;
	dirToVisit.recurse = recurse;
	m_dirsToVisit.push_back(dirToVisit);
}

local_recursive_operation::~local_recursive_operation()
{
}

void local_recursive_operation::AddRecursionRoot(local_recursion_root&& root)
{
	if (!root.empty()) {
		fz::scoped_lock l(mutex_);
		recursion_roots_.push_back(std::move(root));
	}
}

bool local_recursive_operation::start_recursive_operation(OperationMode mode, ActiveFilters const& filters, bool ignore_links)
{
	if (!do_start_recursive_operation(mode, filters, ignore_links)) {
		StopRecursiveOperation();
		return false;
	}
	return true;
}

bool local_recursive_operation::do_start_recursive_operation(OperationMode mode, ActiveFilters const& filters, bool ignore_links)
{
	fz::scoped_lock l(mutex_);

	if (m_operationMode != recursive_none) {
		return false;
	}

	if (mode == recursive_chmod) {
		return false;
	}

	if (recursion_roots_.empty()) {
		// Nothing to do in this case
		return false;
	}

	m_processedFiles = 0;
	m_processedDirectories = 0;

	m_operationMode = mode;

	m_filters = filters;
	m_ignoreLinks = ignore_links;

	if (pool_) {
		thread_ = pool_->spawn([this] { thread_entry(); });
		if (!thread_) {
			m_operationMode = recursive_none;
			return false;
		}
	}

	return true;
}

void local_recursive_operation::StopRecursiveOperation()
{
	{
		fz::scoped_lock l(mutex_);
		if (m_operationMode == recursive_none) {
			return;
		}

		m_operationMode = recursive_none;
		recursion_roots_.clear();

		m_processedFiles = 0;
		m_processedDirectories = 0;

	}

	thread_.join();
	m_listedDirectories.clear();
}

void local_recursive_operation::EnqueueEnumeratedListing(fz::scoped_lock& l, listing&& d, bool recurse)
{
	if (recursion_roots_.empty()) {
		return;
	}

	auto& root = recursion_roots_.front();

	if (recurse) {
		// Queue for recursion
		for (auto const& entry : d.dirs) {
			local_recursion_root::new_dir dir;
			CLocalPath localSub = d.localPath;
			localSub.AddSegment(entry.name);

			CServerPath remoteSub = d.remotePath;
			if (!remoteSub.empty()) {
				if (m_operationMode == recursive_transfer) {
					// Non-flatten case
					remoteSub.AddSegment(entry.name);
				}
			}
			root.add_dir_to_visit(localSub, remoteSub);
		}
	}

	m_listedDirectories.emplace_back(std::move(d));

	// Hand off to GUI thread
	if (m_listedDirectories.size() == 1) {
		l.unlock();
		on_listed_directory();
		l.lock();
	}
}

void local_recursive_operation::thread_entry()
{
	{
		fz::scoped_lock l(mutex_);

		// Make copy, as it is used in the unlocked section
		auto filters = m_filters.first;

		while (!recursion_roots_.empty()) {
			listing d;
			bool recurse{true};

			{
				auto& root = recursion_roots_.front();
				if (root.m_dirsToVisit.empty()) {
					recursion_roots_.pop_front();
					continue;
				}

				auto const& dir = root.m_dirsToVisit.front();
				d.localPath = dir.localPath;
				d.remotePath = dir.remotePath;
				recurse = dir.recurse;

				root.m_dirsToVisit.pop_front();
			}

			// Do the slow part without holding mutex
			l.unlock();

			bool sentPartial = false;
			fz::local_filesys fs;
			fz::native_string localPath = fz::to_native(d.localPath.GetPath());

			if (fs.begin_find_files(localPath)) {
				listing::entry entry;
				bool isLink{};
				fz::native_string name;
				fz::local_filesys::type t{};
				while (fs.get_next_file(name, isLink, t, &entry.size, &entry.time, &entry.attributes)) {
					if (isLink && m_ignoreLinks) {
						continue;
					}
					entry.name = fz::to_wstring(name);

					if (!filter_manager::FilenameFiltered(filters, entry.name, d.localPath.GetPath(), t == fz::local_filesys::dir, entry.size, entry.attributes, entry.time)) {
						entry.flags = (t == fz::local_filesys::type::dir) ? listing::entry::flag_dir : 0;
						if (isLink) {
							entry.flags |= listing::entry::flag_link;
						}

						if (t == fz::local_filesys::dir) {
							d.dirs.emplace_back(std::move(entry));
						}
						else {
							d.files.emplace_back(std::move(entry));
						}

						// If having queued 5k items, hand off to main thread.
						if (d.files.size() + d.dirs.size() >= 5000) {
							sentPartial = true;

							listing next;
							next.localPath = d.localPath;
							next.remotePath = d.remotePath;

							l.lock();
							// Check for cancellation
							if (recursion_roots_.empty()) {
								l.unlock();
								break;
							}
							EnqueueEnumeratedListing(l, std::move(d), recurse);
							l.unlock();
							d = next;
						}
					}
				}
			}
			else {
				on_listing_failed();
			}

			l.lock();
			// Check for cancellation
			if (recursion_roots_.empty()) {
				break;
			}
			if (!sentPartial || !d.files.empty() || !d.dirs.empty()) {
				EnqueueEnumeratedListing(l, std::move(d), recurse);
			}
		}

		listing d;
		m_listedDirectories.emplace_back(std::move(d));
	}

	on_listed_directory();
}

