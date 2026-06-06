#ifndef FILEZILLA_INTERFACE_CHMODDIALOG_HEADER
#define FILEZILLA_INTERFACE_CHMODDIALOG_HEADER

#include "dialogex.h"
#include "../commonui/chmod_data.h"

class CChmodDialog final : public wxDialogEx
{
public:
	CChmodDialog();
	~CChmodDialog();

	bool Create(wxWindow* parent, int fileCount, int dirCount,
				std::wstring const& name, posix_chmod const& initial_chmod);

	ChmodData GetChmodData() const;

protected:
	posix_chmod GetChmodFromCheckboxes();

	void OnOK(wxCommandEvent&);
	void OnRecurseChanged(wxCommandEvent&);

	void OnCheckboxClick(wxCommandEvent&);
	void OnNumericChanged(wxCommandEvent&);

	struct impl;
	std::unique_ptr<impl> impl_;
};

#endif
