#include "filezilla.h"
#include "led.h"
#include "filezillaapp.h"
#include "themeprovider.h"

#include <wx/dcclient.h>

BEGIN_EVENT_TABLE(CLed, wxWindow)
	EVT_PAINT(CLed::OnPaint)
END_EVENT_TABLE()

CLed::CLed(wxWindow *parent, unsigned int index)
	: m_index(index ? 1 : 0)
{
#if defined(__WXGTK20__) && !defined(__WXGTK3__)
	SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
#endif

	wxSize const& size = CThemeProvider::GetIconSize(iconSizeTiny);
	Create(parent, -1, wxDefaultPosition, size);

	wxBitmap bmp = CThemeProvider::Get()->CreateBitmap(L"ART_LEDS", wxART_OTHER, size * 2);
	if (bmp.IsOk()) {
		m_leds[0] = bmp.GetSubBitmap(wxRect(size.x, index * size.y, size.x, size.y));
		m_leds[1] = bmp.GetSubBitmap(wxRect(0, index * size.y, size.x, size.y));
		m_loaded = true;
	}
}

void CLed::OnPaint(wxPaintEvent&)
{
	wxPaintDC dc(this);

	if (!m_loaded) {
		return;
	}

	dc.DrawBitmap(m_leds[lit_ ? 1 : 0], 0, 0, true);
}

void CLed::Set()
{
	Set(true);
}

void CLed::Set(bool lit)
{
	if (lit_ != lit) {
		lit_ = lit;
		Refresh();
	}
}

void CLed::Unset()
{
	Set(false);
}
