#include "filezilla.h"

#include "textctrlex.h"

wxTextCtrlEx::wxTextCtrlEx(wxWindow* parent, int id, wxString const& value, wxPoint const& pos, wxSize const& size, long style)
	: wxTextCtrl(parent, id, value, pos, size, style)
{
#ifdef __WXGTK__
	if (!(style & wxTE_MULTILINE))
#endif
	SetMaxLength(512 * 1024);

#ifdef __WXMSW__
	if (style & wxTE_MULTILINE && style & wxTE_READONLY) {
		Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& evt) {
			SetBackgroundColour(GetClassDefaultAttributes().colBg);
			evt.Skip();
		});
	}
#endif

#if defined(__WXMAC__)
	OSXEnableAutomaticQuoteSubstitution(false);
	OSXEnableAutomaticDashSubstitution(false);
#endif
}

bool wxTextCtrlEx::Create(wxWindow* parent, int id, wxString const& value, wxPoint const& pos, wxSize const& size, long style)
{
	bool ret = wxTextCtrl::Create(parent, id, value, pos, size, style);
	if (ret) {
#ifdef __WXGTK__
		if (!(style & wxTE_MULTILINE))
#endif
		SetMaxLength(512 * 1024);
	}
	return ret;
}

#ifdef __WXMAC__
static wxTextAttr DoGetDefaultStyle(wxTextCtrl* ctrl)
{
	wxTextAttr style;
	style.SetFont(ctrl->GetFont());
	return style;
}

const wxTextAttr& GetDefaultTextCtrlStyle(wxTextCtrl* ctrl)
{
	static const wxTextAttr style = DoGetDefaultStyle(ctrl);
	return style;
}

void wxTextCtrlEx::Paste()
{
	wxTextCtrl::Paste();
	SetStyle(0, GetLastPosition(), GetDefaultTextCtrlStyle(this));
}
#endif
