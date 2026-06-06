#ifndef FILEZILLA_COMMONUI_MISC_HEADER
#define FILEZILLA_COMMONUI_MISC_HEADER

#include <string>

#include "../include/server.h"
#include "../include/commands.h"

#include "visibility.h"

#include <libfilezilla/time.hpp>

std::wstring FZCUI_PUBLIC_SYMBOL StripVMSRevision(std::wstring const& name);

class COptionsBase;
transfer_flags FZCUI_PUBLIC_SYMBOL GetTransferFlags(bool download, CServer const& server, COptionsBase& options, std::wstring const& sourceFile, CServerPath const& remotePath);
transfer_flags FZCUI_PUBLIC_SYMBOL GetMkdirFlags(CServer const& server, COptionsBase& options, CServerPath const& remotePath);

int FZCUI_PUBLIC_SYMBOL CompareWithThreshold(fz::datetime const& a, fz::datetime const& b, fz::duration const& threshold);

#endif
