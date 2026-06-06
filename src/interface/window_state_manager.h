#ifndef FILEZILLA_INTERFACE_WINDOW_STATE_MANAGER_HEADER
#define FILEZILLA_INTERFACE_WINDOW_STATE_MANAGER_HEADER

// This class get used to remember toplevel window size and position across
// sessions.

#include "Options.h"

class CWindowStateManager final : public wxEvtHandler
{
public:
	explicit CWindowStateManager(wxTopLevelWindow* pWindow, COptionsBase & options);
	virtual ~CWindowStateManager();

	bool Restore(interfaceOptions const optionId, const wxSize& default_size = wxSize(-1, -1));
	void Remember(interfaceOptions const optionId);

	static wxRect GetScreenDimensions();

#ifdef __WXGTK__
	// Set nonzero if Restore maximized the window.
	// Reason is that under wxGTK, maximization request may take some time.
	// It is actually done asynchronously by the window manager.
	unsigned int m_maximize_requested;
#endif

protected:
	bool ReadDefaults(interfaceOptions const optionId, bool& maximized, wxPoint& position, wxSize& size);

	wxTopLevelWindow* m_pWindow;
	COptionsBase & options_;

	bool m_lastMaximized;
	wxPoint m_lastWindowPosition;
	wxSize m_lastWindowSize;

	void OnSize(wxSizeEvent& event);
	void OnMove(wxMoveEvent& event);
};

#endif
