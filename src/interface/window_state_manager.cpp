#include "filezilla.h"
#include "window_state_manager.h"
#include "Options.h"
#if wxUSE_DISPLAY
#include <wx/display.h>
#endif

CWindowStateManager::CWindowStateManager(wxTopLevelWindow* pWindow, COptionsBase & options)
	: m_pWindow(pWindow)
	, options_(options)
{
	m_lastMaximized = false;

	if (m_pWindow) {
		m_pWindow->Connect(wxID_ANY, wxEVT_SIZE, wxSizeEventHandler(CWindowStateManager::OnSize), 0, this);
		m_pWindow->Connect(wxID_ANY, wxEVT_MOVE, wxMoveEventHandler(CWindowStateManager::OnMove), 0, this);
	}

#ifdef __WXGTK__
	m_maximize_requested = 0;
#endif
}

CWindowStateManager::~CWindowStateManager()
{
	if (m_pWindow) {
		m_pWindow->Disconnect(wxID_ANY, wxEVT_SIZE, wxSizeEventHandler(CWindowStateManager::OnSize), 0, this);
		m_pWindow->Disconnect(wxID_ANY, wxEVT_MOVE, wxMoveEventHandler(CWindowStateManager::OnMove), 0, this);
	}
}

void CWindowStateManager::Remember(interfaceOptions const optionId)
{
	if (!m_lastWindowSize.IsFullySpecified()) {
		return;
	}

	wxString posString;

	// is_maximized
	posString += wxString::Format(_T("%d "), m_lastMaximized ? 1 : 0);

	// pos_x pos_y
	posString += wxString::Format(_T("%d %d "), m_lastWindowPosition.x, m_lastWindowPosition.y);

	// pos_width pos_height
	posString += wxString::Format(_T("%d %d "), m_lastWindowSize.x, m_lastWindowSize.y);

	options_.set(optionId, posString.ToStdWstring());
}

bool CWindowStateManager::ReadDefaults(interfaceOptions const optionId, bool& maximized, wxPoint& position, wxSize& size)
{
	if (wxGetKeyState(WXK_SHIFT) && wxGetKeyState(WXK_ALT) && wxGetKeyState(WXK_CONTROL)) {
		return false;
	}

	// Fields:
	// - maximized (1 or 0)
	// - x position
	// - y position
	// - width
	// - height
	auto tokens = fz::strtok(options_.get_string(optionId), L" ");
	if (tokens.size() < 5) {
		return false;
	}

	int values[5];
	for (int i = 0; i < 5; ++i) {
		values[i] = fz::to_integral(tokens[i], std::numeric_limits<int>::min());
		if (values[i] == std::numeric_limits<int>::min()) {
			return false;
		}
	}
	if (values[3] <= 0 || values[4] <= 0) {
		return false;
	}

	wxRect const screen_size = GetScreenDimensions();

	size.x = values[3];
	size.y = values[4];

	// Make sure position is (somewhat) sane.
	// We allow the window to be partially out of sight, as long as the title bar is at least partially visible.

	// Deal with the horizontal
	position.x = wxMin(screen_size.GetRight() - 30, values[1]);
	if (position.x + size.x - 30 < screen_size.GetLeft()) {
		position.x = screen_size.GetLeft() + 30 - size.x;
	}

	// Deal with the vertical
	position.y = wxMin(screen_size.GetBottom() - 30, values[2]);
	if (position.y < screen_size.GetTop()) {
		position.y = screen_size.GetTop();
	}

#if wxUSE_DISPLAY
	// In a second step, adjust vertical again, but this time based on the screen at the assumed centerpoint of the titlebar
	wxPoint title_center = wxPoint(position.x + size.x / 2, position.y + 4);
	int const di = wxDisplay::GetFromPoint(title_center);
	if (di != wxNOT_FOUND) {
		wxDisplay d(di);
		if (d.IsOk()) {
			wxRect const dr = d.GetClientArea();

			int dy = position.y - dr.GetTop();
			if (dy > -100 && dy < 0) {
				position.y = dr.GetTop();
			}

			dy = (dr.GetBottom() - 30) - position.y;
			if (dy > -100 && dy < 0) {
				position.y = dr.GetBottom() - 30;
			}
		}
	}
#endif

	maximized = values[0] != 0;

	return true;
}

