#ifndef FILEZILLA_COMMONUI_HOSTKEY_STORE_HEADER
#define FILEZILLA_COMMONUI_HOSTKEY_STORE_HEADER

#include "site.h"
#include "xml_file.h"
#include "visibility.h"

#include <map>

namespace fz::ssh {
class algorithm_info;
class public_key;
}

class COptionsBase;

class FZCUI_PUBLIC_SYMBOL HostkeyStore
{
public:
	virtual ~HostkeyStore();

	bool IsTrusted(Site const& site, fz::ssh::public_key const& key, fz::ssh::algorithm_info const& info, bool silent);

	enum class verification_flags : unsigned {
		unknown = 1,
		changed = 2,
		insecure = 4
	};

protected:
	static bool is_insecure_algorithm(std::string_view const& alg);
	static std::string get_insecure_algorithms(fz::ssh::algorithm_info algs);

	virtual bool AskUnknownHostkey(Site const& site, fz::ssh::public_key const& key, fz::ssh::algorithm_info const& info, verification_flags flags, bool &remember) = 0;

	virtual void Load() = 0;
	virtual void Save(bool silent) = 0;

	struct entry
	{
		std::vector<std::string> hostkeys_; // pubkey blobs
		std::string allowed_insecure_algorithms_;

		bool known_key(std::string_view const& key) const;
	};

	// maps host/port to entry
	std::map<std::pair<std::string, uint16_t>, entry> session_trusted_keys_;
	std::map<std::pair<std::string, uint16_t>, entry> trusted_keys_;
};

inline HostkeyStore::verification_flags operator|(HostkeyStore::verification_flags lhs, HostkeyStore::verification_flags rhs) {
	return static_cast<HostkeyStore::verification_flags>(static_cast<std::underlying_type_t<HostkeyStore::verification_flags>>(lhs) | static_cast<std::underlying_type_t<HostkeyStore::verification_flags>>(rhs));
}
inline HostkeyStore::verification_flags& operator|=(HostkeyStore::verification_flags & lhs, HostkeyStore::verification_flags rhs) {
	lhs = lhs | rhs;
	return lhs;
}
inline bool operator&(HostkeyStore::verification_flags lhs, HostkeyStore::verification_flags rhs) {
	return (static_cast<std::underlying_type_t<HostkeyStore::verification_flags>>(lhs) & static_cast<std::underlying_type_t<HostkeyStore::verification_flags>>(rhs)) != 0;
}


class FZCUI_PUBLIC_SYMBOL XmlHostkeyStore : public HostkeyStore
{
public:
	explicit XmlHostkeyStore(COptionsBase & options);

protected:
	virtual void Load() override;
	virtual void Save(bool silent) override;

	virtual void OnSaveError(std::wstring const& message) = 0;

	COptionsBase & options_;
	CXmlFile file_;
};

// Does not overwrite data for known host/port entries
void FZCUI_PUBLIC_SYMBOL ImportPuttyHostkeys(COptionsBase & options);

#endif
