#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_THEMES_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_THEMES_HEADER

class COptionsPageThemes final : public COptionsPage
{
public:
	COptionsPageThemes();
	virtual ~COptionsPageThemes();

	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

	virtual bool CreateControls(wxWindow* parent) override;
protected:
	bool DisplayTheme(std::wstring const& theme);

	virtual bool OnDisplayedFirstTime() override;

	void OnThemeChange(wxCommandEvent& event);

	struct impl;
	std::unique_ptr<impl> impl_;
};

#endif
