#ifndef FILEZILLA_INTERFACE_LOGINMANAGER_HEADER
#define FILEZILLA_INTERFACE_LOGINMANAGER_HEADER

#include "../commonui/login_manager.h"

#include <list>

// The purpose of this class is to manage some aspects of the login
// behaviour. These are:
// - Password dialog for servers with ASK or INTERACTIVE logontype
// - Storage of passwords for ASK servers for duration of current session

class CLoginManager : public login_manager
{
public:
	bool AskDecryptor(fz::public_key const& pub, bool allowForgotten, bool allowCancel);

	static std::string get_topt(Site const& site);
	static std::optional<std::vector<std::string>> interactive_prompt(Site const& site, std::string const& name, std::string const& instruction, fz::ssh::keyboard_interactive_prompts const& p);

protected:
	virtual bool query_unprotect_site(Site & site) override;
	virtual bool query_credentials(Site & site, bool canRemember) override;
};

class KeyfilePasswordManager final : public keyfile_password_manager
{
protected:
	virtual std::optional<std::string> query_password(Site const& site, std::string const& file, std::string const& fingerprint, std::string const& comment, bool & remember) override;
};

#endif
