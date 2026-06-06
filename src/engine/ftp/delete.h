#ifndef FILEZILLA_ENGINE_FTP_DELETE_HEADER
#define FILEZILLA_ENGINE_FTP_DELETE_HEADER

#include "ftpcontrolsocket.h"

#include "../../include/serverpath.h"

class CFtpDeleteOpData final : public COpData, public CFtpOpData
{
public:
	CFtpDeleteOpData(CFtpControlSocket & controlSocket)
	    : COpData(Command::del, L"CFtpDeleteOpData")
		, CFtpOpData(controlSocket)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int prevResult, COpData const&) override;
	virtual int Reset(int result) override;

	CServerPath path_;
	std::vector<std::wstring> files_;
	bool omitPath_{};

	// Set to fz::monotonic_clock::now initially and after
	// sending an updated listing to the UI.
	fz::monotonic_clock time_;

	bool needSendListing_{};

	// Set to true if deletion of at least one file failed
	bool deleteFailed_{};
};

#endif
