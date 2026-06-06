#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_ACTIVE_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_ACTIVE_HEADER

class COptionsPageConnectionActive final : public COptionsPage
{
public:
	COptionsPageConnectionActive();
	virtual ~COptionsPageConnectionActive();

	virtual bool CreateControls(wxWindow* parent) override;
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

protected:
	void SetCtrlState();

	struct impl;
	std::unique_ptr<impl> impl_;
};

#endif
