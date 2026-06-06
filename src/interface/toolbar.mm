#include <wx/wx.h>

#include <AppKit/NSWindow.h>

void fix_toolbar_style(wxFrame& frame)
{
	if (__builtin_available(macos 11.0, *)) {
		WXWindow tlw = frame.MacGetTopLevelWindowRef();
		[tlw setToolbarStyle:NSWindowToolbarStyleExpanded];
	}
}
