#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_HEADER

#include "optionspage.h"

#include <memory>

class COptionsPageConnection final : public COptionsPage
{
public:
	COptionsPageConnection();
	~COptionsPageConnection();

	virtual bool CreateControls(wxWindow* parent) override;
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

private:
	struct impl;
	std::unique_ptr<impl> impl_;
};

#endif
