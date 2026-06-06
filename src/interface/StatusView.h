#ifndef FILEZILLA_INTERFACE_STATUSVIEW_HEADER
#define FILEZILLA_INTERFACE_STATUSVIEW_HEADER

#ifdef __WXMSW__
#include "richedit.h"
#endif

#include "option_change_event_handler.h"

#include <wx/timer.h>

#include <list>

class CFastTextCtrl;
class CStatusView final : public wxNavigationEnabled<wxWindow>, public COptionChangeEventHandler
{
public:
	CStatusView(wxWindow* parent, COptionsBase & options);
	virtual ~CStatusView();

	void AddToLog(CLogmsgNotification && pNotification);
	void AddToLog(logmsg::type messagetype, std::wstring && message, fz::datetime const& time);

	void InitDefAttr();

	virtual void SetFocus();

	virtual bool Show(bool show = true);

private:

	int m_nLineCount{};
	CFastTextCtrl *m_pTextCtrl{};

	void OnOptionsChanged(watched_options const& options);

	DECLARE_EVENT_TABLE()
	void OnSize(wxSizeEvent &);
	void OnContextMenu(wxContextMenuEvent&);
	void OnClear(wxCommandEvent& );
	void OnCopy(wxCommandEvent& );
	void OnTimer(wxTimerEvent&);

	std::list<int> m_lineLengths;
	std::list<int> m_unusedLineLengths;

	struct t_attributeCache
	{
		std::wstring prefix;
		size_t len{};
		wxTextAttr attr;
#ifdef __WXMSW__
		CHARFORMAT2 cf{};
#endif
	} m_attributeCache[sizeof(logmsg::type) * 8];

	bool m_rtl{};

	bool m_shown{};

	// Don't update actual log window if not shown,
	// do it later when showing the window.
	struct t_line
	{
		logmsg::type messagetype;
		std::wstring message;
		fz::datetime time;
	};
	std::list<t_line> m_hiddenLines;

	bool m_showTimestamps{};
	fz::datetime m_lastTime;
	std::wstring m_lastTimeString;

	// Re-using the same string to format the message in avoids uneeded allocations
	std::wstring m_formattedMessage;

	COptionsBase & options_;
};

#endif
