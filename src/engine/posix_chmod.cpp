#include "../include/posix_chmod.h"

#include <libfilezilla/string.hpp>

#include <cstring>

using namespace std::literals;

#if !FZ_WINDOWS
// I can't find an example where the bits are different, so I don't think a mapping layer is needed.
#include <sys/stat.h>
static_assert(static_cast<std::underlying_type_t<posix_permissions>>(posix_permissions::user_read)     == S_IRUSR, "Exotic platform, need mapping layer");
static_assert(static_cast<std::underlying_type_t<posix_permissions>>(posix_permissions::user_write)    == S_IWUSR, "Exotic platform, need mapping layer");
static_assert(static_cast<std::underlying_type_t<posix_permissions>>(posix_permissions::user_execute)  == S_IXUSR, "Exotic platform, need mapping layer");
static_assert(static_cast<std::underlying_type_t<posix_permissions>>(posix_permissions::group_read)    == S_IRGRP, "Exotic platform, need mapping layer");
static_assert(static_cast<std::underlying_type_t<posix_permissions>>(posix_permissions::group_write)   == S_IWGRP, "Exotic platform, need mapping layer");
static_assert(static_cast<std::underlying_type_t<posix_permissions>>(posix_permissions::group_execute) == S_IXGRP, "Exotic platform, need mapping layer");
static_assert(static_cast<std::underlying_type_t<posix_permissions>>(posix_permissions::other_read)    == S_IROTH, "Exotic platform, need mapping layer");
static_assert(static_cast<std::underlying_type_t<posix_permissions>>(posix_permissions::other_write)   == S_IWOTH, "Exotic platform, need mapping layer");
static_assert(static_cast<std::underlying_type_t<posix_permissions>>(posix_permissions::other_execute) == S_IXOTH, "Exotic platform, need mapping layer");
static_assert(static_cast<std::underlying_type_t<posix_permissions>>(posix_permissions::setuid)        == S_ISUID, "Exotic platform, need mapping layer");
static_assert(static_cast<std::underlying_type_t<posix_permissions>>(posix_permissions::setgid)        == S_ISGID, "Exotic platform, need mapping layer");
static_assert(static_cast<std::underlying_type_t<posix_permissions>>(posix_permissions::sticky)        == S_ISVTX, "Exotic platform, need mapping layer");
#endif

std::optional<posix_permissions> parse_permissions(std::string_view in)
{
	if (!in.empty() && in[0] >= '0' && in[0] <= '7') {
		while (!in.empty() && in[0] == '0') {
			in.remove_prefix(1);
		}
		if (in.size() > 4) {
			return {};
		}

		uint16_t perms{};
		uint16_t shift{};

		for (auto c = in.rbegin(); c != in.rend(); ++c) {
			if (*c < '0' || *c > '7') {
				return {};
			}
			perms |= (*c - '0') << shift;
			shift += 3;
		}
		return static_cast<posix_permissions>(perms);
	}
	if (in.size() != 10) {
		return {};
	}

	switch (in[0]) {
		case '-':
		case 'd':
		case 'l':
		case 'p':
		case 'b':
		case 'c':
		case 's':
			break;
		default:
			return {};
	}

	posix_permissions ret{};
	if (in[1] == 'r') {
		ret |= posix_permissions::user_read;
	}
	else if (in[1] != '-') {
		return {};
	}

	if (in[2] == 'w') {
		ret |= posix_permissions::user_write;
	}
	else if (in[2] != '-') {
		return {};
	}

	if (in[3] == 'x') {
		ret |= posix_permissions::user_execute;
	}
	else if (in[3] == 's') {
		ret |= posix_permissions::user_execute | posix_permissions::setuid;
	}
	else if (in[3] == 'S') {
		ret |= posix_permissions::setuid;
	}
	else if (in[3] != '-') {
		return {};
	}

	if (in[4] == 'r') {
		ret |= posix_permissions::group_read;
	}
	else if (in[4] != '-') {
		return {};
	}

	if (in[5] == 'w') {
		ret |= posix_permissions::group_write;
	}
	else if (in[5] != '-') {
		return {};
	}

	if (in[6] == 'x') {
		ret |= posix_permissions::group_execute;
	}
	else if (in[6] == 's') {
		ret |= posix_permissions::group_execute | posix_permissions::setgid;
	}
	else if (in[6] == 'S') {
		ret |= posix_permissions::setgid;
	}
	else if (in[6] != '-') {
		return {};
	}

	if (in[7] == 'r') {
		ret |= posix_permissions::other_read;
	}
	else if (in[7] != '-') {
		return {};
	}

	if (in[8] == 'w') {
		ret |= posix_permissions::other_write;
	}
	else if (in[8] != '-') {
		return {};
	}

	if (in[9] == 'x') {
		ret |= posix_permissions::other_execute;
	}
	else if (in[9] == 't') {
		ret |= posix_permissions::other_execute | posix_permissions::sticky;
	}
	else if (in[9] == 'T') {
		ret |= posix_permissions::sticky;
	}
	else if (in[9] != '-') {
		return {};
	}

	return ret;
}

