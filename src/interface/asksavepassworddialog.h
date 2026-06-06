#ifndef FILEZILLA_INTERFACE_ASKSAVEPASSWORDDIALOG_HEADER
#define FILEZILLA_INTERFACE_ASKSAVEPASSWORDDIALOG_HEADER

#include "dialogex.h"

class COptionsBase;
class CAskSavePasswordDialog final : public wxDialogEx
{
public:
	CAskSavePasswordDialog(COptionsBase & options);
	~CAskSavePasswordDialog();

	static bool Run(wxWindow* parent, COptionsBase & options);
private:
	bool Create(wxWindow* parent);

	void OnOk(wxCommandEvent& event);

	struct impl;
	std::unique_ptr<impl> impl_;
};

#endif
