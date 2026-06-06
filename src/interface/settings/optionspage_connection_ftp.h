#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_FTP_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_FTP_HEADER

#include "optionspage.h"

#include <memory>

class COptionsPageConnectionFTP final : public COptionsPage
{
public:
	COptionsPageConnectionFTP();
	~COptionsPageConnectionFTP();

	virtual bool CreateControls(wxWindow* parent) override;
	virtual bool LoadPage() override;
	virtual bool SavePage() override;

private:
	struct impl;
	std::unique_ptr<impl> impl_;
};

#endif
