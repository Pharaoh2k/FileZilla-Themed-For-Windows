#ifndef FILEZILLA_ENGINE_ACTIVITY_LOGGER_HEADER
#define FILEZILLA_ENGINE_ACTIVITY_LOGGER_HEADER

#include <libfilezilla/mutex.hpp>

#include "visibility.h"

#include <atomic>
#include <functional>
#include <utility>

class FZC_PUBLIC_SYMBOL activity_logger
{
public:
	enum _direction
	{
		send,
		recv
	};

	activity_logger() = default;
	virtual ~activity_logger() noexcept = default;

	void record(_direction direction, uint64_t amount);

	std::pair<uint64_t, uint64_t> extract_amounts();

	void set_notifier(std::function<void()> && notification_cb);

private:
	std::atomic_uint64_t amounts_[2]{};

	fz::mutex mtx_;
	std::function<void()> notification_cb_;
	bool waiting_{};
};

#endif

