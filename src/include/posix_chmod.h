#ifndef FILEZILLA_ENGINE_POSIX_CHMOD_HEADER
#define FILEZILLA_ENGINE_POSIX_CHMOD_HEADER

#include "visibility.h"

#include <cstdint>
#include <optional>
#include <string>

enum class posix_permissions : uint16_t
{
	none,
	user_read = 0400,
	user_write = 0200,
	user_execute = 0100,
	group_read = 040,
	group_write = 020,
	group_execute = 010,
	other_read = 04,
	other_write = 02,
	other_execute = 01,
	setuid = 04000,
	setgid = 02000,
	sticky = 01000,

	all_read =    0444,
	all_write =   0222,
	all_execute = 0111,
	all_setid =  06000,

	user_mask =   0700,
	group_mask =   070,
	other_mask =    07,
	ugo_mask =    0777,
	mask =       07777,
};

inline posix_permissions operator&(posix_permissions lhs, posix_permissions rhs) {
	return static_cast<posix_permissions>((static_cast<std::underlying_type_t<posix_permissions>>(lhs) & static_cast<std::underlying_type_t<posix_permissions>>(rhs)));
}
inline posix_permissions& operator&=(posix_permissions & lhs, posix_permissions rhs) {
	lhs = lhs & rhs;
	return lhs;
}

inline posix_permissions operator|(posix_permissions lhs, posix_permissions rhs) {
	return static_cast<posix_permissions>(static_cast<std::underlying_type_t<posix_permissions>>(lhs) | static_cast<std::underlying_type_t<posix_permissions>>(rhs));
}
inline posix_permissions& operator|=(posix_permissions & lhs, posix_permissions rhs) {
	lhs = lhs | rhs;
	return lhs;
}

inline posix_permissions operator^(posix_permissions lhs, posix_permissions rhs) {
	return static_cast<posix_permissions>(static_cast<std::underlying_type_t<posix_permissions>>(lhs) ^ static_cast<std::underlying_type_t<posix_permissions>>(rhs));
}

inline posix_permissions operator~(posix_permissions op) {
	return static_cast<posix_permissions>(~static_cast<std::underlying_type_t<posix_permissions>>(op)) & posix_permissions::mask;
}

struct posix_chmod {
	explicit operator bool() const {
		return mask_ != posix_permissions::none;
	}
	posix_permissions perms_{};
	posix_permissions mask_{};
};

posix_permissions FZC_PUBLIC_SYMBOL apply(posix_permissions orig, posix_chmod change);

std::optional<posix_permissions> FZC_PUBLIC_SYMBOL parse_permissions(std::string_view in);
posix_chmod FZC_PUBLIC_SYMBOL parse_chmod(std::string_view in);

std::string FZC_PUBLIC_SYMBOL to_string(posix_permissions perms); // as in rwxrwxrwx
std::string FZC_PUBLIC_SYMBOL to_octal(posix_permissions perms, bool always_with_setid_sticky);
std::string FZC_PUBLIC_SYMBOL to_string(posix_chmod chmod);

#endif