posix_chmod parse_chmod(std::string_view in)
{
	posix_chmod ret;

	if (!in.empty() && in[0] >= '0' && in[0] <= '7') {
		uint16_t perms{};
		uint16_t shift{};

		for (auto c = in.rbegin(); c != in.rend(); ++c) {
			if (*c < '0' || *c > '7') {
				return {};
			}
			if (shift >= 12) {
				if (*c != '0') {
					return {};
				}
			}
			else {
				perms |= (*c - '0') << shift;
			}
			if (shift < 15) {
				shift += 3;
			}
		}
		ret.perms_ = {static_cast<posix_permissions>(perms)};
		if (shift > 12) {
			ret.mask_ = posix_permissions::mask;
		}
		else {
			ret.mask_ = (perms & 07000) ? posix_permissions::mask : posix_permissions::ugo_mask;
		}
	}
	else {
		for (auto const& segment : fz::strtokenizer(in, ',', false)) {
			posix_permissions mask{};

			size_t i{};
			for (i = 0; i < segment.size(); ++i) {
				switch (segment[i]) {
					case 'u':
						mask |= posix_permissions::user_mask | posix_permissions::setuid;
						break;
					case 'g':
						mask |= posix_permissions::group_mask | posix_permissions::setgid;
						break;
					case 'o':
						mask |= posix_permissions::other_mask | posix_permissions::sticky;
						break;
					case 'a':
						mask |= posix_permissions::mask;
						break;
					default:
						if (mask == posix_permissions::none) {
							mask = posix_permissions::mask;
						}
						goto ugoa_done;
						break;
				}
			}

ugoa_done:
			if (i >= segment.size()) {
				return {};
			}

			char mode = segment[i++]; // Validated later

			posix_permissions perms{};
			for (; i < segment.size(); ++i) {
				switch (segment[i]) {
					case 'r':
						perms |= posix_permissions::all_read;
						break;
					case 'w':
						perms |= posix_permissions::all_write;
						break;
					case 'x':
						perms |= posix_permissions::all_execute;
						break;
					case 's':
						perms |= posix_permissions::all_setid;
						break;
					case 't':
						perms |= posix_permissions::sticky;
						break;
					case '=':
					case '-':
					case '+':
						goto perms_done;
					default:
						return {};
				}
			}
perms_done:

			switch (mode) {
				case '+':
					ret.mask_ |= (perms & mask);
					ret.perms_ |= perms & mask;
					break;
				case '-':
					ret.mask_ |= (perms & mask);
					ret.perms_ = (ret.perms_ & ~(perms & mask));
					break;
				case '=':
					ret.mask_ |= mask;
					ret.perms_ = (ret.perms_ & ~mask) | (perms & mask);
					break;
				default:
					return {};
					break;
			}
			if (i < segment.size()) {
				goto ugoa_done;
			}
		}
	}

	return ret;
}

