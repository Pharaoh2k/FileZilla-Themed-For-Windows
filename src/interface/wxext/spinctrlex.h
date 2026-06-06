#ifndef FILEZILLA_INTERFACE_EXEXT_SPINCTRL_HEADER
#define FILEZILLA_INTERFACE_EXEXT_SPINCTRL_HEADER

#include <wx/spinctrl.h>

class wxSpinCtrlEx : public wxSpinCtrl
{
public:
	wxSpinCtrlEx();
	wxSpinCtrlEx(wxWindow* parent, wxWindowID id = wxID_ANY, wxString const& value = wxString(), wxPoint const& pos = wxDefaultPosition, wxSize const& size = wxDefaultSize, long style = wxSP_ARROW_KEYS | wxALIGN_RIGHT, int min = 0, int max = 100, int initial = 0);

	bool Create(wxWindow* parent, wxWindowID id = wxID_ANY, wxString const& value = wxString(), wxPoint const& pos = wxDefaultPosition, wxSize const& size = wxDefaultSize, long style = wxSP_ARROW_KEYS | wxALIGN_RIGHT, int min = 0, int max = 100, int initial = 0, wxString const& name = wxT("wxSpinCtrl"));

	void SetMaxLength(unsigned long len);
};

class wxSpinCtrlDoubleEx : public wxSpinCtrlDouble
{
public:
    wxSpinCtrlDoubleEx();
    wxSpinCtrlDoubleEx(wxWindow* parent, wxWindowID id = wxID_ANY, wxString const& value = wxString(), wxPoint const& pos = wxDefaultPosition, wxSize const& size = wxDefaultSize, long style = wxSP_ARROW_KEYS | wxALIGN_RIGHT, double min = 0, double max = 100, double initial = 0, double inc = 1);
    
	bool Create(wxWindow* parent, wxWindowID id = wxID_ANY, wxString const& value = wxString(), wxPoint const& pos = wxDefaultPosition, wxSize const& size = wxDefaultSize, long style = wxSP_ARROW_KEYS | wxALIGN_RIGHT, double min = 0, double max = 100, double initial = 0, double inc = 1, wxString const& name = wxT("wxSpinCtrlDouble"));

	void SetMaxLength(unsigned long len);
};

#endif