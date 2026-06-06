#ifndef FILEZILLAINTERFACE_VERIFYHOSTKEYDIALOG_HEADER
#define FILEZILLAINTERFACE_VERIFYHOSTKEYDIALOG_HEADER

#include "../commonui/hostkey_store.h"

class CVerifyHostkeyDialog final : public XmlHostkeyStore
{
public:
	using XmlHostkeyStore::XmlHostkeyStore;

protected:
	virtual bool AskUnknownHostkey(Site const& site, fz::ssh::public_key const& key, fz::ssh::algorithm_info const& info, verification_flags flags, bool &remember) override;
	virtual void OnSaveError(std::wstring const& message) override;
};

#endif
