#include "filezilla.h"
#include "option_change_event_handler.h"

namespace {
void wx_option_watcher_notifier(void* handler, watched_options&& options)
{
	static_cast<COptionChangeEventHandler*>(handler)->notify(std::move(options));
}
}

std::tuple<void*, watcher_notifier> get_option_watcher_notifier(COptionChangeEventHandler* handler)
{
	return std::make_tuple(handler, &wx_option_watcher_notifier);
}


void COptionChangeEventHandler::notify(watched_options && options)
{
	handler_.CallAfter([this, o = std::move(options)]() { OnOptionsChanged(o); });
}
