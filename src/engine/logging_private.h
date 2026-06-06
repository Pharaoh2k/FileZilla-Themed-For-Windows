#ifndef FILEZILLA_ENGINE_LOGGING_PRIVATE_HEADER
#define FILEZILLA_ENGINE_LOGGING_PRIVATE_HEADER

#include "engineprivate.h"
#include "../include/logfile_writer.h"

class CLoggingOptionsChanged;

class CLogging : public fz::logger_interface
{
public:
	explicit CLogging(CFileZillaEnginePrivate & engine, logfile_writer & writer);
	virtual ~CLogging();

	CLogging(CLogging const&) = delete;
	CLogging& operator=(CLogging const&) = delete;

	virtual void do_log(logmsg::type t, std::wstring&& msg) override final {
		auto now = fz::datetime::now();
		writer_.log(t, msg, now, engine_.GetEngineId(), this);
		engine_.AddLogNotification(std::make_unique<CLogmsgNotification>(t, msg, now));
	}
	
	void UpdateLogLevel(COptionsBase & options);

private:
	CFileZillaEnginePrivate & engine_;
	logfile_writer & writer_;

	std::unique_ptr<CLoggingOptionsChanged> optionChangeHandler_;

};

#endif
