#ifndef FILEZILLA_INTERFACE_XH_TEXT_EX_HEADER
#define FILEZILLA_INTERFACE_XH_TEXT_EX_HEADER

#include <wx/xrc/xh_text.h>

#ifndef __WXMAC__
#define wxTextCtrlXmlHandlerEx wxTextCtrlXmlHandler
#else

class wxTextCtrlXmlHandlerEx final : public wxTextCtrlXmlHandler
{
public:
	virtual wxObject *DoCreateResource();
};

#endif

#endif
