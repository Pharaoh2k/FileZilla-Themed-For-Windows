#include "filezilla.h"

#include "textctrlex.h"
#include "xh_text_ex.h"

#ifdef __WXMAC__

wxObject *wxTextCtrlXmlHandlerEx::DoCreateResource()
{
	XRC_MAKE_INSTANCE(text, wxTextCtrlEx);

	text->Create(m_parentAsWindow,
				 GetID(),
				 GetText(wxT("value")),
				 GetPosition(), GetSize(),
				 GetStyle());

	SetupWindow(text);

	if (HasParam(wxT("maxlength")))
		text->SetMaxLength(GetLong(wxT("maxlength")));

	if (HasParam(wxT("hint")))
		text->SetHint(GetText(wxS("hint")));

	return text;
}

#endif
