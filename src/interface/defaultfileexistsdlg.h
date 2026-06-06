#ifndef FILEZILLA_INTERFACE_DEFAULTFILEEXISTSDLG_HEADER
#define FILEZILLA_INTERFACE_DEFAULTFILEEXISTSDLG_HEADER

#include "dialogex.h"

class CDefaultFileExistsDlg final : protected wxDialogEx
{
public:
	CDefaultFileExistsDlg();
	~CDefaultFileExistsDlg();

	static CFileExistsNotification::OverwriteAction GetDefault(bool download);
	static void SetDefault(bool download, CFileExistsNotification::OverwriteAction action);

	bool Run(wxWindow* parent, bool fromQueue, CFileExistsNotification::OverwriteAction *downloadAction = 0, CFileExistsNotification::OverwriteAction *uploadAction = 0);

protected:
	bool Load(wxWindow* parent, bool fromQueue, bool local, bool remote);

	void SelectDefaults(CFileExistsNotification::OverwriteAction* downloadAction, CFileExistsNotification::OverwriteAction* uploadAction);

	static CFileExistsNotification::OverwriteAction m_defaults[2];

	struct impl;
	std::unique_ptr<impl> impl_;
};

#endif
