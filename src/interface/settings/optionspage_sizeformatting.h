#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_SIZEFORMATTING_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_SIZEFORMATTING_HEADER

#include "../../include/sizeformatting.h"

class COptionsPageSizeFormatting final : public COptionsPage
{
public:
	COptionsPageSizeFormatting();
	virtual ~COptionsPageSizeFormatting();

	virtual bool CreateControls(wxWindow* parent) override;

	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

private:
	void UpdateControls();

	SizeFormat GetFormat() const;

	wxString FormatSize(int64_t size);

	struct impl;
	std::unique_ptr<impl> impl_;
};

#endif
