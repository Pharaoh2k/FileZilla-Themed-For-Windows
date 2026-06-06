#include "protect.h"

void protect(ProtectedCredentials& creds, login_manager& lim, COptionsBase& options)
{
	if (creds.logonType_ != LogonType::normal && creds.logonType_ != LogonType::account) {
		creds.SetPass(L"");
		return;
	}

	bool kiosk_mode = options.get_int(OPTION_DEFAULT_KIOSKMODE) != 0;
	if (kiosk_mode) {
		if (creds.logonType_ == LogonType::normal || creds.logonType_ == LogonType::account) {
			creds.SetPass(L"");
			creds.logonType_ = LogonType::ask;
		}
	}
	else {
		auto key = fz::public_key::from_base64(fz::to_utf8(options.get_string(OPTION_MASTERPASSWORDENCRYPTOR)));
		protect(lim, creds, key);
	}
}
