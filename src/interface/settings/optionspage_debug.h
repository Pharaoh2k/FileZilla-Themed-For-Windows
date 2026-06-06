#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_DEBUG_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_DEBUG_HEADER

class COptionsPageDebug final : public COptionsPage
{
public:
	COptionsPageDebug();
	virtual ~COptionsPageDebug();

	virtual bool CreateControls(wxWindow* parent) override;
	virtual bool LoadPage() override;
	virtual bool SavePage() override;

protected:
	struct impl;
	std::unique_ptr<impl> impl_;
};

#endif
