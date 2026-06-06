#include "misc.h"
#include "options.h"
#include "auto_ascii_files.h"

// Filenames on VMS systems have a revision suffix, e.g.
// foo.bar;1
// foo.bar;2
// foo.bar;3
std::wstring StripVMSRevision(std::wstring const& name)
{
	size_t pos = name.rfind(';');
	if (pos == std::wstring::npos || !pos) {
		return name;
	}

	if (pos == name.size() - 1) {
		return name;
	}

	size_t p = pos;
	while (++p < name.size()) {
		wchar_t const& c = name[p];
		if (c < '0' || c > '9') {
			return name;
		}
	}

	return name.substr(0, pos);
}

transfer_flags GetTransferFlags(bool download, CServer const& server, COptionsBase& options, std::wstring const& sourceFile, CServerPath const& remotePath)
{
	transfer_flags flags{};

	if (server.HasFeature(ProtocolFeature::DataTypeConcept)) {
		if (download) {
			if (CAutoAsciiFiles::TransferRemoteAsAscii(options, sourceFile, remotePath.GetType())) {
				flags |= ftp_transfer_flags::ascii;
			}
		}
		else {
			if (CAutoAsciiFiles::TransferLocalAsAscii(options, sourceFile, remotePath.GetType())) {
				flags |= ftp_transfer_flags::ascii;
			}
		}
	}

	return flags;
}

transfer_flags FZCUI_PUBLIC_SYMBOL GetMkdirFlags(CServer const&, COptionsBase&, CServerPath const&)
{
	return {};
}

int CompareWithThreshold(fz::datetime const& a, fz::datetime const& b, fz::duration const& threshold)
{
	int const dateCmp = a.compare(b);
	if (!dateCmp) {
		return 0;
	}

	int adjusted{};
	if (dateCmp < 0) {
		adjusted = (a + threshold).compare(b);
	}
	else {
		adjusted = a.compare(b + threshold);
	}

	if (dateCmp == -adjusted) {
		return 0;
	}
	return dateCmp;
}
