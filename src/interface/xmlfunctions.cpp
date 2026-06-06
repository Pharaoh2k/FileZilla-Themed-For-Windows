#include "xmlfunctions.h"
#include "msgbox.h"
#include "loginmanager.h"
#include "Options.h"

#include "../commonui/protect.h"
#include "../commonui/xmlfunctions.h"

#include <wx/msgdlg.h>


bool SaveWithErrorDialog(CXmlFile& file, bool updateMetadata)
{
	bool res = file.Save(updateMetadata);
	if (!res) {
		auto error = file.GetError();
		wxString msg = wxString::Format(_("Could not write \"%s\":"), file.GetFileName());
		if (error.empty()) {
			error = _("Unknown error");
		}
		wxMessageBoxEx(msg + _T("\n") + error, _("Error writing xml file"), wxICON_ERROR);
	}
	return res;
}
