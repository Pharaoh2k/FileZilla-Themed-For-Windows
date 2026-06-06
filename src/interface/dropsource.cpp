#include "filezilla.h"
#include "dropsource.h"

DropSource::DropSource(wxWindow *win)
	: wxDropSource(win)
{
}

DropSource::~DropSource()
{
}

#ifndef __WXMAC__
wxDragResult DropSource::DoFileDragDrop(int flags)
{
	return DoDragDrop(flags);
}
#endif

