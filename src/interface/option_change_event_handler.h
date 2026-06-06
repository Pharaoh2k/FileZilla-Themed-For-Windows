#ifndef FILEZILLA_INTERFACE_OPTION_CHANGE_EVENT_HANDLER_HEADER
#define FILEZILLA_INTERFACE_OPTION_CHANGE_EVENT_HANDLER_HEADER

#include "../include/optionsbase.h"

#include <wx/event.h>

class COptionChangeEventHandler
{
public:
	COptionChangeEventHandler(COptionChangeEventHandler const&) = delete;

	explicit COptionChangeEventHandler(wxEvtHandler* handler)
		: handler_(*handler)
	{
	}

	virtual ~COptionChangeEventHandler() {};

	void notify(watched_options && options);

protected:
	virtual void OnOptionsChanged(watched_options const& options) = 0;

private:
	wxEvtHandler& handler_;
};

std::tuple<void*, watcher_notifier> get_option_watcher_notifier(COptionChangeEventHandler *);

#endif
