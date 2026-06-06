#include "filezilla.h"

#include <libfilezilla/format.hpp>
#include <libfilezilla/uri.hpp>

#include <assert.h>

struct t_protocolInfo
{
	ServerProtocol const protocol;
	std::wstring const prefix;
	bool alwaysShowPrefix;
	bool parse_from_prefix;
	unsigned int defaultPort;
	bool const translateable;
	char const* const name;
	std::wstring const alternative_prefix;
};

static const t_protocolInfo protocolInfos[] = {
	{ FTP,             L"ftp",       false, true,    21, true,  fztranslate_mark("FTP - File Transfer Protocol with optional encryption"), L"" },
	{ SFTP,            L"sftp",      true,  true,    22, false, "SFTP - SSH File Transfer Protocol",                                       L"" },
	{ HTTP,            L"http",      true,  true,    80, false, "HTTP - Hypertext Transfer Protocol",                                      L"" },
	{ HTTPS,           L"https",     true,  true,   443, true,  fztranslate_mark("HTTPS - HTTP over TLS"),                                 L"" },
	{ FTPS,            L"ftps",      true,  true,   990, true,  fztranslate_mark("FTPS - FTP over implicit TLS"),                          L"" },
	{ FTPES,           L"ftpes",     true,  true,    21, true,  fztranslate_mark("FTPES - FTP over explicit TLS"),                         L"" },
	{ INSECURE_FTP,    L"ftp",       false, false,   21, true,  fztranslate_mark("FTP - Insecure File Transfer Protocol"),                 L"" },
	{ S3,              L"s3",        true,  true,   443, false, "S3 - Amazon Simple Storage Service",                                      L"" },
	{ STORJ,           L"storj",     true,  false, 7777, true,  fztranslate_mark("Storj (using legacy API key)"),                          L"" },
	{ WEBDAV,          L"webdav",    true,  true,   443, true,  fztranslate_mark("WebDAV using HTTPS"),                                    L"https" },
	{ AZURE_FILE,      L"azfile",    true,  true,   443, false, "Microsoft Azure File Storage Service",                                    L"https" },
	{ AZURE_BLOB,      L"azblob",    true,  true,   443, false, "Microsoft Azure Blob Storage Service",                                    L"https" },
	{ SWIFT,           L"swift",     true,  true,   443, false, "OpenStack Swift",                                                         L"https" },
	{ GOOGLE_CLOUD,    L"google",    true,  true,   443, false, "Google Cloud Storage",                                                    L"https" },
	{ GOOGLE_DRIVE,    L"gdrive",    true,  true,   443, false, "Google Drive",                                                            L"https" },
	{ DROPBOX,         L"dropbox",   true,  true,   443, false, "Dropbox",                                                                 L"https" },
	{ ONEDRIVE,        L"onedrive",  true,  true,   443, false, "Microsoft OneDrive",                                                      L"https" },
	{ B2,              L"b2",        true,  true,   443, false, "Backblaze B2",                                                            L"https" },
	{ BOX,             L"box",       true,  true,   443, false, "Box",                                                                     L"https" },
	{ INSECURE_WEBDAV, L"webdav",    true,  true,    80, true,  fztranslate_mark("WebDAV using HTTP (insecure)"),                          L"http" },
	{ RACKSPACE,       L"rackspace", true,  true,   443, false, "Rackspace Cloud Storage",                                                 L"https" },
	{ STORJ_GRANT,     L"storj",     true,  true,  7777, true,  fztranslate_mark("Storj - Decentralized Cloud Storage"),                   L"" },
	{ S3_SSO,          L"s3sso",     true,  true,   443, false, "S3 via IAM Identity Center (formerly SSO)",                               L"" },
	{ GOOGLE_CLOUD_SVC_ACC,  L"googleacc", true,  true,   443, false, fztranslate_mark("Google Cloud Storage with Service Account"),       L"https" },
	{ CLOUDFLARE_R2,   L"r2",        true,  true,   443, false, "Cloudflare R2",                                                           L"https" },
	{ UNKNOWN,         L"",          false, false,   21, false, "",                                                                        L"" }
};

static std::vector<ServerProtocol> const defaultProtocols = {
#if ENABLE_FTP
	FTP,
#endif
#if ENABLE_SFTP
	SFTP,
#endif
#if ENABLE_FTP
	FTPS,
	FTPES,
	INSECURE_FTP,
#endif
#if ENABLE_STORJ
	STORJ_GRANT,
#endif
};

static char const* const typeNames[SERVERTYPE_MAX] = {
	"Unix",
	"VMS",
	"DOS with backslash separators",
	"MVS, OS/390, z/OS",
	"VxWorks",
	"z/VM",
	"HP NonStop",
	fztranslate_mark("DOS-like with virtual paths"),
	"Cygwin",
	"DOS with forward-slash separators",
};

