#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_SFTP_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_SFTP_HEADER

#include <memory>

class wxListEvent;
class COptionsPageConnectionSFTP final : public COptionsPage
{
public:
	COptionsPageConnectionSFTP();
	virtual ~COptionsPageConnectionSFTP();

	virtual bool CreateControls(wxWindow* parent) override;
	virtual bool LoadPage() override;
	virtual bool SavePage() override;

protected:
	struct impl;
	std::unique_ptr<impl> impl_;

	bool AddKey(std::wstring keyFile, bool silent);
	bool KeyFileExists(std::wstring const& keyFile);

	void SetCtrlState();

	void OnAdd(wxCommandEvent& event);
	void OnRemove(wxCommandEvent& event);
	void OnSelChanged(wxListEvent& event);
};

#endif
