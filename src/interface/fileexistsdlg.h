#ifndef FILEZILLA_INTERFACE_FILEEXISTSDLG_HEADER
#define FILEZILLA_INTERFACE_FILEEXISTSDLG_HEADER

#include "dialogex.h"

class COptionsBase;
class TimeFormatter;
class CFileExistsDlg final : public wxDialogEx
{
	DECLARE_EVENT_TABLE()

public:
	/// Constructors
	CFileExistsDlg(CFileExistsNotification *pNotification, COptionsBase & options, TimeFormatter & time_formatter);

	/// Creation
	bool Create(wxWindow* parent);

	CFileExistsNotification::OverwriteAction GetAction() const;
	bool Always(bool &directionOnly, bool &queueOnly) const;

protected:
	bool SetupControls();

	void DisplayFile(bool left, std::wstring const& name, int64_t size, fz::datetime const& time, std::wstring const& iconFile);

	void OnOK(wxCommandEvent& event);
	void OnCancel(wxCommandEvent& event);
	void OnCheck(wxCommandEvent& event);

	void LoadIcon(int id, std::wstring const& file);
	std::wstring GetPathEllipsis(std::wstring const& path, wxWindow *window);

	COptionsBase & options_;
	TimeFormatter & time_formatter_;

	CFileExistsNotification *m_pNotification;
	CFileExistsNotification::OverwriteAction m_action;
	bool m_always{};
	bool m_directionOnly{};
	bool m_queueOnly{};
};

#endif
