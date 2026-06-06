#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_LOGGING_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_LOGGING_HEADER

#include "optionspage.h"

class COptionsPageLogging final : public COptionsPage
{
public:
	COptionsPageLogging();
	virtual ~COptionsPageLogging();

	virtual bool CreateControls(wxWindow* parent) override;
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

protected:
	void SetCtrlState();

	void OnBrowse(wxCommandEvent& event);
	void OnCheck(wxCommandEvent& event);

	struct impl;
	std::unique_ptr<impl> impl_;
};

#endif
