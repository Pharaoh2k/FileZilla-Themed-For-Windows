#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_PROXY_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_PROXY_HEADER

class COptionsPageProxy final : public COptionsPage
{
public:
	COptionsPageProxy();
	virtual ~COptionsPageProxy();

	virtual bool CreateControls(wxWindow* parent) override;
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

protected:
	struct impl;
	std::unique_ptr<impl> impl_;

	void SetCtrlState();

	void OnProxyTypeChanged(wxCommandEvent& event);
};

#endif
