#ifndef FILEZILLA_INTERFACE_SPEEDLIMITS_DIALOG_HEADER
#define FILEZILLA_INTERFACE_SPEEDLIMITS_DIALOG_HEADER

#include "dialogex.h"

class COptionsBase;
class CSpeedLimitsDialog final : public wxDialogEx
{
public:
	CSpeedLimitsDialog(COptionsBase & options);
	virtual ~CSpeedLimitsDialog();

	void Run(wxWindow* parent);

protected:
	struct impl;
	std::unique_ptr<impl> impl_;

	void OnToggleEnable(wxCommandEvent& event);
	void OnOK(wxCommandEvent& event);
};

#endif
