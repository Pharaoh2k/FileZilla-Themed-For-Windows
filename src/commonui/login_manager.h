#ifndef FILEZILLA_COMMONUI_LOGINMANAGER_HEADER
#define FILEZILLA_COMMONUI_LOGINMANAGER_HEADER

#include "site.h"
#include "visibility.h"

#include <fzssh/ssh.hpp>

#include <libfilezilla/encryption.hpp>

class PasswordRequest;

#include <map>
#include <list>
#include <optional>
#include <string>
#include <vector>

// The purpose of this class is to manage some aspects of the login
// behaviour. These are:
// - Query credentials for servers with ASK or INTERACTIVE logontype
// - Storage of passwords for ASK servers for duration of current session

class FZCUI_PUBLIC_SYMBOL login_manager
{
public:
	virtual ~login_manager() = default;

	bool GetPassword(Site & site, bool silent);

	bool GetPassword(PasswordRequest & req, bool silent);

	void CachedPasswordFailed(CServer const& server);

	void RememberPassword(Site & site);

	fz::private_key GetDecryptor(fz::public_key const& pub, bool * forgotten = nullptr);
	void Remember(fz::private_key const& key, std::string_view const& pass = std::string_view());

	void RememberAsForgotten(fz::public_key const& pub_key);

protected:

	virtual bool query_unprotect_site(Site&) = 0;
	virtual bool query_credentials(Site&, bool /*canRemember*/) = 0;

	// Session password cache for Ask-type servers
	struct t_passwordcache final
	{
		std::wstring host;
		unsigned int port{};
		std::wstring user;
		std::wstring password;
	};

	std::list<t_passwordcache>::iterator FindItem(CServer const& server);

	std::list<t_passwordcache> m_passwordCache;

	std::map<fz::public_key, fz::private_key> decryptors_;
	std::vector<std::string> decryptorPasswords_;
};

class FZCUI_PUBLIC_SYMBOL keyfile_password_manager
{
public:
	virtual ~keyfile_password_manager() = default;

	std::optional<std::string> get_password(Site const& site, std::string const& file, std::string const& fingerprint, std::string const& comment, bool silent);
	void clear_password(std::string const& file, std::string const& fingerprint);

protected:
	virtual std::optional<std::string> query_password(Site const& site, std::string const& file, std::string const& fingerprint, std::string const& comment, bool & remember) = 0;

private:
	// filename and fingerprint
	using entry = std::pair<std::string, std::string>;
	std::map<entry, std::string> passwords_;
};

#endif
