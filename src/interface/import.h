#ifndef FILEZILLA_INTERFACE_IMPORT_HEADER
#define FILEZILLA_INTERFACE_IMPORT_HEADER

#include "dialogex.h"

#include "xmlfunctions.h"

class CQueueView;
class XmlOptions;
class login_manager;
class CImportDialog final : public wxDialogEx
{
public:
	CImportDialog(wxWindow* parent, CQueueView* pQueueView);

	void Run(XmlOptions & options, login_manager & lim);

protected:

	wxWindow* const m_parent;
	CQueueView* m_pQueueView;
};

#endif
