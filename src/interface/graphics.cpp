#include "filezilla.h"

#include "graphics.h"

#include <wx/combobox.h>
#include <wx/listctrl.h>

namespace {
wxColour const background_colors[] = {
	wxColour(),
	wxColour(255, 0, 0, 32),
	wxColour(0, 255, 0, 32),
	wxColour(0, 0, 255, 32),
	wxColour(255, 255, 0, 32),
	wxColour(0, 255, 255, 32),
	wxColour(255, 0, 255, 32),
	wxColour(255, 128, 0, 32) };
}

wxColor site_colour_to_wx(site_colour c)
{
	auto index = static_cast<size_t>(c);
	if (index < sizeof(background_colors) / sizeof(*background_colors)){
		return background_colors[index];
	}
	return background_colors[0];
}

CWindowTinter::CWindowTinter(wxWindow& wnd)
	: m_wnd(wnd)
{
	m_wnd.Bind(wxEVT_SYS_COLOUR_CHANGED, &CWindowTinter::OnColorChange, this);
}

CWindowTinter::~CWindowTinter()
{
	m_wnd.Unbind(wxEVT_SYS_COLOUR_CHANGED, &CWindowTinter::OnColorChange, this);
}

void CWindowTinter::OnColorChange(wxSysColourChangedEvent &)
{
	SetBackgroundTint(site_colour_to_wx(tint_));
}

void CWindowTinter::SetBackgroundTint(site_colour tint)
{
	tint_ = tint;
	SetBackgroundTint(site_colour_to_wx(tint));
}

wxColour CWindowTinter::GetOriginalColor()
{
#ifndef __WXMSW__
	auto listctrl = dynamic_cast<wxListCtrl*>(m_wnd.GetParent());
	if (listctrl && reinterpret_cast<wxWindow*>(listctrl->m_mainWin) == &m_wnd) {
		return listctrl->GetDefaultAttributes().colBg;
	}
#endif

#ifdef __WXMAC__
	auto combo = dynamic_cast<wxComboBox*>(&m_wnd);
	if (combo) {
		wxColour c = wxTextCtrl::GetClassDefaultAttributes().colBg;

		// In old versions of macOS, the wrong color may be reported. Try to detect it and just default to white.
		if ((!c.IsOk() || c == *wxBLACK) && !wxSystemSettingsNative::GetAppearance().IsDark()) {
			return wxColour(255, 255, 255);
		}

		return c;
	}
#endif
	return m_wnd.GetDefaultAttributes().colBg;
}

void CWindowTinter::SetBackgroundTint(wxColour const& tint)
{
	if (!tint.IsOk() && dynamic_cast<wxComboBox*>(&m_wnd)) {
		m_wnd.SetBackgroundColour(wxColour());
		m_wnd.Refresh();
		return;
	}

	wxColour originalColor = GetOriginalColor();

	wxColour const newColour = tint.IsOk() ? AlphaComposite_Over(originalColor, tint) : originalColor;
	if (newColour != m_wnd.GetBackgroundColour()) {
		if (m_wnd.SetBackgroundColour(newColour)) {
			m_wnd.Refresh();
		}
	}
}

void Overlay(wxBitmap& bg, wxBitmap const& fg)
{
	if (!bg.IsOk() || !fg.IsOk()) {
		return;
	}

	wxImage foreground = fg.ConvertToImage();
	if (!foreground.HasAlpha()) {
		foreground.InitAlpha();
	}

	wxImage background = bg.ConvertToImage();
	if (!background.HasAlpha()) {
		background.InitAlpha();
	}

	if (foreground.GetSize() != background.GetSize()) {
		foreground.Rescale(background.GetSize().x, background.GetSize().y, wxIMAGE_QUALITY_HIGH);
	}

	unsigned char* bg_data = background.GetData();
	unsigned char* bg_alpha = background.GetAlpha();
	unsigned char* fg_data = foreground.GetData();
	unsigned char* fg_alpha = foreground.GetAlpha();
	unsigned char* bg_end = bg_data + background.GetWidth() * background.GetHeight() * 3;
	while (bg_data != bg_end) {
		AlphaComposite_Over_Inplace(
			*bg_data, *(bg_data + 1), *(bg_data + 2), *bg_alpha,
			*fg_data, *(fg_data + 1), *(fg_data + 2), *fg_alpha);
		bg_data += 3;
		fg_data += 3;
		++bg_alpha;
		++fg_alpha;
	}

#ifdef __WXMAC__
	bg = wxBitmap(background, -1, bg.GetScaleFactor());
#else
	bg = wxBitmap(background, -1);
#endif
}

