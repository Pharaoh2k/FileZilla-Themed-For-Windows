#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_FILEEXISTS_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_FILEEXISTS_HEADER

class COptionsPageFileExists final : public COptionsPage
{
public:
	COptionsPageFileExists();
	virtual ~COptionsPageFileExists();

	virtual bool CreateControls(wxWindow* parent) override;
	virtual bool LoadPage() override;
	virtual bool SavePage() override;

private:
	struct impl;
	std::unique_ptr<impl> impl_;
};

#endif
