#include "../include/activity_logger.h"

void activity_logger::record(_direction direction, uint64_t amount)
{
	if (!amounts_[direction].fetch_add(amount)) {
		fz::scoped_lock l(mtx_);
		if (waiting_) {
			waiting_ = false;
			if (notification_cb_) {
				notification_cb_();
			}
		}
	}
}

std::pair<uint64_t, uint64_t> activity_logger::extract_amounts()
{
	fz::scoped_lock l(mtx_);

	std::pair<uint64_t, uint64_t> ret(amounts_[0].exchange(0), amounts_[1].exchange(0));
	if (!ret.first && !ret.second) {
		waiting_ = true;
	}

	return ret;
}

void activity_logger::set_notifier(std::function<void()> && notification_cb)
{
	fz::scoped_lock l(mtx_);
	notification_cb_ = std::move(notification_cb);
	if (notification_cb_) {
		amounts_[0] = 0;
		amounts_[1] = 0;
		waiting_ = true;
	}
}