std::string to_string(posix_permissions perms)
{
	std::string ret;

	ret += (perms & posix_permissions::user_read) != posix_permissions::none ? 'r' : '-';
	ret += (perms & posix_permissions::user_write) != posix_permissions::none ? 'w' : '-';
	if ((perms & posix_permissions::setuid) != posix_permissions::none) {
		ret += (perms & posix_permissions::user_execute) != posix_permissions::none ? 's' : 'S';
	}
	else {
		ret += (perms & posix_permissions::user_execute) != posix_permissions::none ? 'x' : '-';
	}

	ret += (perms & posix_permissions::group_read) != posix_permissions::none ? 'r' : '-';
	ret += (perms & posix_permissions::group_write) != posix_permissions::none ? 'w' : '-';
	if ((perms & posix_permissions::setgid) != posix_permissions::none) {
		ret += (perms & posix_permissions::group_execute) != posix_permissions::none ? 's' : 'S';
	}
	else {
		ret += (perms & posix_permissions::group_execute) != posix_permissions::none ? 'x' : '-';
	}

	ret += (perms & posix_permissions::other_read) != posix_permissions::none ? 'r' : '-';
	ret += (perms & posix_permissions::other_write) != posix_permissions::none ? 'w' : '-';
	if ((perms & posix_permissions::sticky) != posix_permissions::none) {
		ret += (perms & posix_permissions::other_execute) != posix_permissions::none ? 't' : 'T';
	}
	else {
		ret += (perms & posix_permissions::other_execute) != posix_permissions::none ? 'x' : '-';
	}

	return ret;
}

std::string to_octal(posix_permissions perms, bool always_with_setid_sticky)
{
	std::string ret;
	ret = "0";
	if (always_with_setid_sticky || (perms & (posix_permissions::all_setid | posix_permissions::sticky)) != posix_permissions::none) {
		ret += '0' + static_cast<char>((static_cast<uint16_t>(perms) >> 9) & 7);
	}
	ret += '0' + static_cast<char>((static_cast<uint16_t>(perms) >> 6) & 7);
	ret += '0' + static_cast<char>((static_cast<uint16_t>(perms) >> 3) & 7);
	ret += '0' + static_cast<char>(static_cast<uint16_t>(perms) & 7);

	return ret;
}

std::string to_string(posix_chmod chmod)
{
	if (chmod.mask_ == posix_permissions::mask) {
		return to_octal(chmod.perms_, true);
	}

	auto component_to_string = [](auto p)
	{
		std::string ret;
		if ((p & posix_permissions::all_read) != posix_permissions::none) {
			ret += 'r';
		}
		if ((p & posix_permissions::all_write) != posix_permissions::none) {
			ret += 'w';
		}
		if ((p & posix_permissions::all_execute) != posix_permissions::none) {
			ret += 'x';
		}
		if ((p & posix_permissions::all_setid) != posix_permissions::none) {
			ret += 's';
		}
		if ((p & posix_permissions::sticky) != posix_permissions::none) {
			ret += 't';
		}

		return ret;
	};

	auto to_string = [&] (auto p, auto m, auto component) {
		std::string ret;

		m &= component;
		p &= m;
		if (m == component) {
			ret += '=';
			if ((p & posix_permissions::all_read) != posix_permissions::none) {
				ret += 'r';
			}
			if ((p & posix_permissions::all_write) != posix_permissions::none) {
				ret += 'w';
			}
			if ((p & posix_permissions::all_execute) != posix_permissions::none) {
				ret += 'x';
			}
			if ((p & posix_permissions::all_setid) != posix_permissions::none) {
				ret += 's';
			}
			if ((p & posix_permissions::sticky) != posix_permissions::none) {
				ret += 't';
			}
		}
		else {
			auto to_add = component_to_string(p & m);
			auto to_remove = component_to_string(~p & m);
			if (!to_add.empty()) {
				ret += '+';
				ret += to_add;
			}
			if (!to_remove.empty()) {
				ret += '-';
				ret += to_remove;
			}
		}

		return ret;
	};

	auto user = to_string(chmod.perms_, chmod.mask_, posix_permissions::user_mask | posix_permissions::setuid);
	auto group = to_string(chmod.perms_, chmod.mask_, posix_permissions::group_mask | posix_permissions::setgid);
	auto other = to_string(chmod.perms_, chmod.mask_, posix_permissions::other_mask | posix_permissions::sticky);

	auto append = [](std::string & ret, std::string const& v, char t) {
		if (v.empty()) {
			return;
		}
		if (!ret.empty()) {
			ret += ',';
		}
		ret += t;
		ret += v;
	};

	std::string ret;
	if (user == group && group == other) {
		ret = user;
	}
	else {
		append(ret, user, 'u');
		append(ret, group, 'g');
		append(ret, other, 'o');
	}

	return ret;
}

posix_permissions apply(posix_permissions orig, posix_chmod change)
{
	return (orig & ~change.mask_) | (change.perms_ & change.mask_);
}
