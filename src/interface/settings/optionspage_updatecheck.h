#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_UPDATECHECK_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_UPDATECHECK_HEADER

#include "../../commonui/updater.h"

#if FZ_MANUALUPDATECHECK && FZ_AUTOUPDATECHECK

#include "optionspage.h"

class COptionsPageUpdateCheck final : public COptionsPage
{
public:
	COptionsPageUpdateCheck();
	virtual ~COptionsPageUpdateCheck();

	virtual bool CreateControls(wxWindow* parent) override;
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

protected:
	void OnRunUpdateCheck(wxCommandEvent&);

	struct impl;
	std::unique_ptr<impl> impl_;
};

#endif

#endif
