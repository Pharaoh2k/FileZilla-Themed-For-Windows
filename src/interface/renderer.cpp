#include "filezilla.h"
#include "renderer.h"

#if defined(__WXMAC__)

#include "themeprovider.h"
#include <wx/renderer.h>

namespace {
class CRenderer final : public wxDelegateRendererNative
{
public:
	CRenderer()
		: wxDelegateRendererNative(wxRendererNative::GetDefault())
	{
	}

	virtual wxSplitterRenderParams GetSplitterParams(wxWindow const* win) override
	{
		return wxSplitterRenderParams(5, 0, false);
	}

	virtual void DrawSplitterSash(wxWindow* win, wxDC & dc, wxSize const& size, wxCoord position, wxOrientation orient, int flags) override
	{
		wxRendererNative::GetDefault().DrawSplitterSash(win, dc, size, position, orient, flags);

		int const height = GetSplitterParams(win).widthSash;

		dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW)));
		if (orient == wxVERTICAL) {
			dc.DrawLine(position, 0, position, size.y);
			dc.DrawLine(position + height - 1, 0, position + height - 1, size.y);
		}
		else {
			dc.DrawLine(0, position, size.x, position);
			dc.DrawLine(0, position + height - 1, size.x, position + height - 1);
		}
	}

	virtual void DrawTreeItemButton(wxWindow* win, wxDC& dc, wxRect const& rect, int flags) override
	{
		std::wstring name = L"ART_TREEITEM_";
		name += (flags & wxCONTROL_EXPANDED) ? L"EXPANDED" : L"COLLAPSED";
		name += wxSystemSettingsNative::GetAppearance().IsDark() ? L"_LIGHT" : L"_DARK";
		
		auto bmp = CThemeProvider::Get()->CreateBitmap(name, wxART_OTHER, rect.GetSize());
		dc.DrawBitmap(bmp, rect.x, rect.y);
	}
};
}

void InitRenderer()
{
	auto renderer = new CRenderer;
	wxRendererNative::Get(); // To initialize the native one
	wxRendererNative::Set(renderer);
}

#else

void InitRenderer()
{
}

#endif
