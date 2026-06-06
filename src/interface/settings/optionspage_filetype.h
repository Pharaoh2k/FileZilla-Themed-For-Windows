#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_FILETYPE_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_FILETYPE_HEADER

#include <memory>

class COptionsPageFiletype final : public COptionsPage
{
public:
	COptionsPageFiletype();
	virtual ~COptionsPageFiletype();

	virtual bool CreateControls(wxWindow* parent) override;
	virtual bool LoadPage() override;
	virtual bool SavePage() override;

private:
	struct impl;
	std::unique_ptr<impl> impl_;

	void SetCtrlState();

	void OnAdd();
	void OnRemove();
};

#endif
