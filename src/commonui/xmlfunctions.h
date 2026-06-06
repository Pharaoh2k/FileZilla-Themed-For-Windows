/*
 * xmlfunctions.h declares some useful xml helper functions, especially to
 * improve usability together with wxWidgets.
 */

#ifndef FILEZILLA_COMMONUI_XMLFUNCTIONS_HEADER
#define FILEZILLA_COMMONUI_XMLFUNCTIONS_HEADER

#include "options.h"
#include "login_manager.h"
#include "xml_file.h"

#include "visibility.h"

// Function to save CServer objects to the XML file
void FZCUI_PUBLIC_SYMBOL SetServer(pugi::xml_node node, Site const& site, login_manager& lim, COptionsBase& options);

#endif
