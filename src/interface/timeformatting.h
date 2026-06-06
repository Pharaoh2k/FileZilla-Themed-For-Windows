#ifndef FILEZILLA_INTERFACE_TIMEFORMATTING_HEADER
#define FILEZILLA_INTERFACE_TIMEFORMATTING_HEADER

#include <memory>
#include <string>

namespace fz {
class datetime;
}

class COptionsBase;
class TimeFormatter final
{
public:
	explicit TimeFormatter(COptionsBase & options);
	~TimeFormatter();

	std::wstring Format(fz::datetime const& time);
	std::wstring FormatDateTime(fz::datetime const& time);
	std::wstring FormatDate(fz::datetime const& time);

private:
	class Impl;
	std::unique_ptr<Impl> impl_;
};

#endif
