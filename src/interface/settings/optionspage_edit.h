#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_EDIT_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_EDIT_HEADER

#include "optionspage.h"

#include <memory>

class COptionsPageEdit final : public COptionsPage
{
public:
	COptionsPageEdit();
	virtual ~COptionsPageEdit();

	virtual bool CreateControls(wxWindow* parent) override;
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

private:
	struct impl;
	std::unique_ptr<impl> impl_;

	void SetCtrlState();
	void OnBrowseEditor();
};

#endif
