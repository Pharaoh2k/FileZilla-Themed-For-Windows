#include "filezilla.h"

#include "../include/activity_logger.h"
#include "../include/engine_context.h"
#include "../include/engine_options.h"
#include "../include/logfile_writer.h"
#include "../include/sizeformatting.h"

#include "directorycache.h"
#include "logging_private.h"
#include "oplock_manager.h"
#include "pathcache.h"

#include <libfilezilla/event_loop.hpp>
#include <libfilezilla/rate_limiter.hpp>
#include <libfilezilla/thread_pool.hpp>
#include <libfilezilla/tls_system_trust_store.hpp>

namespace {
class option_change_handler final : public fz::event_handler
{
public:
	option_change_handler(COptionsBase& options, fz::event_loop & loop, fz::rate_limit_manager & rate_limit_mgr, fz::rate_limiter & rate_limiter)
		: fz::event_handler(loop)
		, options_(options)
		, rate_limit_mgr_(rate_limit_mgr)
		, rate_limiter_(rate_limiter)
	{
		UpdateRateLimit();
		options_.watch(OPTION_SPEEDLIMIT_ENABLE, this);
		options_.watch(OPTION_SPEEDLIMIT_INBOUND, this);
		options_.watch(OPTION_SPEEDLIMIT_OUTBOUND, this);
		options_.watch(OPTION_SPEEDLIMIT_BURSTTOLERANCE, this);
	}

	~option_change_handler()
	{
		options_.unwatch_all(this);
		remove_handler();
	}

private:
	virtual void operator()(fz::event_base const& ev) override {
		fz::dispatch<options_changed_event>(ev, this, &option_change_handler::on_options_changed);
	}

	void on_options_changed(watched_options const&)
	{
		UpdateRateLimit();
	}

	void UpdateRateLimit();

	COptionsBase & options_;
	fz::rate_limit_manager & rate_limit_mgr_;
	fz::rate_limiter & rate_limiter_;
};

void option_change_handler::UpdateRateLimit()
{
	fz::rate::type tolerance;
	switch (options_.get_int(OPTION_SPEEDLIMIT_BURSTTOLERANCE)) {
	case 1:
		tolerance = 2;
		break;
	case 2:
		tolerance = 5;
		break;
	default:
		tolerance = 1;
	}
	rate_limit_mgr_.set_burst_tolerance(tolerance);

	fz::rate::type limits[2]{ fz::rate::unlimited, fz::rate::unlimited };
	if (options_.get_int(OPTION_SPEEDLIMIT_ENABLE)) {
		auto const inbound = options_.get_int(OPTION_SPEEDLIMIT_INBOUND);
		if (inbound > 0) {
			limits[0] = inbound * 1024;
		}
		auto const outbound = options_.get_int(OPTION_SPEEDLIMIT_OUTBOUND);
		if (outbound > 0) {
			limits[1] = outbound * 1024;
		}
	}
	rate_limiter_.set_limits(limits[0], limits[1]);
}
}

class CFileZillaEngineContext::Impl final
{
public:
	Impl(COptionsBase& options)
		: options_(options)
		, rate_limit_mgr_(loop_)
		, tlsSystemTrustStore_(pool_)
		, logfile_writer_(options_, loop_)
	    , size_formatter_(options_)
	{
		directory_cache_.SetTtl(fz::duration::from_seconds(options.get_int(OPTION_CACHE_TTL)));
		rate_limit_mgr_.add(&rate_limiter_);
	}

	~Impl()
	{
	}

	COptionsBase& options_;
	fz::thread_pool pool_;
	fz::event_loop loop_{pool_};
	fz::rate_limit_manager rate_limit_mgr_;
	fz::rate_limiter rate_limiter_;
	option_change_handler option_change_handler_{options_, loop_, rate_limit_mgr_, rate_limiter_};
	CDirectoryCache directory_cache_;
	CPathCache path_cache_;
	OpLockManager opLockManager_;
	fz::tls_system_trust_store tlsSystemTrustStore_;
	activity_logger activity_logger_;
	logfile_writer logfile_writer_;
	SizeFormatter size_formatter_;
};

CFileZillaEngineContext::CFileZillaEngineContext(COptionsBase & options, CustomEncodingConverterBase const& customEncodingConverter)
: options_(options)
, customEncodingConverter_(customEncodingConverter)
, impl_(std::make_unique<Impl>(options))
{
}

CFileZillaEngineContext::~CFileZillaEngineContext()
{
}

fz::thread_pool& CFileZillaEngineContext::GetThreadPool()
{
	return impl_->pool_;
}

fz::event_loop& CFileZillaEngineContext::GetEventLoop()
{
	return impl_->loop_;
}

fz::rate_limiter& CFileZillaEngineContext::GetRateLimiter()
{
	return impl_->rate_limiter_;
}

CDirectoryCache& CFileZillaEngineContext::GetDirectoryCache()
{
	return impl_->directory_cache_;
}

CPathCache& CFileZillaEngineContext::GetPathCache()
{
	return impl_->path_cache_;
}

OpLockManager& CFileZillaEngineContext::GetOpLockManager()
{
	return impl_->opLockManager_;
}

fz::tls_system_trust_store& CFileZillaEngineContext::GetTlsSystemTrustStore()
{
	return impl_->tlsSystemTrustStore_;
}

activity_logger& CFileZillaEngineContext::GetActivityLogger()
{
	return impl_->activity_logger_;
}

logfile_writer & CFileZillaEngineContext::GetLogFileWriter()
{
	return impl_->logfile_writer_;
}

SizeFormatter & CFileZillaEngineContext::size_formatter()
{
	return impl_->size_formatter_;
}
