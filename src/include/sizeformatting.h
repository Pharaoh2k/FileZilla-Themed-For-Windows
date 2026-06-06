#ifndef FILEZILLA_ENGINE_SIZEFORMATTING_HEADER
#define FILEZILLA_ENGINE_SIZEFORMATTING_HEADER

#include "visibility.h"

#include <optional>
#include <string>

enum class SizeFormatPurpose {
	normal, // Bytes suffix may be missing based on _format
	in_line, // If used e.g. in a sentence, "bytes" suffix always mandatory
	shortened,
};

enum class SizeFormat {
	bytes,
	iec,
	si1024,
	si1000,
};

// We stop at Exa. If someone has files bigger than that, he can afford to
// make a donation to have this changed ;)
enum class UnitPrefix : unsigned {
	byte,
	kilo,
	mega,
	giga,
	tera,
	peta,
	exa,

	count,
	max = count - 1
};

class COptionsBase;
class FZC_PUBLIC_SYMBOL SizeFormatter final
{
public:
	explicit SizeFormatter(COptionsBase & options);

	static std::wstring Format(int64_t size, bool add_bytes_suffix, SizeFormat format, bool thousands_separator, int num_decimal_places, std::optional<UnitPrefix> forced_prefix = std::nullopt);

	std::wstring Format(int64_t size, SizeFormatPurpose p);
	std::wstring Format(int64_t size, UnitPrefix forced_prefix, int base = 1024);

	std::wstring GetUnitSymbol(UnitPrefix prefix, int base);

private:
	COptionsBase & options_;
};

#endif
