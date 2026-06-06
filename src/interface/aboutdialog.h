#ifndef FILEZILLA_INTERFACE_ABOUTDIALOG_HEADER
#define FILEZILLA_INTERFACE_ABOUTDIALOG_HEADER

#include "dialogex.h"

class COptionsBase;
class CAboutDialog final : public wxDialogEx
{
public:
	CAboutDialog(COptionsBase& options);

	bool Create(wxWindow* parent);

protected:
	void OnCopy();

	COptionsBase& options_;
};

#endif
