#ifndef FILEZILLA_INTERFACE_WELCOME_DIALOG_HEADER
#define FILEZILLA_INTERFACE_WELCOME_DIALOG_HEADER

#include "dialogex.h"

#include <wx/timer.h>

class COptionsBase;
class CWelcomeDialog final : public wxDialogEx
{
public:
	CWelcomeDialog(COptionsBase & options, wxWindow* parent);

	bool Run(bool force = false);
	void RunDelayed();

protected:

	void InitFooter(std::wstring const& resources);

	COptionsBase & options_;
	wxTimer m_delayedShowTimer;

	wxWindow* parent_{};

	DECLARE_EVENT_TABLE()
	void OnTimer(wxTimerEvent& event);
};

#endif
