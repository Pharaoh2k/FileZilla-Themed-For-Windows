#ifndef FILEZILLA_COMMONUI_UPDATER_CERT_HEADER
#define FILEZILLA_COMMONUI_UPDATER_CERT_HEADER

#include "visibility.h"

#include <string_view>

// BASE-64 encoded DER without the BEGIN/END CERTIFICATE
extern FZCUI_PUBLIC_SYMBOL std::string_view const updater_cert;

#endif
