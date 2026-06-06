#include "../include/logfile_writer.h"
#include "../include/engine_options.h"

#include "../include/logging.h"
#include "../include/misc.h"

#include <libfilezilla/translate.hpp>
#include <libfilezilla/util.hpp>

#ifndef FZ_WINDOWS
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#endif

logfile_writer::logfile_writer(COptionsBase & options, fz::event_loop & loop)
    : fz::event_handler(loop)
    , options_(options)
#if FZ_WINDOWS
    , pid_(static_cast<unsigned int>(GetCurrentProcessId()))
#else
    , pid_(static_cast<unsigned int>(getpid()))
#endif
{
	options.watch(OPTION_LOGGING_FILE, this);
	options.watch(OPTION_LOGGING_FILE_SIZELIMIT, this);
}

logfile_writer::~logfile_writer()
{
	remove_handler();
	options_.unwatch_all(this);
}

void logfile_writer::operator()(fz::event_base const&)
{
	fz::scoped_lock l(mtx_);
	file_.close();
	initialized_ = false;
}

bool logfile_writer::rotate(fz::scoped_lock & l, fz::logger_interface * error_logger)
{
#ifdef FZ_WINDOWS
	if (max_size_ && max_size_ < file_.size()) {
		file_.close();

		// m_log_fd might no longer be the original file.
		// Recheck on a new handle. Proteced with a mutex against other processes
		HANDLE hMutex = ::CreateMutexW(nullptr, true, L"FileZilla 3 Logrotate Mutex");
		if (!hMutex) {
			DWORD err = GetLastError();
			l.unlock();
			if (error_logger) {
				error_logger->log(fz::logmsg::error, fztranslate("Could not create logging mutex: %s"), GetSystemErrorDescription(err));
			}
			return false;
		}

		fz::native_string name = fz::to_native(options_.get_string(OPTION_LOGGING_FILE));
		if (!do_open(l, error_logger, name)) {
			// Oh dear..
			ReleaseMutex(hMutex);
			CloseHandle(hMutex);
			return false;
		}

		bool ret{true};

		if (file_.size()  > max_size_) {
			file_.close();

			// MoveFileEx can fail if trying to access a deleted file for which another process still has
			// a handle. Move it far away first.
			// Todo: Handle the case in which logdir and tmpdir are on different volumes.
			// (Why is everthing so needlessly complex on MSW?)

			wchar_t tempDir[MAX_PATH + 1];
			DWORD res = GetTempPath(MAX_PATH, tempDir);
			if (res && res <= MAX_PATH) {
				tempDir[MAX_PATH] = 0;

				wchar_t tempFile[MAX_PATH + 1];
				res = GetTempFileNameW(tempDir, L"fz3", 0, tempFile);
				if (res) {
					tempFile[MAX_PATH] = 0;
					MoveFileExW((name + L".1").c_str(), tempFile, MOVEFILE_REPLACE_EXISTING);
					DeleteFileW(tempFile);
				}
			}
			MoveFileExW(name.c_str(), (name + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
			ret = do_open(l, error_logger, name, true);
		}

		ReleaseMutex(hMutex);
		CloseHandle(hMutex);

		return ret;
	}
#else
	while (max_size_ && max_size_ < file_.size()) {
		struct flock lock = {};
		lock.l_type = F_WRLCK;
		lock.l_whence = SEEK_SET;
		lock.l_start = 0;
		lock.l_len = 1;

		int old_fd = file_.detach(); // Keep it for now for the advisory lock

		// Retry through signals
		int rc{};
		while ((rc = fcntl(old_fd, F_SETLKW, &lock)) == -1 && errno == EINTR);


		struct stat buf;
		rc = fstat(old_fd, &buf);

		fz::native_string name = fz::to_native(options_.get_string(OPTION_LOGGING_FILE));
		if (!do_open(l, error_logger, name)) {
			close(old_fd);
			return false;
		}

		struct stat buf2;
		int rc2 = fstat(file_.fd(), &buf2);

		// Check if inodes are different, if so someone else already has rotated
		if (!rc && !rc2 && buf.st_ino != buf2.st_ino) {
			close(old_fd); // Releases the lock
			continue;
		}

		// At this point, the file is indeed the log file and we are holding a lock on it.

		// Rename it
		rename(name.c_str(), (name + ".1").c_str());

		// Closing any descriptor releases the lock, hence keep this also until after creation
		int old_fd2 = file_.detach();
		bool ret = do_open(l, error_logger, name, true);

		close(old_fd2);
		close(old_fd);

		if (!ret) {
			return false;
		}
	}
#endif
	return true;
}

void logfile_writer::log(fz::logmsg::type type, std::wstring const& msg, fz::datetime const& now, size_t id, fz::logger_interface * error_logger)
{
	fz::scoped_lock l(mtx_);

	if (!file_.opened()) {
		if (initialized_ || !init(l, error_logger)) {
			return;
		}
	}

	if (!rotate(l, error_logger)) {
		return;
	}

	std::string out;
	if (id) {
		out = fz::sprintf("%s %u %u %s %s"
#ifdef FZ_WINDOWS
		"\r\n",
#else
		"\n",
#endif
		now.format("%Y-%m-%d %H:%M:%S", fz::datetime::local), pid_, id, prefixes_[fz::bitscan_reverse(type)], fz::to_utf8(msg));
	}
	else {
		out = fz::sprintf("%s %u %s %s"
#ifdef FZ_WINDOWS
		"\r\n",
#else
		"\n",
#endif
		now.format("%Y-%m-%d %H:%M:%S", fz::datetime::local), pid_, prefixes_[fz::bitscan_reverse(type)], fz::to_utf8(msg));
	}

	std::string_view o = out;
	while (!o.empty()) {
		fz::rwresult r = file_.write2(o.data(), o.size());
		if (!r || !r.value_) {
			file_.close();
			break;
		}
		o.remove_prefix(r.value_);
	}
}

bool logfile_writer::init(fz::scoped_lock & l, fz::logger_interface * error_logger)
{
	[[maybe_unused]] static bool const once = [&]() {
		prefixes_[fz::bitscan_reverse(fz::logmsg::status)] = fz::to_utf8(fztranslate("Status:"));
		prefixes_[fz::bitscan_reverse(fz::logmsg::error)] = fz::to_utf8(fztranslate("Error:"));
		prefixes_[fz::bitscan_reverse(fz::logmsg::command)] = fz::to_utf8(fztranslate("Command:"));
		prefixes_[fz::bitscan_reverse(fz::logmsg::reply)] = fz::to_utf8(fztranslate("Response:"));
		prefixes_[fz::bitscan_reverse(fz::logmsg::debug_warning)] = fz::to_utf8(fztranslate("Trace:"));
		prefixes_[fz::bitscan_reverse(fz::logmsg::debug_info)] = prefixes_[fz::bitscan_reverse(fz::logmsg::debug_warning)];
		prefixes_[fz::bitscan_reverse(fz::logmsg::debug_verbose)] = prefixes_[fz::bitscan_reverse(fz::logmsg::debug_warning)];
		prefixes_[fz::bitscan_reverse(fz::logmsg::debug_debug)] = prefixes_[fz::bitscan_reverse(fz::logmsg::debug_warning)];
		prefixes_[fz::bitscan_reverse(logmsg::listing)] = fz::to_utf8(fztranslate("Listing:"));
		return true;
	}();

	initialized_ = true;

	fz::native_string name = fz::to_native(options_.get_string(OPTION_LOGGING_FILE));
	if (!do_open(l, error_logger, name)) {
		return false;
	}

	max_size_ = options_.get_int(OPTION_LOGGING_FILE_SIZELIMIT);
	max_size_ *= 1024 * 1024;

	return true;
}

bool logfile_writer::do_open(fz::scoped_lock & l, fz::logger_interface * error_logger, fz::native_string const& name, bool empty)
{
	file_.close();

	if (name.empty()) {
		return false;
	}

	fz::result r = file_.open(name, fz::file::appending, empty ? fz::file::empty : fz::file::existing);
	if (!r) {
		l.unlock();
		if (error_logger) {
			error_logger->log(fz::logmsg::error, fztranslate("Could not open log file for writing."));
		}
		return false;
	}
	return true;
}
