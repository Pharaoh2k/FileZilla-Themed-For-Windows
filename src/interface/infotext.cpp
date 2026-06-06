#include "filezilla.h"
#include "infotext.h"

#include <wx/dcclient.h>

BEGIN_EVENT_TABLE(CInfoText, wxWindow)
EVT_PAINT(CInfoText::OnPaint)
END_EVENT_TABLE()

void CInfoText::Reposition()
{
	wxRect rect = parent_.GetClientRect();
	wxSize size = GetTextSize();

	if (!parent_.GetItemCount()) {
		rect.y = 60;
	}
	else {
		wxRect itemRect;
		parent_.GetItemRect(0, itemRect);
		rect.y = wxMax(60, itemRect.GetBottom() + 1);
	}
	rect.x = rect.x + (rect.width - size.x) / 2;
	rect.width = size.x;
	rect.height = size.y;

	SetSize(rect);
#ifdef __WXMSW__
	if (GetLayoutDirection() != wxLayout_RightToLeft) {
		Refresh(true);
		Update();
	}
	else
#endif
		Refresh(false);
}

void CInfoText::OnPaint(wxPaintEvent&)
{
	wxPaintDC paintDc(this);

	paintDc.SetFont(GetFont());
	paintDc.SetTextForeground(GetForegroundColour());

	paintDc.DrawText(m_text, 0, 0);
}
