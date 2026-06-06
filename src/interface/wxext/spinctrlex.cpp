#include "../filezilla.h"
#include "spinctrlex.h"

#define DEFAULT_LENGTH_LIMIT 26 // Big enough for all 64bit values with thousands separator
#define DEFAULT_LENGTH_LIMIT_DOUBLE 100 // A guess

wxSpinCtrlEx::wxSpinCtrlEx()
{
}

namespace {
wxSize FixSpinCtrlSize(wxSize const& s) {
#ifdef __WXGTK3__
	// The control is naturally monstrous on wxGTK3.
	return wxDefaultSize;
#endif

	return s;
}
}

wxSpinCtrlEx::wxSpinCtrlEx(wxWindow* parent, wxWindowID id, wxString const& value, wxPoint const& pos, wxSize const& size, long style, int min, int max, int initial)
	: wxSpinCtrl(parent, id, value, pos, FixSpinCtrlSize(size), style, min, max, initial)
{
	SetMaxLength(DEFAULT_LENGTH_LIMIT);
}

bool wxSpinCtrlEx::Create(wxWindow* parent, wxWindowID id, wxString const& value, wxPoint const& pos, wxSize const& size, long style, int min, int max, int initial, wxString const& name)
{
	bool ret = wxSpinCtrl::Create(parent, id, value, pos, size, style, min, max, initial, name);
	if (ret) {
		SetMaxLength(DEFAULT_LENGTH_LIMIT);
	}
	return ret;
}

void wxSpinCtrlEx::SetMaxLength(unsigned long len)
{
#ifdef __WXMSW__
	::SendMessage(m_hwndBuddy, EM_LIMITTEXT, len, 0);
#else
	(void)len;
#endif
}

wxSpinCtrlDoubleEx::wxSpinCtrlDoubleEx()
{
}

wxSpinCtrlDoubleEx::wxSpinCtrlDoubleEx(wxWindow* parent, wxWindowID id, wxString const& value, wxPoint const& pos, wxSize const& size, long style, double min, double max, double initial, double inc)
	: wxSpinCtrlDouble(parent, id, value, pos, size, style, min, max, initial, inc)
{
	SetMaxLength(DEFAULT_LENGTH_LIMIT_DOUBLE);
}

bool wxSpinCtrlDoubleEx::Create(wxWindow* parent, wxWindowID id, wxString const& value, wxPoint const& pos, wxSize const& size, long style, double min, double max, double initial, double inc, wxString const& name)
{
	bool ret = wxSpinCtrlDouble::Create(parent, id, value, pos, size, style, min, max, initial, inc, name);
	if (ret) {
		SetMaxLength(DEFAULT_LENGTH_LIMIT_DOUBLE);
	}
	return false;
}

void wxSpinCtrlDoubleEx::SetMaxLength(unsigned long len)
{
#ifndef __WXGTK__
	GetText()->SetMaxLength(len);
#else
	(void)len;
#endif
}
