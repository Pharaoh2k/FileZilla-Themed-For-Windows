#ifndef FILEZILLA_STORJ_KEY_INTERFACE_HEADER
#define FILEZILLA_STORJ_KEY_INTERFACE_HEADER

namespace fz {
class process;
}

#include <tuple>

class COptionsBase;
class wxWindow;
class CStorjKeyInterface final
{
public:
	CStorjKeyInterface(COptionsBase& options, wxWindow* parent);
	virtual ~CStorjKeyInterface();

	std::tuple<bool, std::wstring> ValidateGrant(std::wstring const& grant, bool silent);

	bool ProcessFailed() const;
protected:

	COptionsBase& options_;
	wxWindow* m_parent;
	std::unique_ptr<fz::process> m_process;
	bool m_initialized{};

	enum ReplyCode {
		success = 1,
		error,
	};

	bool LoadProcess(bool silent);
	bool Send(std::wstring const& cmd);
	ReplyCode GetReply(std::wstring& reply);
};

#endif
