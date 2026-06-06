#ifndef FILEZILLA_ENGINE_LOGFILE_WRITER
#define FILEZILLA_ENGINE_LOGFILE_WRITER

#include "optionsbase.h"

#include <libfilezilla/event_handler.hpp>
#include <libfilezilla/file.hpp>
#include <libfilezilla/logger.hpp>

class logfile_writer final : public fz::event_handler
{
public:
	logfile_writer(COptionsBase & options, fz::event_loop & loop);
	~logfile_writer();

	void FZC_PUBLIC_SYMBOL log(fz::logmsg::type type, std::wstring const& msg, fz::datetime const& now, size_t id = 0, fz::logger_interface * error_logger = nullptr);

private:
	bool init(fz::scoped_lock & l, fz::logger_interface * error_logger);
	bool rotate(fz::scoped_lock & l, fz::logger_interface * error_logger);
	bool do_open(fz::scoped_lock & l, fz::logger_interface * error_logger, fz::native_string const& name, bool empty = false);

	void operator()(fz::event_base const& ev);
	COptionsBase & options_;

	fz::mutex mtx_{false};
	fz::file file_;
	bool initialized_{};

	std::string prefixes_[sizeof(fz::logmsg::type) * 8];

	unsigned int const pid_;
	int64_t max_size_{};
};

#endif
