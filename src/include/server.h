#ifndef FILEZILLA_ENGINE_SERVER_HEADER
#define FILEZILLA_ENGINE_SERVER_HEADER

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "visibility.h"

enum ServerProtocol
{
	// Never change any existing values or user's saved sites will become
	// corrupted
	UNKNOWN = -1,
	FTP, // FTP, attempts AUTH TLS
	SFTP,
	HTTP,
	FTPS, // Implicit SSL
	FTPES, // Explicit SSL
	HTTPS,
	INSECURE_FTP, // Insecure, as the name suggests

	S3, // Amazon S3 or compatible

	STORJ,

	WEBDAV,

	AZURE_FILE,
	AZURE_BLOB,

	SWIFT,

	GOOGLE_CLOUD,
	GOOGLE_DRIVE,

	DROPBOX,

	ONEDRIVE,

	B2,

	BOX,

	INSECURE_WEBDAV,

	RACKSPACE,

	STORJ_GRANT,

	S3_SSO,

	GOOGLE_CLOUD_SVC_ACC,

	CLOUDFLARE_R2,

	MAX_VALUE = CLOUDFLARE_R2
};

enum ServerType : unsigned
{
	UNIX,
	VMS,
	DOS, // Backslashes as preferred separator
	MVS,
	VXWORKS,
	ZVM,
	HPNONSTOP,
	DOS_VIRTUAL,
	CYGWIN,
	DOS_FWD_SLASHES, // Forwardslashes as preferred separator

	SERVERTYPE_MAX
};

enum PasvMode
{
	MODE_DEFAULT,
	MODE_ACTIVE,
	MODE_PASSIVE
};

enum class ServerFormat
{
	host_only,
	with_optional_port,
	with_port,
	with_user_and_optional_port,
	url,
	url_with_password
};

enum CharsetEncoding
{
	ENCODING_UTF8,
	ENCODING_CUSTOM
};

enum class ProtocolFeature
{
	DataTypeConcept,		// Some protocol distinguish between ASCII and binary files for line-ending conversion.
	TransferMode,
	PreserveTimestamp,
	Charset,
	ServerType,
	EnterCommand,
	DirectoryRename,
	PostLoginCommands,
	RecursiveDelete,
	ServerAssignedHome,
	TemporaryUrl,
	Security, // Encryption, integrity protection and authentication
	UnixChmod,
	ProExclusive,
	PruneOldVersions,
	ListVersions,
	DownloadVersion,
	DeleteVersion,
	Share
};

enum class CaseSensitivity
{
	unspecified,
	yes,
	no
};

CaseSensitivity GetCaseSensitivity(ServerProtocol protocol);

class Credentials;
class CServerPath;
class FZC_PUBLIC_SYMBOL CServer final
{
public:

	// No error checking is done in the constructors
	CServer() = default;
	CServer(ServerProtocol protocol, ServerType type, std::wstring const& host, unsigned int);

	CServer(CServer const&) = default;
	CServer& operator=(CServer const&) = default;

	CServer(CServer &&) noexcept = default;
	CServer& operator=(CServer &&) noexcept = default;

	void clear();

	void SetType(ServerType type);

	ServerProtocol GetProtocol() const;
	ServerType GetType() const;
	std::wstring GetHost() const;
	unsigned int GetPort() const;
	std::wstring GetUser() const;
	int GetTimezoneOffset() const;
	PasvMode GetPasvMode() const;
	bool GetBypassProxy() const;

	void SetProtocol(ServerProtocol serverProtocol);
	bool SetHost(std::wstring const& host, unsigned int port);

	void SetUser(std::wstring const& user);

	bool operator==(const CServer &op) const;
	bool operator<(const CServer &op) const;
	bool operator!=(const CServer &op) const;

	// Returns whether the argument refers to the same resource.
	// Compares things like protocol and hostname, but excludes things like the name or the timezone offset.
	bool SameResource(CServer const& other) const;

	// Stricter than SameResource, also compares things like the encoding.
	// Can be used as key comparator in content caches
	bool SameContent(CServer const& other) const;

	bool SetTimezoneOffset(int minutes);
	void SetPasvMode(PasvMode pasvMode);

	std::wstring Format(ServerFormat formatType) const;
	std::wstring Format(ServerFormat formatType, Credentials const& credentials) const;

	bool SetEncodingType(CharsetEncoding type, std::wstring const& encoding = std::wstring());
	CharsetEncoding GetEncodingType() const;
	std::wstring GetCustomEncoding() const;

	static unsigned int GetDefaultPort(ServerProtocol protocol);
	static ServerProtocol GetProtocolFromPort(unsigned int port, bool defaultOnly = false);

	static std::wstring GetProtocolName(ServerProtocol protocol);
	static ServerProtocol GetProtocolFromName(std::wstring const& name);

	static ServerProtocol GetProtocolFromPrefix(std::wstring const& prefix, ServerProtocol const hint = UNKNOWN);
	static std::wstring GetPrefixFromProtocol(ServerProtocol const protocol);
	static std::vector<ServerProtocol> const& GetDefaultProtocols();

