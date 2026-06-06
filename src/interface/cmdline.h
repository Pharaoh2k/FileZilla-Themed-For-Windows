#ifndef FILEZILLA_INTERFACE_CMDLINE_HEADER
#define FILEZILLA_INTERFACE_CMDLINE_HEADER

#include <wx/cmdline.h>

class CCommandLine final
{
public:
	enum t_switches
	{
		sitemanager,
		close,
		version,
		debug_startup
	};

	enum t_option
	{
		logontype,
		site,
		local
	};

	CCommandLine(int argc, wxChar** argv);
	bool Parse();
	void DisplayUsage();

	bool HasSwitch(t_switches s) const;
	std::wstring GetOption(t_option option) const;
	std::wstring GetParameter() const;

	bool BlocksReconnectAtStartup() const;

protected:
	wxCmdLineParser m_parser;
};

#endif
