#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_LANGUAGE_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_LANGUAGE_HEADER

#include "optionspage.h"

class wxListBox;
class COptionsPageLanguage final : public COptionsPage
{
public:
	virtual bool CreateControls(wxWindow* parent) override;
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

	virtual bool OnDisplayedFirstTime();

	struct _locale_info { wxString name; std::wstring code; };

protected:
	void GetLocales();

	wxListBox* lb_{};
	std::vector<_locale_info> locales_;
};

#endif
