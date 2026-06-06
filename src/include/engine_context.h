#ifndef FILEZILLA_ENGINE_CONTEXT_HEADER
#define FILEZILLA_ENGINE_CONTEXT_HEADER

#include "visibility.h"

#include <memory>

class activity_logger;
class CDirectoryCache;
class COptionsBase;
class CPathCache;
class OpLockManager;
class logfile_writer;
class SizeFormatter;

namespace fz {
class event_loop;
class rate_limiter;
class thread_pool;
class tls_system_trust_store;
}

class FZC_PUBLIC_SYMBOL CustomEncodingConverterBase
{
public:
	virtual ~CustomEncodingConverterBase() = default;

	virtual std::wstring toLocal(std::wstring const& encoding, char const* buffer, size_t len) const = 0;
	virtual std::string toServer(std::wstring const& encoding, wchar_t const* buffer, size_t len) const = 0;
};

// There can be multiple engines, but there can be at most one context
class FZC_PUBLIC_SYMBOL CFileZillaEngineContext final
{
public:
	CFileZillaEngineContext(COptionsBase & options, CustomEncodingConverterBase const& customEncodingConverter);
	~CFileZillaEngineContext();

	COptionsBase& GetOptions() { return options_; }
	fz::thread_pool& GetThreadPool();
	fz::event_loop& GetEventLoop();
	fz::rate_limiter& GetRateLimiter();
	CDirectoryCache& GetDirectoryCache();
	CPathCache& GetPathCache();
	CustomEncodingConverterBase const& GetCustomEncodingConverter() { return customEncodingConverter_; }
	OpLockManager& GetOpLockManager();
	fz::tls_system_trust_store& GetTlsSystemTrustStore();
	activity_logger& GetActivityLogger();
	logfile_writer & GetLogFileWriter();
	SizeFormatter & size_formatter();

protected:
	COptionsBase& options_;
	CustomEncodingConverterBase const& customEncodingConverter_;

	class Impl;
	std::unique_ptr<Impl> impl_;
};

#endif
