#ifndef FILEZILLA_INTERFACE_LED_HEADER
#define FILEZILLA_INTERFACE_LED_HEADER

#include <wx/event.h>
#include <wx/timer.h>

class CLed final : public wxWindow
{
public:
	CLed(wxWindow *parent, unsigned int index);

	void Set();
	void Set(bool lit);
	void Unset();

protected:

	int const m_index;
	bool lit_{};

	wxBitmap m_leds[2];
	bool m_loaded{};

	DECLARE_EVENT_TABLE()
	void OnPaint(wxPaintEvent& event);
};

#endif
