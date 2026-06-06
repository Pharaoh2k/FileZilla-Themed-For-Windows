#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_FILELISTS_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_FILELISTS_HEADER

#include <memory>

class COptionsPageFilelists final : public COptionsPage
{
public:
	COptionsPageFilelists();
	virtual ~COptionsPageFilelists();

	virtual bool CreateControls(wxWindow* parent) override;
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

private:
	struct impl;
	std::unique_ptr<impl> impl_;
};

#endif