static const t_protocolInfo& GetProtocolInfo(ServerProtocol protocol)
{
	unsigned int i = 0;
	for ( ; protocolInfos[i].protocol != UNKNOWN; ++i) {
		if (protocolInfos[i].protocol == protocol) {
			break;
		}
	}
	return protocolInfos[i];
}

ServerProtocol CServer::GetProtocol() const
{
	return m_protocol;
}

ServerType CServer::GetType() const
{
	return m_type;
}

std::wstring CServer::GetHost() const
{
	return m_host;
}

unsigned int CServer::GetPort() const
{
	return m_port;
}

std::wstring CServer::GetUser() const
{
	return m_user;
}

bool CServer::operator==(const CServer &op) const
{
	if (m_protocol != op.m_protocol) {
		return false;
	}
	else if (m_type != op.m_type) {
		return false;
	}
	else if (m_host != op.m_host) {
		return false;
	}
	else if (m_port != op.m_port) {
		return false;
	}
	if (m_user != op.m_user) {
		return false;
	}
	if (m_timezoneOffset != op.m_timezoneOffset) {
		return false;
	}
	else if (m_pasvMode != op.m_pasvMode) {
		return false;
	}
	else if (m_encodingType != op.m_encodingType) {
		return false;
	}
	else if (m_encodingType == ENCODING_CUSTOM) {
		if (m_customEncoding != op.m_customEncoding) {
			return false;
		}
	}
	if (m_postLoginCommands != op.m_postLoginCommands) {
		return false;
	}
	if (m_bypassProxy != op.m_bypassProxy) {
		return false;
	}
	if (extraParameters_ != op.extraParameters_) {
		return false;
	}

	return true;
}

bool CServer::operator<(const CServer &op) const
{
	if (m_protocol < op.m_protocol) {
		return true;
	}
	else if (m_protocol > op.m_protocol) {
		return false;
	}

	if (m_type < op.m_type) {
		return true;
	}
	else if (m_type > op.m_type) {
		return false;
	}

	int cmp = m_host.compare(op.m_host);
	if (cmp < 0) {
		return true;
	}
	else if (cmp > 0) {
		return false;
	}

	if (m_port < op.m_port) {
		return true;
	}
	else if (m_port > op.m_port) {
		return false;
	}

	cmp = m_user.compare(op.m_user);
	if (cmp < 0) {
		return true;
	}
	else if (cmp > 0) {
		return false;
	}
	if (m_timezoneOffset < op.m_timezoneOffset) {
		return true;
	}
	else if (m_timezoneOffset > op.m_timezoneOffset) {
		return false;
	}

	if (m_pasvMode < op.m_pasvMode) {
		return true;
	}
	else if (m_pasvMode > op.m_pasvMode) {
		return false;
	}

	if (m_encodingType < op.m_encodingType) {
		return true;
	}
	else if (m_encodingType > op.m_encodingType) {
		return false;
	}

	if (m_encodingType == ENCODING_CUSTOM) {
		if (m_customEncoding < op.m_customEncoding) {
			return true;
		}
		else if (m_customEncoding > op.m_customEncoding) {
			return false;
		}
	}
	if (m_bypassProxy < op.m_bypassProxy) {
		return true;
	}
	else if (m_bypassProxy > op.m_bypassProxy) {
		return false;
	}

	if (extraParameters_ < op.extraParameters_) {
		return true;
	}
	else if (extraParameters_ > op.extraParameters_) {
		return false;
	}

	// Do not compare number of allowed multiple connections

	return false;
}

bool CServer::operator!=(const CServer &op) const
{
	return !(*this == op);
}

CServer::CServer(ServerProtocol protocol, ServerType type, std::wstring const& host, unsigned int port)
{
	m_protocol = protocol;
	m_type = type;
	m_host = host;
	if (port) {
		m_port = port;
	}
	else {
		m_port = GetDefaultPort(protocol);
	}
}

void CServer::SetType(ServerType type)
{
	m_type = type;
}

void CServer::SetProtocol(ServerProtocol serverProtocol)
{
	assert(serverProtocol != UNKNOWN);

	if (!ProtocolHasFeature(serverProtocol, ProtocolFeature::PostLoginCommands)) {
		m_postLoginCommands.clear();
	}

	m_protocol = serverProtocol;

	// Clear out parameters not supported by the current protocol
	if (!ProtocolHasUser(serverProtocol)) {
		m_user.clear();
	}

	std::map<std::string, std::wstring, std::less<>> oldParams;
	std::swap(extraParameters_, oldParams);
	for (auto const& param : oldParams) {
		SetExtraParameter(param.first, param.second);
	}
}

