/*
 * xmlfunctions.h declares some useful xml helper functions, especially to
 * improve usability together with wxWidgets.
 */

#ifndef FILEZILLA_INTERFACE_XMLFUNCTIONS_HEADER
#define FILEZILLA_INTERFACE_XMLFUNCTIONS_HEADER

#include "../commonui/xml_file.h"
#include "../commonui/xmlfunctions.h"

bool SaveWithErrorDialog(CXmlFile& file, bool updateMetadata = true);

#endif
