#ifndef FILEZILLA_INTERFACE_AUTO_ASCII_FILES_HEADER
#define FILEZILLA_INTERFACE_AUTO_ASCII_FILES_HEADER

#include "../include/engine_options.h"
#include "../include/server.h"

#include "visibility.h"

class FZCUI_PUBLIC_SYMBOL CAutoAsciiFiles final
{
public:
	static bool TransferLocalAsAscii(COptionsBase& options, std::wstring const& local_file, ServerType server_type);
	static bool TransferRemoteAsAscii(COptionsBase& options, std::wstring const& remote_file, ServerType server_type);

	static void SettingsChanged(COptionsBase& options);
protected:
	static bool IsAsciiExtension(std::wstring const& ext);
	static std::vector<std::wstring> ascii_extensions_;
};

#endif
