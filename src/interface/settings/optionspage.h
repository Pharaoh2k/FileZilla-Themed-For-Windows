#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_HEADER

#include <wx/panel.h>

#include "../Options.h"

class CSettingsDialog;
class COptionsPage : public wxPanel
{
public:
	virtual bool CreatePage(COptions & options, CSettingsDialog* pOwner, wxWindow* parent, wxSize& maxSize);

	void UpdateMaxPageSize(wxSize& maxSize);

	virtual bool LoadPage() = 0;
	virtual bool SavePage() = 0;
	virtual bool Validate() { return true; }

	void ReloadSettings();

	// Always returns false
	bool DisplayError(wxWindow* pWnd, wxString const& error);

	bool Display();

	virtual bool OnDisplayedFirstTime();

protected:
	virtual bool CreateControls(wxWindow* parent) = 0;

	COptions* m_pOptions{};
	CSettingsDialog* m_pOwner{};

	bool m_was_selected{};
};

#endif