bool CWindowStateManager::Restore(interfaceOptions const optionId, wxSize const& default_size)
{
	wxPoint position(-1, -1);
	wxSize size = default_size;
	bool maximized = false;

	bool read = ReadDefaults(optionId, maximized, position, size);

	if (maximized) {
		// We need to move so it appears on the proper display on multi-monitor systems
		m_pWindow->Move(position.x, position.y);

		// We need to call SetClientSize here too. Since window isn't yet shown here, Maximize
		// doesn't actually resize the window till it is shown

		// The slight off-size is needed to ensure the client sizes gets changed at least once.
		// Otherwise all the splitters would have default size still.
		m_pWindow->SetClientSize(size.x + 1, size.y);

		// A 2nd call is neccessary, for some reason the first call
		// doesn't fully set the height properly at least under wxMSW
		m_pWindow->SetClientSize(size.x, size.y);

#ifdef __WXMSW__
		m_pWindow->Show();
#endif
#ifdef __WXMAC__
		m_pWindow->ShowFullScreen(true, wxFULLSCREEN_NOMENUBAR);
#else
		m_pWindow->Maximize();
#endif
#ifdef __WXGTK__
		if (!m_pWindow->IsMaximized())
			m_maximize_requested = 4;
#endif //__WXGTK__
	}
	else {
		if (m_pWindow->GetSizer()) {
			wxSize minSize = m_pWindow->GetSizer()->GetMinSize();
			if (minSize.IsFullySpecified()) {
				size.x = std::max(size.x, minSize.x);
				size.y = std::max(size.y, minSize.y);
			}
		}
		if (read) {
			m_pWindow->Move(position.x, position.y);
		}

		if (size.IsFullySpecified()) {
			// The slight off-size is needed to ensure the client sizes gets changed at least once.
			// Otherwise all the splitters would have default size still.
			m_pWindow->SetClientSize(size.x + 1, size.x);

			// A 2nd call is neccessary, for some reason the first call
			// doesn't fully set the height properly at least under wxMSW
			m_pWindow->SetClientSize(size.x, size.y);
		}

		if (!read) {
			m_pWindow->CentreOnScreen();
		}
	}

	return true;
}

void CWindowStateManager::OnSize(wxSizeEvent& event)
{
#ifdef __WXGTK__
	if (m_maximize_requested) {
		m_maximize_requested--;
	}
#endif
	if (!m_pWindow->IsIconized()) {
#ifdef __WXMAC__
		m_lastMaximized = m_pWindow->IsFullScreen();
#else
		m_lastMaximized = m_pWindow->IsMaximized();
#endif
		if (!m_lastMaximized) {
			m_lastWindowPosition = m_pWindow->GetPosition();
			m_lastWindowSize = m_pWindow->GetClientSize();
		}
	}
	event.Skip();
}

void CWindowStateManager::OnMove(wxMoveEvent& event)
{
	if (!m_pWindow->IsIconized() && !m_pWindow->IsMaximized() && !m_pWindow->IsFullScreen()) {
		m_lastWindowPosition = m_pWindow->GetPosition();
		m_lastWindowSize = m_pWindow->GetClientSize();
	}
	event.Skip();
}

wxRect CWindowStateManager::GetScreenDimensions()
{
#if wxUSE_DISPLAY
	wxRect screen_size(0, 0, 0, 0);

	// Get bounding rectangle of virtual screen
	for (unsigned int i = 0; i < wxDisplay::GetCount(); ++i) {
		wxDisplay display(i);
		wxRect rect = display.GetClientArea();
		screen_size.Union(rect);
	}
	if (screen_size.GetWidth() <= 0 || screen_size.GetHeight() <= 0) {
		screen_size = wxRect(0, 0, 1000000000, 1000000000);
	}
#else
	wxRect screen_size(0, 0, 1000000000, 1000000000);
#endif

	return screen_size;
}