bool CServer::SetHost(std::wstring const& host, unsigned int port)
{
	if (host.empty()) {
		return false;
	}

	if (port < 1 || port > 65535) {
		return false;
	}

	m_host = host;
	m_port = port;

	if (m_protocol == UNKNOWN) {
		m_protocol = GetProtocolFromPort(m_port);
	}

	return true;
}

void CServer::SetUser(std::wstring const& user)
{
	m_user = user;
}

bool CServer::SetTimezoneOffset(int minutes)
{
	if (minutes > (60 * 24) || minutes < (-60 * 24)) {
		return false;
	}

	m_timezoneOffset = minutes;

	return true;
}

int CServer::GetTimezoneOffset() const
{
	return m_timezoneOffset;
}

PasvMode CServer::GetPasvMode() const
{
	return m_pasvMode;
}

void CServer::SetPasvMode(PasvMode pasvMode)
{
	m_pasvMode = pasvMode;
}

std::wstring CServer::Format(ServerFormat formatType) const
{
	return Format(formatType, Credentials());
}

std::wstring CServer::Format(ServerFormat formatType, Credentials const& credentials) const
{
	std::wstring server = m_host;

	t_protocolInfo const& info = GetProtocolInfo(m_protocol);

	if (server.find(':') != std::wstring::npos) {
		server = L"[" + server + L"]";
	}

	if (formatType == ServerFormat::host_only) {
		return server;
	}

	if (m_port != GetDefaultPort(m_protocol) || formatType == ServerFormat::with_port) {
		server += fz::sprintf(L":%d", m_port);
	}

	if (formatType == ServerFormat::with_optional_port || formatType == ServerFormat::with_port) {
		return server;
	}

	auto user = GetUser();
	if (m_protocol == STORJ) {
		// FIXME: The API key is not a user name. Move it into credentials section in a future version.
		// Hide it here, we shouldn't display the key like this.
		user.clear();
	}
	if (credentials.logonType_ != LogonType::anonymous) {
		// For now, only escape if formatting for URL.
		// Open question: Do we need some form of escapement for presentation within the GUI,
		// that deals e.g. with whitespace but does not touch Unicode characters?
		if (formatType == ServerFormat::url || formatType == ServerFormat::url_with_password) {
			user = fz::percent_encode_w(user);
		}
		if (!user.empty()) {
			if (formatType == ServerFormat::url_with_password) {
				auto pass = credentials.GetPass();
				if (!pass.empty()) {
					if (formatType == ServerFormat::url || formatType == ServerFormat::url_with_password) {
						pass = fz::percent_encode_w(pass);
					}
					server = user + L":" + pass + L"@" + server;
				}
			}
			else {
				server = user + L"@" + server;
			}
		}
	}

	if (formatType == ServerFormat::with_user_and_optional_port) {
		if (!info.alwaysShowPrefix && m_port == info.defaultPort) {
			return server;
		}
	}

	if (!info.prefix.empty()) {
		server = info.prefix + L"://" + server;
	}

	return server;
}

void CServer::clear()
{
	*this = CServer();
}

bool CServer::SetEncodingType(CharsetEncoding type, std::wstring const& encoding)
{
	if (type == ENCODING_CUSTOM && encoding.empty()) {
		return false;
	}

	m_encodingType = type;
	m_customEncoding = encoding;

	return true;
}

CharsetEncoding CServer::GetEncodingType() const
{
	return m_encodingType;
}

std::wstring CServer::GetCustomEncoding() const
{
	return m_customEncoding;
}

unsigned int CServer::GetDefaultPort(ServerProtocol protocol)
{
	const t_protocolInfo& info = GetProtocolInfo(protocol);

	return info.defaultPort;
}

ServerProtocol CServer::GetProtocolFromPort(unsigned int port, bool defaultOnly)
{
	for (unsigned int i = 0; protocolInfos[i].protocol != UNKNOWN; ++i) {
		if (protocolInfos[i].defaultPort == port) {
			return protocolInfos[i].protocol;
		}
	}

	if (defaultOnly) {
		return UNKNOWN;
	}

	// Else default to FTP
	return FTP;
}

std::wstring CServer::GetProtocolName(ServerProtocol protocol)
{
	t_protocolInfo const* protocolInfo = protocolInfos;
	while (protocolInfo->protocol != UNKNOWN) {
		if (protocolInfo->protocol != protocol) {
			++protocolInfo;
			continue;
		}

		if (protocolInfo->translateable) {
			return fz::translate(protocolInfo->name);
		}
		else {
			return fz::to_wstring(protocolInfo->name);
		}
	}

	return std::wstring();
}

