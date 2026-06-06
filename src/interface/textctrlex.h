#ifndef FILEZILLA_INTERFACE_TEXTCTRLEX_HEADER
#define FILEZILLA_INTERFACE_TEXTCTRLEX_HEADER

#include <wx/textctrl.h>

class wxTextCtrlEx : public wxTextCtrl
{
public:
	wxTextCtrlEx() = default;
	wxTextCtrlEx(wxWindow* parent, int id, wxString const& value = wxString(), wxPoint const& pos = wxDefaultPosition, wxSize const& size = wxDefaultSize, long style = 0);

	bool Create(wxWindow* parent, int id, wxString const& value = wxString(), wxPoint const& pos = wxDefaultPosition, wxSize const& size = wxDefaultSize, long style = 0);

#ifdef __WXMAC__
	// Disable pasting of formatting, we're only ever interested in the text.
	virtual void Paste() override;
#endif
};

#ifdef __WXMAC__
const wxTextAttr& GetDefaultTextCtrlStyle(wxTextCtrl* ctrl);
#endif

#endif
