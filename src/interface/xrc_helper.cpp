#include "filezilla.h"
#include "filezillaapp.h"
#include "xrc_helper.h"

#include <libfilezilla/local_filesys.hpp>

#include <wx/xrc/xh_panel.h>
#include <wx/xrc/xh_sizer.h>
#include <wx/xrc/xh_stbox.h>
#include <wx/xrc/xh_stlin.h>
#include <wx/xrc/xh_sttxt.h>
#include <wx/xrc/xh_hyperlink.h>
#include <wx/xrc/xh_stbmp.h>
#include "xh_text_ex.h"

#include <unordered_set>

void InitHandlers(wxXmlResource& res)
{
	res.AddHandler(new wxSizerXmlHandler);
	res.AddHandler(new wxStaticTextXmlHandler);
	res.AddHandler(new wxStaticBoxXmlHandler);
	res.AddHandler(new wxStaticLineXmlHandler);
	res.AddHandler(new wxPanelXmlHandler);
	res.AddHandler(new wxHyperlinkCtrlXmlHandler);
	res.AddHandler(new wxStaticBitmapXmlHandler);
}
