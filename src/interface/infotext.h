#ifndef FILEZILLA_INTERFACE_INFOTEXT_HEADER
#define FILEZILLA_INTERFACE_INFOTEXT_HEADER

#include "graphics.h"
#include "listctrlex.h"

class CInfoText final : public wxWindow
{
public:
	CInfoText(wxListCtrlEx& parent)
		: parent_(parent)
		, m_tinter(*this)
	{
		Hide();
		Create(&parent, wxID_ANY, wxPoint(0, 60), wxDefaultSize);

		SetForegroundColour(parent.GetForegroundColour());
		SetBackgroundColour(parent.GetBackgroundColour());
		GetTextExtent(m_text, &m_textSize.x, &m_textSize.y);

#ifdef __WXMSW__
		if (GetLayoutDirection() != wxLayout_RightToLeft) {
			SetDoubleBuffered(true);
		}
#endif
	}

	virtual wxVisualAttributes GetDefaultAttributes() const override
	{
		return GetParent()->GetDefaultAttributes();
	}

	void SetText(wxString const& text)
	{
		if (text == m_text) {
			return;
		}

		m_text = text;
		GetTextExtent(m_text, &m_textSize.x, &m_textSize.y);
	}

	wxSize GetTextSize() const { return m_textSize; }

	bool AcceptsFocus() const { return false; }

	void SetBackgroundTint(site_colour tint) {
		m_tinter.SetBackgroundTint(tint);
	}

	void Reposition();

protected:
	wxListCtrlEx& parent_;

	wxString m_text;

	void OnPaint(wxPaintEvent&);

	wxSize m_textSize;

	CWindowTinter m_tinter;

	DECLARE_EVENT_TABLE()
};

#endif