ServerProtocol CServer::GetProtocolFromName(std::wstring const& name)
{
	const t_protocolInfo *protocolInfo = protocolInfos;
	while (protocolInfo->protocol != UNKNOWN) {
		if (protocolInfo->translateable) {
			if (fz::translate(protocolInfo->name) == name) {
				return protocolInfo->protocol;
			}
		}
		else {
			if (fz::to_wstring(protocolInfo->name) == name) {
				return protocolInfo->protocol;
			}
		}
		++protocolInfo;
	}

	return UNKNOWN;
}

bool CServer::SetPostLoginCommands(const std::vector<std::wstring>& postLoginCommands)
{
	if (!ProtocolHasFeature(m_protocol, ProtocolFeature::PostLoginCommands)) {
		m_postLoginCommands.clear();
		return false;
	}

	m_postLoginCommands = postLoginCommands;
	return true;
}

ServerProtocol CServer::GetProtocolFromPrefix(std::wstring const& prefix, ServerProtocol const hint)
{
	std::wstring const lower = fz::str_tolower_ascii(prefix);

	if (hint != UNKNOWN && !lower.empty()) {
		auto const& info = GetProtocolInfo(hint);
		if (info.prefix == lower || info.alternative_prefix == lower) {
			return hint;
		}
	}

	for (unsigned int i = 0; protocolInfos[i].protocol != UNKNOWN; ++i) {
		if (protocolInfos[i].prefix == lower && protocolInfos[i].parse_from_prefix) {
			return protocolInfos[i].protocol;
		}
	}

	return UNKNOWN;
}

std::wstring CServer::GetPrefixFromProtocol(ServerProtocol const protocol)
{
	const t_protocolInfo& info = GetProtocolInfo(protocol);

	return info.prefix;
}

std::vector<ServerProtocol> const& CServer::GetDefaultProtocols()
{
	return defaultProtocols;
}

void CServer::SetBypassProxy(bool val)
{
	m_bypassProxy = val;
}

bool CServer::GetBypassProxy() const
{
	return m_bypassProxy;
}

bool CServer::HasFeature(ProtocolFeature const feature) const
{
	return ProtocolHasFeature(m_protocol, feature);
}

bool CServer::ProtocolHasFeature(ServerProtocol const protocol, ProtocolFeature const feature)
{
	switch (feature) {
	case ProtocolFeature::DataTypeConcept:
	case ProtocolFeature::TransferMode:
	case ProtocolFeature::EnterCommand:
	case ProtocolFeature::PostLoginCommands:
		if (protocol == FTP || protocol == FTPS || protocol == FTPES || protocol == INSECURE_FTP) {
			return true;
		}
		break;
	case ProtocolFeature::Charset:
		if (protocol == FTP || protocol == FTPS || protocol == FTPES || protocol == INSECURE_FTP ||
			protocol == SFTP) {
			return true;
		}
		break;
	case ProtocolFeature::PreserveTimestamp:
		if (protocol == FTP || protocol == FTPS || protocol == FTPES || protocol == INSECURE_FTP ||
			protocol == SFTP ||
			protocol == AZURE_BLOB || protocol == AZURE_FILE || protocol == B2 || protocol == BOX ||
			protocol == DROPBOX || protocol == GOOGLE_CLOUD || protocol == GOOGLE_DRIVE ||
			protocol == ONEDRIVE || protocol == S3 || protocol == S3_SSO || protocol == CLOUDFLARE_R2 ||
			protocol == SWIFT || protocol == WEBDAV || protocol == GOOGLE_CLOUD_SVC_ACC) {
			return true;
		}
		break;
	case ProtocolFeature::ServerType:
		if (protocol == FTP || protocol == FTPS || protocol == FTPES || protocol == INSECURE_FTP) {
			return true;
		}
		break;
	case ProtocolFeature::UnixChmod:
		if (protocol == FTP || protocol == FTPS || protocol == FTPES || protocol == INSECURE_FTP ||
			protocol == SFTP) {
			return true;
		}
		break;
	case ProtocolFeature::DirectoryRename:
		if (protocol != AZURE_FILE) {
			return true;
		}
		break;
	case ProtocolFeature::RecursiveDelete:
		if (protocol == BOX || protocol == GOOGLE_DRIVE || protocol == DROPBOX || protocol == ONEDRIVE || protocol == B2) {
			return true;
		}
		break;
	case ProtocolFeature::ServerAssignedHome:
		if (protocol == FTP || protocol == FTPS || protocol == FTPES || protocol == INSECURE_FTP ||
			protocol == SFTP) {
			return true;
		}
		break;
	case ProtocolFeature::TemporaryUrl:
		if (protocol == B2 || protocol == S3 || protocol == S3_SSO || protocol == DROPBOX ||
			protocol == AZURE_BLOB || protocol == AZURE_FILE) {
			return true;
		}
		break;
	case ProtocolFeature::Security:
		return protocol != HTTP && protocol != INSECURE_FTP && protocol != INSECURE_WEBDAV;
	case ProtocolFeature::ProExclusive:
		switch (protocol) {
			case FTP:
			case FTPS:
			case FTPES:
			case INSECURE_FTP:
			case SFTP:
			case HTTP:
			case HTTPS:
			case STORJ:
			case STORJ_GRANT:
				return false;
			default:
				return true;
		}
		break;
	case ProtocolFeature::PruneOldVersions:
		if (protocol == B2 || protocol == BOX || protocol == GOOGLE_DRIVE || protocol == S3 || protocol == S3_SSO) {
			return true;
		}
		break;
	case ProtocolFeature::ListVersions:
		if (protocol == B2 || protocol == BOX || protocol == DROPBOX ||
			protocol == ONEDRIVE || protocol == GOOGLE_DRIVE || protocol == S3 || protocol == S3_SSO) {
			return true;
		}
		break;
	case ProtocolFeature::DownloadVersion:
		if (protocol == B2 || protocol == BOX || protocol == DROPBOX ||
			protocol == GOOGLE_DRIVE || protocol == S3 || protocol == S3_SSO) {
			return true;
		}
		break;
	case ProtocolFeature::DeleteVersion:
		if (protocol == B2 || protocol == BOX || protocol == GOOGLE_DRIVE || protocol == S3 || protocol == S3_SSO) {
			return true;
		}
		break;
	case ProtocolFeature::Share:
		if (protocol == BOX || protocol == DROPBOX || protocol == GOOGLE_DRIVE || protocol == ONEDRIVE) {
			return true;
		}
		break;
	}
	return false;
}

