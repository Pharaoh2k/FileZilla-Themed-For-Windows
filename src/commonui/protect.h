
#ifndef FILEZILLA_COMMONUI_PROTECT_HEADER
#define FILEZILLA_COMMONUI_PROTECT_HEADER

#include "options.h"
#include "login_manager.h"

#include "visibility.h"

void FZCUI_PUBLIC_SYMBOL protect(ProtectedCredentials&, login_manager& lim, COptionsBase& options);

#endif
