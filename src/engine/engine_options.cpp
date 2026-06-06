#include "../include/engine_options.h"

namespace {

unsigned int register_engine_options()
{
	static int const value = register_options({
		{ "Use Pasv mode", 1, option_flags::normal, 0, 1 },
		{ "Limit local ports", false, option_flags::normal },
		{ "Limit ports low", 6000, option_flags::normal, 1, 65535 },
		{ "Limit ports high", 7000, option_flags::normal, 1, 65535 },
		{ "Limit ports offset", 0, option_flags::normal, -65534, 65534 },
		{ "External IP mode", 0, option_flags::normal, 0, 2 },
		{ "External IP", L"", option_flags::normal, 100 },
		{ "External address resolver", L"http://ip.filezilla-project.org/ip.php", option_flags::normal, 1024 },
		{ "Last resolved IP", L"", option_flags::normal, 100 },
		{ "No external ip on local conn", true, option_flags::normal },
		{ "Pasv reply fallback mode", 0, option_flags::normal, 0, 2 },
		{ "Timeout", 20, option_flags::normal, 0, 9999, [](int& v)
			{
				if (v && v < 10) {
					v = 10;
				}
				return true;
			}
		},
		{ "Logging Debug Level", 0, option_flags::normal, 0, 4 },
		{ "Logging Raw Listing", false, option_flags::normal },
		{ "fzstorj executable", L"", option_flags::internal },
		{ "Allow transfermode fallback", true, option_flags::normal },
		{ "Reconnect count", 2, option_flags::numeric_clamp, 0, 99 },
		{ "Reconnect delay", 5, option_flags::numeric_clamp, 0, 999 },
		{ "Enable speed limits", false, option_flags::normal },
		{ "Speedlimit inbound", 1000, option_flags::numeric_clamp, 0, 999999999 },
		{ "Speedlimit outbound", 100, option_flags::numeric_clamp, 0, 999999999 },
		{ "Speedlimit burst tolerance", 0, option_flags::normal, 0, 2 },
		{ "Preallocate space", false, option_flags::normal },
		{ "View hidden files", false, option_flags::normal },
		{ "Preserve timestamps", false, option_flags::normal },

		// Make it large enough by default
		// to enable a large TCP window scale
		{ "Socket recv buffer size (v2)", 4194304, option_flags::numeric_clamp, -1, 64 * 1024 * 1024, [](int& v)
			{
				if (v >= 0 && v < 4096) {
					v = 4096;
				}
				return true;
			}
		},
		{ "FTP Keep-alive commands", false, option_flags::normal },
		{ "FTP Proxy type", 0, option_flags::normal, 0, 4 },
		{ "FTP Proxy host", L"", option_flags::normal },
		{ "FTP Proxy user", L"", option_flags::normal },
		{ "FTP Proxy password", L"", option_flags::normal },
		{ "FTP Proxy login sequence", L"", option_flags::normal },
		{ "SFTP keyfiles", L"", option_flags::platform },
		{ "SFTP compression", false, option_flags::normal },
		{ "Proxy type", 0, option_flags::normal, 0, 3 },
		{ "Proxy host", L"", option_flags::normal },
		{ "Proxy port", 0, option_flags::normal, 1, 65535 },
		{ "Proxy user", L"", option_flags::normal },
		{ "Proxy password", L"", option_flags::normal },
		{ "Logging file", L"", option_flags::platform },
		{ "Logging filesize limit", 10, option_flags::normal, 0, 2000 },
		{ "Logging show detailed logs", false, option_flags::internal },
		{ "Size format", 0, option_flags::normal, 0, 4 },
		{ "Size thousands separator", true, option_flags::normal },
		{ "Size decimal places", 1, option_flags::numeric_clamp, 0, 3 },
		{ "TCP Keepalive Interval", 15, option_flags::numeric_clamp, 1, 10000 },
		{ "Cache TTL", 600, option_flags::numeric_clamp, 30, 60*60*24 },
		{ "Minimum TLS Version", 2, option_flags::numeric_clamp, 0, 3 },
		{ "Directory listing item limit", 10000000, option_flags::numeric_clamp, 1000000, 2000000000 }
	});
	return value;
}

option_registrator r(&register_engine_options);
}

optionsIndex mapOption(engineOptions opt)
{
	static unsigned int const offset = register_engine_options();

	auto ret = optionsIndex::invalid;
	if (opt < OPTIONS_ENGINE_NUM) {
		return static_cast<optionsIndex>(opt + offset);
	}
	return ret;
}
