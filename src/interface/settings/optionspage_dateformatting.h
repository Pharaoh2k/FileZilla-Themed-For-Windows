#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_DATEFORMATTING_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_DATEFORMATTING_HEADER

#include "optionspage.h"

#include <memory>

class COptionsPageDateFormatting final : public COptionsPage
{
public:
	COptionsPageDateFormatting();
	~COptionsPageDateFormatting();

	virtual bool CreateControls(wxWindow* parent) override;
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

private:
	void SetCtrlState();

	struct impl;
	std::unique_ptr<impl> impl_;
};

#endif