std::wstring CServer::GetNameFromServerType(ServerType type)
{
	if (type >= SERVERTYPE_MAX) {
		return {};
	}
	return fz::translate(typeNames[type]);
}

ServerType CServer::GetServerTypeFromName(std::wstring const& name)
{
	for (int i = 0; i < SERVERTYPE_MAX; ++i) {
		ServerType type = static_cast<ServerType>(i);
		if (name == CServer::GetNameFromServerType(type)) {
			return type;
		}
	}

	return UNIX;
}

void CServer::ClearExtraParameters()
{
	extraParameters_.clear();
}

std::wstring CServer::GetExtraParameter(std::string_view const& name) const
{
	auto it = extraParameters_.find(name);
	if (it != extraParameters_.cend()) {
		return it->second;
	}
	return std::wstring();
}

std::map<std::string, std::wstring, std::less<>> const& CServer::GetExtraParameters() const
{
	return extraParameters_;
}

bool CServer::HasExtraParameter(std::string_view const& name) const
{
	return extraParameters_.find(name) != extraParameters_.cend();
}

void CServer::SetExtraParameter(std::string_view const& name, std::wstring const& value)
{
	auto it = extraParameters_.find(name);
	if (value.empty()) {
		if (it != extraParameters_.cend()) {
			extraParameters_.erase(it);
		}
	}
	else {
		bool found = false;
		auto const& traits = ExtraServerParameterTraits(m_protocol);
		for (auto const& trait : traits) {
			if (trait.section_ != ParameterSection::credentials && name == trait.name_) {
				found = true;
				break;
			}
		}

		if (found) {
			if (it == extraParameters_.cend()) {
				extraParameters_.emplace(name, value);
			}
			else {
				it->second = value;
			}
		}
	}
}

void CServer::ClearExtraParameter(std::string_view const& name)
{
	auto it = extraParameters_.find(name);
	if (it != extraParameters_.cend()) {
		extraParameters_.erase(it);
	}
}

LogonType GetLogonTypeFromName(std::wstring const& name)
{
	if (name == _("Normal")) {
		return LogonType::normal;
	}
	else if (name == _("Ask for password")) {
		return LogonType::ask;
	}
	else if (name == _("Key file")) {
		return LogonType::key;
	}
	else if (name == _("Interactive")) {
		return LogonType::interactive;
	}
	else if (name == _("Account")) {
		return LogonType::account;
	}
	else if (name == _("Profile")) {
		return LogonType::profile;
	}
	else if (name == _("Application Default Credentials")) {
		return LogonType::adc;
	}
	else {
		return LogonType::anonymous;
	}
}

