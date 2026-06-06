#ifndef FILEZILLA_INTERFACE_INPUTDIALOG_HEADER
#define FILEZILLA_INTERFACE_INPUTDIALOG_HEADER

#include "dialogex.h"

class CInputDialog final : public wxDialogEx
{
public:
	CInputDialog() = default;

	bool Create(wxWindow* parent, wxString const& title, wxString const& text, int max_len = -1, bool password = false);

	void AllowEmpty(bool allowEmpty);

	void SetValue(wxString const& value);
	wxString GetValue() const;

	bool SelectText(int start, int end);

protected:
	bool allowEmpty_{};
	wxTextCtrl* textCtrl_{};
};

#endif
