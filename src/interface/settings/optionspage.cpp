#include "../filezilla.h"
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"

bool COptionsPage::CreatePage(COptions & options, CSettingsDialog* pOwner, wxWindow* parent, wxSize& maxSize)
{
	m_pOwner = pOwner;
	m_pOptions = &options;

	if (!CreateControls(parent)) {
		return false;
	}

	UpdateMaxPageSize(maxSize);

	return true;
}

void COptionsPage::UpdateMaxPageSize(wxSize& maxSize)
{
	wxSize size = GetSize();

#ifdef __WXGTK__
	// wxStaticBox draws its own border coords -1.
	// Adjust this window so that the left border is fully visible.
	Move(1, 0);
	size.x += 1;
#endif

	if (size.GetWidth() > maxSize.GetWidth()) {
		maxSize.SetWidth(size.GetWidth());
	}
	if (size.GetHeight() > maxSize.GetHeight()) {
		maxSize.SetHeight(size.GetHeight());
	}
}

void COptionsPage::ReloadSettings()
{
	m_pOwner->LoadSettings();
}

bool COptionsPage::DisplayError(wxWindow* pWnd, wxString const& error)
{
	if (pWnd) {
		pWnd->SetFocus();
	}

	wxMessageBoxEx(error, _("Failed to validate settings"), wxICON_EXCLAMATION, this);

	return false;
}

bool COptionsPage::Display()
{
	if (!m_was_selected) {
		if (!OnDisplayedFirstTime()) {
			return false;
		}
		m_was_selected = true;
	}
	Show();

	return true;
}

bool COptionsPage::OnDisplayedFirstTime()
{
	return true;
}
