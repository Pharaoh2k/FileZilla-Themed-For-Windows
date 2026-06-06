#ifndef FILEZILLA_INTERFACE_SEARCH_HEADER
#define FILEZILLA_INTERFACE_SEARCH_HEADER

#include "filter_conditions_dialog.h"
#include "local_recursive_operation.h"
#include "listingcomparison.h"
#include "state.h"
#include <set>

class CEditHandler;
class CFilelistStatusBar;
class COptionsBase;
class CQueueView;
class CSearchDialogFileList;
class CWindowStateManager;
class TimeFormatter;

class CSearchDialog final : public CFilterConditionsDialog, public CStateEventHandler
{
	friend class CSearchDialogFileList;
public:
	enum class search_mode
	{
		local,
		remote,
		comparison
	};

	CSearchDialog(wxWindow* parent, CState& state, CQueueView* pQueue, COptionsBase & options, TimeFormatter & time_formatter, CEditHandler* edit_handler);
	virtual ~CSearchDialog();

	bool Load();
	void Run();

	bool IsIdle();

protected:
	void ProcessDirectoryListing(std::shared_ptr<CDirectoryListing> const& listing);
	void ProcessDirectoryListing(CLocalRecursiveOperation::listing const& listing);

	void SetCtrlState();

	void SaveConditions();
	void LoadConditions();

	wxWindow* m_parent{};
	CSearchDialogFileList *m_results{};
	CSearchDialogFileList *m_remoteResults{};
	CQueueView* m_pQueue{};
	COptionsBase& options_;
	TimeFormatter & time_formatter_;
	wxSize m_otherSize{-1, -1};

	CFilelistStatusBar* m_remoteStatusBar{};

	virtual void OnStateChange(t_statechange_notifications notification, std::wstring const& data, const void* data2) override;

	CWindowStateManager* m_pWindowStateManager{};

	CFilter m_search_filter;

	search_mode mode_{};
	bool searching_{};

	void Stop();

	DECLARE_EVENT_TABLE()
	void OnSearch(wxCommandEvent& event);
	void OnContextMenu(wxContextMenuEvent& event);
	void OnDownload(wxCommandEvent&);
	void OnUpload(wxCommandEvent&);
	void OnEdit(wxCommandEvent&);
	void OnDeleteLocal(wxCommandEvent&);
	void OnDeleteRemote(wxCommandEvent&);
	void OnCharHook(wxKeyEvent& event);
	void OnChangeSearchMode(wxCommandEvent&);
	void OnGetUrl(wxCommandEvent& event);
	void OnLocalOpen(wxCommandEvent& event);
	void OnRemoteOpen(wxCommandEvent& event);
	void OnShowFileManager(wxCommandEvent& event);
	void OnChangeCompareOption(wxCommandEvent& event);

	std::set<CServerPath> m_visited;

	CLocalPath m_local_search_root;
	CServerPath m_remote_search_root;

	CComparisonManager* m_pComparisonManager{};
	CEditHandler* edit_handler_{};
};

#endif
