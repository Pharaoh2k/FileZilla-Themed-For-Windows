#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_EDIT_ASSOCIATIONS_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_EDIT_ASSOCIATIONS_HEADER

class wxTextCtrlEx;
class COptionsPageEditAssociations final : public COptionsPage
{
public:
	virtual bool CreateControls(wxWindow* parent) override;
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

private:
	wxTextCtrlEx* assocs_{};
};

#endif
