#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_PASSIVE_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_PASSIVE_HEADER

#include "optionspage.h"

class COptionsPageConnectionPassive final : public COptionsPage
{
public:
	COptionsPageConnectionPassive();
	virtual ~COptionsPageConnectionPassive();

	virtual bool CreateControls(wxWindow* parent) override;
	virtual bool LoadPage() override;
	virtual bool SavePage() override;

private:
	struct impl;
	std::unique_ptr<impl> impl_;
};

#endif
