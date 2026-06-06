#include "filezilla.h"
#include "buildinfo.h"

#include <sqlite3.h>

std::wstring GetDependencyVersion(gui_lib_dependency d)
{
	switch (d) {
	case gui_lib_dependency::wxwidgets:
		return wxVERSION_NUM_DOT_STRING_T;
	case gui_lib_dependency::sqlite:
		return fz::to_wstring_from_utf8(sqlite3_libversion());
	default:
		return std::wstring();
	}
}

std::wstring GetDependencyName(gui_lib_dependency d)
{
	switch (d) {
	case gui_lib_dependency::wxwidgets:
		return L"wxWidgets";
	case gui_lib_dependency::sqlite:
		return L"SQLite";
	default:
		return std::wstring();
	}
}
