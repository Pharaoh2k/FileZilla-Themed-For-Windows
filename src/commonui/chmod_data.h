#ifndef FILEZILLA_COMMONUI_CHMOD_DATA_HEADER
#define FILEZILLA_COMMONUI_CHMOD_DATA_HEADER

#include "../include/posix_chmod.h"

#include "visibility.h"
#include <string>

#include <cstdint>

class FZCUI_PUBLIC_SYMBOL ChmodData
{
public:
	posix_chmod chmod_;
	bool recurse_{};
	bool apply_files_{true};
	bool apply_dirs_{true};

	posix_permissions Apply(std::string_view old, bool dir) const;
	posix_permissions Apply(posix_permissions const&) const;
};

#endif