	bool HasFeature(ProtocolFeature const feature) const;
	static bool ProtocolHasFeature(ServerProtocol const protocol, ProtocolFeature const feature);

	CaseSensitivity GetCaseSensitivity() const;

	// These commands will be executed after a successful login.
	std::vector<std::wstring> const& GetPostLoginCommands() const { return m_postLoginCommands; }
	bool SetPostLoginCommands(std::vector<std::wstring> const& postLoginCommands);

	void SetBypassProxy(bool val);

	static std::wstring GetNameFromServerType(ServerType type);
	static ServerType GetServerTypeFromName(std::wstring const& name);

	bool empty() const { return m_host.empty(); }
	explicit operator bool() const { return !empty(); }

	void ClearExtraParameters();
	std::wstring GetExtraParameter(std::string_view const& name) const;
	std::map<std::string, std::wstring, std::less<>> const& GetExtraParameters() const;
	bool HasExtraParameter(std::string_view const& name) const;
	void SetExtraParameter(std::string_view const& name, std::wstring const& value);
	void ClearExtraParameter(std::string_view const& name);

protected:
	ServerProtocol m_protocol{UNKNOWN};
	ServerType m_type{UNIX};
	std::wstring m_host;
	std::wstring m_user;
	unsigned int m_port{21};
	int m_timezoneOffset{};
	PasvMode m_pasvMode{MODE_DEFAULT};
	bool m_bypassProxy{};
	CharsetEncoding m_encodingType{ENCODING_UTF8};
	std::wstring m_customEncoding;

	std::vector<std::wstring> m_postLoginCommands;

	std::map<std::string, std::wstring, std::less<>> extraParameters_;
};


enum class LogonType
{
	anonymous,
	normal,
	ask, // ask should not be sent to the engine, it's intended to be used by the interface
	interactive,
	account,
	key,
	profile,		// S3 profile
	adc,			// Google Cloud Application Default Credenttials

	count
};

std::wstring FZC_PUBLIC_SYMBOL GetNameFromLogonType(LogonType type);
LogonType FZC_PUBLIC_SYMBOL GetLogonTypeFromName(std::wstring const& name);

std::vector<LogonType> FZC_PUBLIC_SYMBOL GetSupportedLogonTypes(ServerProtocol protocol);
bool FZC_PUBLIC_SYMBOL IsSupportedLogonType(ServerProtocol protocol, LogonType type);

namespace ParameterSection {
    enum type {
		host,
		user,
		credentials,
		extra,

		section_count
	};
}

struct ParameterTraits
{
	std::string name_;

	ParameterSection::type section_;

	enum flags : unsigned char {
		optional = 0x01,
		numeric = 0x02,
		content_transparent = 0x04, // Parameter used for any purpose that does not affect site content
		custom = 0x08 // Hint for the UI to not generate input fields, trait needs custom handling
	};
	unsigned char flags_;
	std::wstring default_;
	std::wstring hint_;
};

std::vector<ParameterTraits> FZC_PUBLIC_SYMBOL const& ExtraServerParameterTraits(ServerProtocol protocol);

std::tuple<std::wstring, std::wstring> FZC_PUBLIC_SYMBOL GetDefaultHost(ServerProtocol protocol);

bool FZC_PUBLIC_SYMBOL ProtocolHasUser(ServerProtocol protocol);

class FZC_PUBLIC_SYMBOL Credentials
{
public:
	virtual ~Credentials() = default;

	bool operator==(Credentials const& rhs) const {
		return
			logonType_ == rhs.logonType_ &&
			password_ == rhs.password_ &&
			account_ == rhs.account_ &&
			keyFile_ == rhs.keyFile_;
	}

	void SetPass(std::wstring const& password);
	std::wstring GetPass() const;

	LogonType logonType_{LogonType::anonymous};
	std::wstring account_;
	std::wstring keyFile_;

	void ClearExtraParameters();
	std::wstring GetExtraParameter(std::string_view const& name) const;
	std::map<std::string, std::wstring, std::less<>> const& GetExtraParameters() const;
	bool HasExtraParameter(std::string_view const& name) const;
	void SetExtraParameter(ServerProtocol protocol, std::string_view const& name, std::wstring const& value);
	void SetExtraParameters(ServerProtocol protocol, std::map<std::string, std::wstring, std::less<>> const& extraParameters);

protected:
	std::wstring password_;
	std::map<std::string, std::wstring, std::less<>> extraParameters_;
};

struct FZC_PUBLIC_SYMBOL ServerHandleData {
protected:
	ServerHandleData() = default;
	virtual ~ServerHandleData() = default;
	ServerHandleData(ServerHandleData const&) = default;
	ServerHandleData& operator=(ServerHandleData const&) = default;
};
typedef std::weak_ptr<ServerHandleData> ServerHandle;

#endif
