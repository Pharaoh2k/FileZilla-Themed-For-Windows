#include "filezilla.h"

#include "logging_private.h"

#include "../include/engine_options.h"

class CLoggingOptionsChanged final : public fz::event_handler
{
public:
	CLoggingOptionsChanged(CLogging& logger, COptionsBase& options, fz::event_loop& loop)
		: fz::event_handler(loop)
		, logger_(logger)
		, options_(options)
	{
		logger_.UpdateLogLevel(options_);
		options_.watch(OPTION_LOGGING_DEBUGLEVEL, this);
		options_.watch(OPTION_LOGGING_RAWLISTING, this);
	}

	virtual ~CLoggingOptionsChanged()
	{
		options_.unwatch_all(this);
		remove_handler();
	}

	virtual void operator()(const fz::event_base&)
	{
		 // In worker thread
		logger_.UpdateLogLevel(options_);
	}

	CLogging & logger_;
	COptionsBase& options_;
};

CLogging::CLogging(CFileZillaEnginePrivate & engine, logfile_writer & writer)
	: engine_(engine)
	, writer_(writer)
{
	UpdateLogLevel(engine.GetOptions());
	optionChangeHandler_ = std::make_unique<CLoggingOptionsChanged>(*this, engine_.GetOptions(), engine.event_loop_);
}

CLogging::~CLogging()
{
}

void CLogging::UpdateLogLevel(COptionsBase & options)
{
	logmsg::type enabled{};
	switch (options.get_int(OPTION_LOGGING_DEBUGLEVEL)) {
	case 1:
		enabled = logmsg::debug_warning;
		break;
	case 2:
		enabled = static_cast<logmsg::type>(logmsg::debug_warning | logmsg::debug_info);
		break;
	case 3:
		enabled = static_cast<logmsg::type>(logmsg::debug_warning | logmsg::debug_info | logmsg::debug_verbose);
		break;
	case 4:
		enabled = static_cast<logmsg::type>(logmsg::debug_warning | logmsg::debug_info | logmsg::debug_verbose | logmsg::debug_debug);
		break;
	default:
		break;
	}
	if (options.get_int(OPTION_LOGGING_RAWLISTING) != 0) {
		enabled = static_cast<logmsg::type>(enabled | logmsg::listing);
	}

	logmsg::type disabled{ (logmsg::debug_warning | logmsg::debug_info | logmsg::debug_verbose | logmsg::debug_debug | logmsg::listing) & ~enabled };

	enable(enabled);
	disable(disabled);
}
