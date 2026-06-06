#include <limits>
#include "filezilla.h"

#include "../include/engine_options.h"
#include "../include/sizeformatting.h"

#include <libfilezilla/format.hpp>

#ifndef FZ_WINDOWS
#include <langinfo.h>
#endif

#include <math.h>
#include <string.h>

using namespace std::literals;

namespace {
wchar_t const prefixes[] = { ' ', 'K', 'M', 'G', 'T', 'P', 'E' };
size_t constexpr prefix_count = sizeof(prefixes) / sizeof(*prefixes);

static_assert(prefix_count == static_cast<unsigned>(UnitPrefix::count));

std::wstring_view GetThousandsSeparator()
{
	static std::wstring const sep = [](){
		std::wstring sep;
#ifdef FZ_WINDOWS
		wchar_t tmp[5];
		int count = ::GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, tmp, 5);
		if (count) {
			sep = tmp;
		}
#else
		char* chr = nl_langinfo(THOUSEP);
		if (chr && *chr) {
			sep = fz::to_wstring(chr);
		}
#endif
		if (sep.size() > 5) {
			sep = sep.substr(0, 5);
		}
		return sep;
	}();
	return sep;
}

std::wstring ToString(int64_t n, bool thousands_separator)
{
	std::wstring_view const sep = thousands_separator ? GetThousandsSeparator() : std::wstring_view{};

	std::wstring ret;
	if (!n) {
		ret = L"0"sv;
	}
	else {
		bool neg = false;
		if (n < 0) {
			n *= -1;
			neg = true;
		}

		wchar_t buf[60];
		wchar_t * const end = &buf[sizeof(buf) / sizeof(wchar_t) - 1];
		wchar_t * p = end;

		unsigned int d{};
		while (n != 0) {
			*--p = '0' + n % 10;
			n /= 10;

			if (sep.size() && n && ++d == 3) {
				d = 0;
				memcpy(p - sep.size(), sep.data(), sep.size() * sizeof(wchar_t));
				p -= sep.size();
			}
		}

		if (neg) {
			*--p = '-';
		}

		ret.assign(p, end - p);
	}
	return ret;
}


std::wstring const& GetRadixSeparator()
{
	static std::wstring const sep = []() {
		std::wstring sep;
#ifdef FZ_WINDOWS
		wchar_t tmp[5];
		int count = ::GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, tmp, 5);
		if (!count) {
			sep = L".";
		}
		else {
			tmp[4] = 0;
			sep = tmp;
		}
#else
		char* chr = nl_langinfo(RADIXCHAR);
		if (!chr || !*chr) {
			sep = L".";
		}
		else {
			sep = fz::to_wstring(chr);
		}
#endif

		return sep;
	}();

	return sep;
}
}

SizeFormatter::SizeFormatter(COptionsBase & options)
    : options_(options)
{
}

namespace {
void AppendUnitSymbol(std::wstring & out, SizeFormat format, size_t p)
{
	if (p) {
		out += prefixes[p];
		if (format == SizeFormat::iec) {
			out += 'i';
		}
	}

	static wchar_t const byte_unit = [](){
		std::wstring t = _("B <Unit symbol for bytes. Only translate first letter>"); // @translator: Only translate first letter.
		return t[0];
	}();

	out += byte_unit;
}
}

std::wstring SizeFormatter::Format(int64_t size, bool add_bytes_suffix, SizeFormat format, bool thousands_separator, int num_decimal_places, std::optional<UnitPrefix> forced_prefix)
{
	if (size < 0) {
		return _("Unknown");
	}

	if (format == SizeFormat::bytes) {
		std::wstring result = ToString(size, thousands_separator);

		if (!add_bytes_suffix) {
			return result;
		}
		else {
			return fz::sprintf(fztranslate("%s byte", "%s bytes", size), result);
		}
	}

	std::wstring places;

	int divider;
	if (format == SizeFormat::si1000) {
		divider = 1000;
	}
	else {
		divider = 1024;
	}

	// Exponent (2^(10p) or 10^(3p) depending on option
	size_t p = 0;

	int64_t r = size;
	int remainder = 0;
	bool clipped = false;

	while (p < prefix_count && forced_prefix ? (p != size_t(*forced_prefix)) : (r > divider)) {
		int64_t const rr = r / divider;
		if (remainder != 0) {
			clipped = true;
		}
		remainder = static_cast<int>(r - rr * divider);
		r = rr;
		++p;
	}
	if (!num_decimal_places) {
		if (remainder != 0 || clipped) {
			++r;
		}
	}
	else if (p) { // Don't add decimal places on exact bytes
		if (format != SizeFormat::si1000) {
			// Binary, need to convert 1024 into range from 1-1000
			if (clipped) {
				++remainder;
				clipped = false;
			}
			remainder = (int)ceil((double)remainder * 1000 / 1024);
		}

		int max;
		switch (num_decimal_places)
		{
		default:
			num_decimal_places = 1;
			// Fall-through
		case 1:
			max = 9;
			divider = 100;
			break;
		case 2:
			max = 99;
			divider = 10;
			break;
		case 3:
			max = 999;
			break;
		}

		if (num_decimal_places != 3) {
			if (remainder % divider) {
				clipped = true;
			}
			remainder /= divider;
		}

		if (clipped) {
			remainder++;
		}
		if (remainder > max) {
			r++;
			remainder = 0;
		}

		wchar_t fmt[] = L"%00d";
		fmt[2] = '0' + num_decimal_places;
		places = fz::sprintf(fmt, remainder);
	}

	std::wstring result = ToString(r, thousands_separator);
	if (!places.empty()) {
		std::wstring const& sep = GetRadixSeparator();

		result += sep;
		result += places;
	}
	result += ' ';

	AppendUnitSymbol(result, format, p);

	return result;
}

std::wstring SizeFormatter::Format(int64_t size, SizeFormatPurpose p)
{
	auto f = SizeFormat(options_.get_int(OPTION_SIZE_FORMAT));
	if (p == SizeFormatPurpose::shortened && f == SizeFormat::bytes) {
		f = SizeFormat::iec;
	}
	bool const thousands_separator = options_.get_bool(OPTION_SIZE_USETHOUSANDSEP);
	int const num_decimal_places = options_.get_int(OPTION_SIZE_DECIMALPLACES);

	return Format(size, p == SizeFormatPurpose::in_line, f, thousands_separator, num_decimal_places);
}

std::wstring SizeFormatter::Format(int64_t size, UnitPrefix forced_prefix, int base)
{
	SizeFormat format;
	if (base == 1000) {
		format = SizeFormat::si1000;
	}
	else {
		format = SizeFormat(options_.get_int(OPTION_SIZE_FORMAT));
		if (format != SizeFormat::si1024) {
			format = SizeFormat::iec;
		}
	}
	bool const thousands_separator = options_.get_bool(OPTION_SIZE_USETHOUSANDSEP);
	int const num_decimal_places = options_.get_int(OPTION_SIZE_DECIMALPLACES);

	return Format(size, true, format, thousands_separator, num_decimal_places, forced_prefix);
}

std::wstring SizeFormatter::GetUnitSymbol(UnitPrefix prefix, int base)
{
	auto format = SizeFormat(options_.get_int(OPTION_SIZE_FORMAT));
	if (base == 1000) {
		format = SizeFormat::si1000;
	}
	else if (format != SizeFormat::si1024) {
		format = SizeFormat::iec;
	}

	if (prefix > UnitPrefix::max) {
		prefix = UnitPrefix::max;
	}

	std::wstring ret;
	AppendUnitSymbol(ret, format, size_t(prefix));
	return ret;
}
