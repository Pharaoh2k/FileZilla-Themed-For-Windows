#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_INTERFACE_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_INTERFACE_HEADER

#include "optionspage.h"

class COptionsPageInterface final : public COptionsPage
{
public:
	COptionsPageInterface();
	virtual ~COptionsPageInterface();

	virtual bool LoadPage() override;
	virtual bool SavePage() override;

protected:
	virtual bool CreateControls(wxWindow* parent) override;

private:
	void OnLayoutChange(wxCommandEvent& event);

	struct impl;
	std::unique_ptr<impl> impl_;
};

#endif