std::wstring GetNameFromLogonType(LogonType type)
{
	assert(type != LogonType::count);

	switch (type)
	{
	case LogonType::normal:
		return _("Normal");
	case LogonType::ask:
		return _("Ask for password");
	case LogonType::key:
		return _("Key file");
	case LogonType::interactive:
		return _("Interactive");
	case LogonType::account:
		return _("Account");
	case LogonType::profile:
		return _("Profile");
	case LogonType::adc:
		return _("Application Default Credentials");
	default:
		return _("Anonymous");
	}
}

void Credentials::SetPass(std::wstring const& password)
{
	if (logonType_ != LogonType::anonymous) {
		password_ = password;
	}
}

std::wstring Credentials::GetPass() const
{
	if (logonType_ == LogonType::anonymous) {
		return L"";
	}
	else {
		return password_;
	}
}

void Credentials::ClearExtraParameters()
{
	extraParameters_.clear();
}

std::wstring Credentials::GetExtraParameter(std::string_view const& name) const
{
	auto it = extraParameters_.find(name);
	if (it != extraParameters_.cend()) {
		return it->second;
	}
	return std::wstring();
}

std::map<std::string, std::wstring, std::less<>> const& Credentials::GetExtraParameters() const
{
	return extraParameters_;
}

bool Credentials::HasExtraParameter(std::string_view const& name) const
{
	return extraParameters_.find(name) != extraParameters_.cend();
}

void Credentials::SetExtraParameter(ServerProtocol protocol, std::string_view const& name, std::wstring const& value)
{
	auto it = extraParameters_.find(name);
	if (value.empty()) {
		if (it != extraParameters_.cend()) {
			extraParameters_.erase(it);
		}
	}
	else {
		bool found = false;
		auto const& traits = ExtraServerParameterTraits(protocol);
		for (auto const& trait : traits) {
			if (trait.section_ == ParameterSection::credentials && name == trait.name_) {
				found = true;
				break;
			}
		}

		if (found) {
			if (it == extraParameters_.cend()) {
				extraParameters_.emplace(name, value);
			}
			else {
				it->second = value;
			}
		}
	}
}

void Credentials::SetExtraParameters(ServerProtocol protocol, std::map<std::string, std::wstring, std::less<>> const& extraParameters)
{
	for (auto const& param : extraParameters) {
		SetExtraParameter(protocol, param.first, param.second);
	}
}

std::vector<LogonType> GetSupportedLogonTypes(ServerProtocol protocol)
{
	switch (protocol) {
	case FTP:
	case HTTP:
	case FTPS:
	case FTPES:
	case INSECURE_FTP:
		return {LogonType::anonymous, LogonType::normal, LogonType::ask, LogonType::interactive, LogonType::account};
	case SFTP:
		return {LogonType::anonymous, LogonType::normal, LogonType::ask, LogonType::interactive, LogonType::key};
	case S3:
		return {LogonType::anonymous, LogonType::normal, LogonType::ask, LogonType::profile};
	case S3_SSO:
		return {LogonType::interactive, LogonType::profile};
	case STORJ:
	case STORJ_GRANT:
	case AZURE_FILE:
	case AZURE_BLOB:
	case SWIFT:
	case B2:
	case RACKSPACE:
		return {LogonType::normal, LogonType::ask};
	case WEBDAV:
	case INSECURE_WEBDAV:
		return {LogonType::anonymous, LogonType::normal, LogonType::ask};
	case GOOGLE_CLOUD:
	case GOOGLE_DRIVE:
	case DROPBOX:
	case ONEDRIVE:
	case BOX:
		return {LogonType::interactive};
	case HTTPS:
	case UNKNOWN:
		return {LogonType::anonymous};
	case GOOGLE_CLOUD_SVC_ACC:
		return {LogonType::key, LogonType::adc};
	case CLOUDFLARE_R2:
		return {LogonType::normal, LogonType::interactive};
	}

	return {LogonType::anonymous};
}

bool FZC_PUBLIC_SYMBOL IsSupportedLogonType(ServerProtocol protocol, LogonType type)
{
	auto const types = GetSupportedLogonTypes(protocol);
	for (auto const& t : types) {
		if (t == type) {
			return true;
		}
	}
	return false;
}

