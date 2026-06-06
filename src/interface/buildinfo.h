#ifndef FILEZILLA_INTERFACE_BUILDINFO_HEADER
#define FILEZILLA_INTERFACE_BUILDINFO_HEADER

#include "../commonui/buildinfo.h"

enum class gui_lib_dependency
{
	wxwidgets,
	sqlite,
	count
};

std::wstring GetDependencyName(gui_lib_dependency d);
std::wstring GetDependencyVersion(gui_lib_dependency d);

#endif
