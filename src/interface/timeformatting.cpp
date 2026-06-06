#include "filezilla.h"
#include "timeformatting.h"
#include "Options.h"

#include "option_change_event_handler.h"

using namespace std::literals;

class TimeFormatter::Impl final : public wxEvtHandler, public COptionChangeEventHandler
{
public:
	Impl(COptionsBase & options)
		: COptionChangeEventHandler(this)
		, options_(options)
	{
		InitFormat();

		options_.watch(OPTION_DATE_FORMAT, this);
		options_.watch(OPTION_TIME_FORMAT, this);
	}

	~Impl()
	{
		options_.unwatch_all(this);
	}

	void InitFormat()
	{
		std::wstring dateFormat = options_.get_string(OPTION_DATE_FORMAT);
		std::wstring timeFormat = options_.get_string(OPTION_TIME_FORMAT);

		if (dateFormat == L"1"sv) {
			m_dateFormat = L"%Y-%m-%d"sv;
		}
		else if (!dateFormat.empty() && dateFormat[0] == '2') {
			dateFormat = dateFormat.substr(1);
			if (fz::datetime::verify_format(dateFormat)) {
				m_dateFormat = dateFormat;
			}
			else {
				m_dateFormat = L"%x"sv;
			}
		}
		else {
			m_dateFormat = L"%x"sv;
		}

		m_dateTimeFormat = m_dateFormat;
		m_dateTimeFormat += ' ';

		if (timeFormat == L"1"sv) {
			m_dateTimeFormat += L"%H:%M"sv;
		}
		else if (!timeFormat.empty() && timeFormat[0] == '2') {
			timeFormat = timeFormat.substr(1);
			if (fz::datetime::verify_format(timeFormat)) {
				m_dateTimeFormat += timeFormat;
			}
			else {
				m_dateTimeFormat += L"%X"sv;
			}
		}
		else {
			m_dateTimeFormat += L"%X"sv;
		}
	}

	virtual void OnOptionsChanged(watched_options const&)
	{
		InitFormat();
	}

	COptionsBase & options_;

	std::wstring m_dateFormat;
	std::wstring m_dateTimeFormat;
};

TimeFormatter::TimeFormatter(COptionsBase & options)
	: impl_(std::make_unique<Impl>(options))
{
}

TimeFormatter::~TimeFormatter()
{}

std::wstring TimeFormatter::Format(fz::datetime const& time)
{
	std::wstring ret;
	if (!time.empty()) {
		if (time.get_accuracy() > fz::datetime::days) {
			ret = FormatDateTime(time);
		}
		else {
			ret = FormatDate(time);
		}
	}
	return ret;
}

std::wstring TimeFormatter::FormatDateTime(fz::datetime const& time)
{
	return time.format(impl_->m_dateTimeFormat, fz::datetime::local);
}

std::wstring TimeFormatter::FormatDate(fz::datetime const& time)
{
	return time.format(impl_->m_dateFormat, fz::datetime::local);
}
