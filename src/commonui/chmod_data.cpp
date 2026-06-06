#include "chmod_data.h"

#include <libfilezilla/string.hpp>

#include <cstring>

using namespace std::literals;

posix_permissions ChmodData::Apply(std::string_view old, bool dir) const
{
	size_t pos = old.find('(');
	if (pos != std::string::npos && old.back() == ')') {
		// MLSD permissions:
		//   foo (0644)
		old = old.substr(pos, old.size() - pos - 2);
	}

	auto perms = parse_permissions(old);
	if (!perms) {
		// Use 0755 for dirs and 0644 for files
		perms = posix_permissions::all_read | posix_permissions::user_write;
		if (dir) {
			*perms |= posix_permissions::all_execute;
		}
	}

	perms = apply(*perms, chmod_);
	return *perms;
}

posix_permissions ChmodData::Apply(posix_permissions const& perms) const
{
	return apply(perms, chmod_);
}
