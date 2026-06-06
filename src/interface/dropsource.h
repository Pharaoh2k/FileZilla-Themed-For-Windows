#ifndef FILEZILLA_INTERFACE_DROPDROUCE_HEADER
#define FILEZILLA_INTERFACE_DROPDROUCE_HEADER

#include <wx/dnd.h>

class DropSource final : public wxDropSource
{
public:
	DropSource(wxWindow *win = nullptr);
	virtual ~DropSource();

	wxDragResult DoFileDragDrop(int flags = wxDrag_CopyOnly);

#ifdef __WXMAC__
	wxString m_OutDir;
#endif
};

#endif
