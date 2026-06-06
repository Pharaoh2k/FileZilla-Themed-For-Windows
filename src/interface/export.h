#ifndef FILEZILLA_INTERFACE_EXPORT_HEADER
#define FILEZILLA_INTERFACE_EXPORT_HEADER

#include "dialogex.h"

class COptions;
class CQueueView;
class CExportDialog final : protected wxDialogEx
{
public:
	CExportDialog(wxWindow* parent);

	void Run(COptions& options, CQueueView const* pQueueView);

protected:
	wxWindow* const m_parent;
};

#endif