std::vector<ParameterTraits> const& ExtraServerParameterTraits(ServerProtocol protocol)
{
	switch (protocol) {
	case FTP:
	case FTPS:
	case FTPES:
	case INSECURE_FTP:
		{
			static std::vector<ParameterTraits> const ret = []() {
				std::vector<ParameterTraits> ret;
				ret.emplace_back(ParameterTraits{"otp_code", ParameterSection::credentials, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				return ret;
			}();
			return ret;
		}
	case SFTP:
		{
			static std::vector<ParameterTraits> const ret = []() {
				std::vector<ParameterTraits> ret;
				ret.emplace_back(ParameterTraits{"allow_non_crlf_identification_string", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom | ParameterTraits::content_transparent, std::wstring(), std::wstring()});
				ret.emplace_back(ParameterTraits{"allow_agent_keys_of_unknown_type", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom | ParameterTraits::content_transparent, std::wstring(), std::wstring()});
				ret.emplace_back(ParameterTraits{"ignore_unknown_flags_in_attributes", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				return ret;
			}();
			return ret;
		}
	case GOOGLE_CLOUD:
		{
			static std::vector<ParameterTraits> const ret = []() {
				std::vector<ParameterTraits> ret;
				ret.emplace_back(ParameterTraits{"login_hint", ParameterSection::user, ParameterTraits::optional, std::wstring(), _("Name or email address")});
				ret.emplace_back(ParameterTraits{"oauth_identity", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				return ret;
			}();
			return ret;
		}
	case GOOGLE_CLOUD_SVC_ACC:
		{
			static std::vector<ParameterTraits> const ret = []() {
				std::vector<ParameterTraits> ret;
				ret.emplace_back(ParameterTraits{"oauth_identity", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				return ret;
			}();
			return ret;
		}
	case SWIFT:
		{
			static std::vector<ParameterTraits> const ret = []() {
				std::vector<ParameterTraits> ret;
				ret.emplace_back(ParameterTraits{"identpath", ParameterSection::host, 0, std::wstring(), _("Path of identity service")});
				ret.emplace_back(ParameterTraits{"identuser", ParameterSection::user, ParameterTraits::optional, std::wstring(), std::wstring()});
				ret.emplace_back(ParameterTraits{"keystone_version", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				ret.emplace_back(ParameterTraits{"domain", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, L"Default", std::wstring()});
				return ret;
			}();
			return ret;
		}
	case RACKSPACE:
		{
			static std::vector<ParameterTraits> const ret = []() {
				std::vector<ParameterTraits> ret;
				ret.emplace_back(ParameterTraits{"identpath", ParameterSection::host, 0, L"/v2.0/tokens", _("Path of identity service")});
				ret.emplace_back(ParameterTraits{"identuser", ParameterSection::user, ParameterTraits::optional, std::wstring(), std::wstring()});
				return ret;
			}();
			return ret;
		}
	case S3:
	case S3_SSO:
		{
			static std::vector<ParameterTraits> const ret = []() {
				std::vector<ParameterTraits> ret;
				ret.emplace_back(ParameterTraits{"ssealgorithm", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				ret.emplace_back(ParameterTraits{"ssekmskey", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				ret.emplace_back(ParameterTraits{"ssecustomerkey", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				ret.emplace_back(ParameterTraits{"stsrolearn", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				ret.emplace_back(ParameterTraits{"stsmfaserial", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				ret.emplace_back(ParameterTraits{"region", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				ret.emplace_back(ParameterTraits{"original_profile", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				ret.emplace_back(ParameterTraits{"ssoregion", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				ret.emplace_back(ParameterTraits{"ssorole", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				ret.emplace_back(ParameterTraits{"ssourl", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				ret.emplace_back(ParameterTraits{"accelerate", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				return ret;
			}();
			return ret;
		}
	case GOOGLE_DRIVE:
	case ONEDRIVE:
		{
			static std::vector<ParameterTraits> const ret = []() {
				std::vector<ParameterTraits> ret;
				ret.emplace_back(ParameterTraits{"login_hint", ParameterSection::user, ParameterTraits::optional, std::wstring(), _("Name or email address")});
				ret.emplace_back(ParameterTraits{"oauth_identity", ParameterSection::extra, ParameterTraits::optional| ParameterTraits::custom, std::wstring(), std::wstring()});
				return ret;
			}();
			return ret;
		}
	case DROPBOX:
		{
			static std::vector<ParameterTraits> const ret = []() {
				std::vector<ParameterTraits> ret;
				ret.emplace_back(ParameterTraits{"oauth_identity", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				ret.emplace_back(ParameterTraits{"root_namespace", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				return ret;
			}();
			return ret;
		}
	case BOX:
		{
			static std::vector<ParameterTraits> const ret = []() {
				std::vector<ParameterTraits> ret;
				ret.emplace_back(ParameterTraits{"oauth_identity", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				return ret;
			}();
			return ret;
		}
	case STORJ:
		{
			static std::vector<ParameterTraits> const ret = []() {
				std::vector<ParameterTraits> ret;
				ret.emplace_back(ParameterTraits{"passphrase_hash", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				return ret;
			}();
			return ret;
		}
	case STORJ_GRANT:
		{
			static std::vector<ParameterTraits> const ret = []() {
				std::vector<ParameterTraits> ret;
				ret.emplace_back(ParameterTraits{"credentials_hash", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				return ret;
			}();
			return ret;
		}

	case CLOUDFLARE_R2:
		{
			static std::vector<ParameterTraits> const ret = []() {
				std::vector<ParameterTraits> ret;
				ret.emplace_back(ParameterTraits{"identuser", ParameterSection::user, ParameterTraits::optional, std::wstring(), std::wstring()});
				ret.emplace_back(ParameterTraits{"jurisdiction", ParameterSection::extra, ParameterTraits::optional | ParameterTraits::custom, std::wstring(), std::wstring()});
				return ret;
			}();
			return ret;
		}

	default:
		break;
	}

	static std::vector<ParameterTraits> empty;
	return empty;
}

std::tuple<std::wstring, std::wstring> GetDefaultHost(ServerProtocol protocol)
{
	switch (protocol)
	{
	case AZURE_FILE:
		return std::tuple<std::wstring, std::wstring>{L"file.core.windows.net", L""};
	case AZURE_BLOB:
		return std::tuple<std::wstring, std::wstring>{L"blob.core.windows.net", L""};
	case GOOGLE_CLOUD:
	case GOOGLE_CLOUD_SVC_ACC:
		return std::tuple<std::wstring, std::wstring>{L"storage.googleapis.com", L""};
	case GOOGLE_DRIVE:
		return std::tuple<std::wstring, std::wstring>{L"www.googleapis.com", L""};
	case S3:
	case S3_SSO:
		return std::tuple<std::wstring, std::wstring>{L"s3.amazonaws.com", L""};
	case DROPBOX:
		return std::tuple<std::wstring, std::wstring>{L"api.dropboxapi.com", L""};
	case ONEDRIVE:
		return std::tuple<std::wstring, std::wstring>{L"graph.microsoft.com", L""};
	case B2:
		return std::tuple<std::wstring, std::wstring>{L"api.backblazeb2.com", L""};
	case BOX:
		return std::tuple<std::wstring, std::wstring>{L"api.box.com", L""};
	case RACKSPACE:
		return std::tuple<std::wstring, std::wstring>{L"identity.api.rackspacecloud.com", L""};
	case STORJ:
	case STORJ_GRANT:
		return std::tuple<std::wstring, std::wstring>{L"us-central-1.tardigrade.io", L""};
	case CLOUDFLARE_R2:
		return std::tuple<std::wstring, std::wstring>{ L"r2.cloudflarestorage.com", L"" };
	default:
		break;
	}

	return std::tuple<std::wstring, std::wstring>{};
}

bool ProtocolHasUser(ServerProtocol protocol)
{
	return protocol != DROPBOX && protocol != ONEDRIVE && protocol != BOX &&
		   protocol != GOOGLE_DRIVE && protocol != STORJ_GRANT && protocol != GOOGLE_CLOUD_SVC_ACC;
}

bool CServer::SameResource(CServer const& other) const
{
	// We include post-login commands as it may be used for things like the HOST command.
	// We include proxy parameters as a hostname may resolve to different servers depending on whether a proxy is used.

	auto l = std::tie(m_protocol, m_host, m_port, m_user, m_postLoginCommands);
	auto r = std::tie(other.m_protocol, other.m_host, other.m_port, other.m_user, other.m_postLoginCommands);
	if (l != r) {
		return false;
	}

	auto const& traits = ExtraServerParameterTraits(m_protocol);
	for (auto const& trait : traits) {
		if (trait.flags_ & ParameterTraits::content_transparent) {
			continue;
		}
		if (GetExtraParameter(trait.name_) != other.GetExtraParameter(trait.name_)) {
			return false;
		}
	}

	return true;
}

bool CServer::SameContent(CServer const& other) const
{
	if (!SameResource(other)) {
		return false;
	}

	auto l = std::tie(m_timezoneOffset, m_encodingType, m_customEncoding);
	auto r = std::tie(other.m_timezoneOffset, other.m_encodingType, other.m_customEncoding);

	return l == r;
}

CaseSensitivity GetCaseSensitivity(ServerProtocol protocol)
{
	switch (protocol) {
	case B2:
	case GOOGLE_DRIVE:
		return CaseSensitivity::yes;
	case BOX:
	case ONEDRIVE:
		return CaseSensitivity::no;
	default:
		return CaseSensitivity::unspecified;
	}
}

CaseSensitivity CServer::GetCaseSensitivity() const
{
	return ::GetCaseSensitivity(m_protocol);
}
