#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_FTPPROXY_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_FTPPROXY_HEADER

#include "optionspage.h"

class COptionsPageFtpProxy final : public COptionsPage
{
public:
	COptionsPageFtpProxy();
	~COptionsPageFtpProxy();

	virtual bool CreateControls(wxWindow* parent) override;
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

protected:

	void SetCtrlState();

	void OnProxyTypeChanged(wxCommandEvent& event);
	void OnLoginSequenceChanged(wxCommandEvent& event);

	struct impl;
	std::unique_ptr<impl> impl_;
};

#endif
