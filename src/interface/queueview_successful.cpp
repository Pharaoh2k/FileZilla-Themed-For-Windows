#include "filezilla.h"
#include "queue.h"
#include "queueview_successful.h"
#include "Options.h"

#include <wx/menu.h>

BEGIN_EVENT_TABLE(CQueueViewSuccessful, CQueueViewFailed)
EVT_CONTEXT_MENU(CQueueViewSuccessful::OnContextMenu)
EVT_MENU(XRCID("ID_AUTOCLEAR"), CQueueViewSuccessful::OnMenuAutoClear)
END_EVENT_TABLE()

CQueueViewSuccessful::CQueueViewSuccessful(CQueue* parent, COptionsBase & options, TimeFormatter & time_formatter, login_manager & lim, int index, CMainFrame* pMainFrame)
	: CQueueViewFailed(parent, options, time_formatter, lim, index, pMainFrame, _("Successful transfers"))
{
	std::vector<ColumnId> extraCols({colTime});
	CreateColumns(extraCols);

	m_autoClear = options_.get_int(OPTION_QUEUE_SUCCESSFUL_AUTOCLEAR) ? true : false;
}

void CQueueViewSuccessful::OnContextMenu(wxContextMenuEvent&)
{
	wxMenu menu;
	menu.Append(XRCID("ID_REMOVEALL"), _("Remove &all"));
	menu.Append(XRCID("ID_REQUEUEALL"), _("Reset and requeue &all"));

	menu.AppendSeparator();
	menu.Append(XRCID("ID_REMOVE"), _("Remove &selected"));
	menu.Append(XRCID("ID_REQUEUE"), _("R&eset and requeue selected files"));

	menu.AppendSeparator();
	menu.Append(XRCID("ID_AUTOCLEAR"), _("A&utomatically remove successful transfers"), wxString(), wxITEM_CHECK);
	menu.Append(XRCID("ID_EXPORT"), _("E&xport..."));

	bool has_selection = HasSelection();

	menu.Enable(XRCID("ID_REMOVE"), has_selection);
	menu.Enable(XRCID("ID_REQUEUE"), has_selection);
	menu.Enable(XRCID("ID_REQUEUEALL"), !m_serverList.empty());
	menu.Check(XRCID("ID_AUTOCLEAR"), m_autoClear);
	menu.Enable(XRCID("ID_EXPORT"), GetItemCount() != 0);

	PopupMenu(&menu);
}

void CQueueViewSuccessful::OnMenuAutoClear(wxCommandEvent&)
{
	m_autoClear = !m_autoClear;
	options_.set(OPTION_QUEUE_SUCCESSFUL_AUTOCLEAR, m_autoClear ? true : false);
}
