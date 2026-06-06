#ifndef FILEZILLA_INTERFACE_MANUAL_TRANSFER_HEADER
#define FILEZILLA_INTERFACE_MANUAL_TRANSFER_HEADER

#include "dialogex.h"

#include "../commonui/site.h"

class COptionsBase;
class CQueueView;
class CState;
class CManualTransfer final : public wxDialogEx
{
public:
	CManualTransfer(COptionsBase& options, CQueueView* pQueueView);
	~CManualTransfer();

	void Run(wxWindow* parent, CState* pState);

protected:
	void DisplayServer();
	void SetControlState();
	void SetAutoAsciiState();

	void OnLocalChanged(wxCommandEvent& event);
	void OnLocalBrowse(wxCommandEvent& event);
	void OnRemoteChanged(wxCommandEvent& event);
	void OnDirection(wxCommandEvent& event);
	void OnServerTypeChanged(wxCommandEvent& event);
	void OnOK(wxCommandEvent& event);
	void OnSelectSite(wxCommandEvent& event);
	void OnSelectedSite(wxCommandEvent& event);

	struct impl;
	std::unique_ptr<impl> impl_;

	bool local_file_exists_{};

	Site site_;
	Site lastSite_;

	COptionsBase & options_;
	CState* state_{};
	CQueueView* queue_{};
};

#endif
