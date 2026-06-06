#ifndef FILEZILLA_COMMONUI_FILTER_HEADER
#define FILEZILLA_COMMONUI_FILTER_HEADER

#include "visibility.h"
#include <libfilezilla/time.hpp>

#include <memory>

#include <vector>

enum t_filterType
{
	filter_name = 0x01,
	filter_size = 0x02,
	filter_attributes = 0x04,
	filter_permissions = 0x08,
	filter_path = 0x10,
	filter_date = 0x20,
#ifdef FZ_WINDOWS
	filter_meta = filter_attributes,
	filter_foreign = filter_permissions,
#else
	filter_meta = filter_permissions,
	filter_foreign = filter_attributes
#endif
};

class FZCUI_PUBLIC_SYMBOL CFilterCondition final
{
public:
	bool set(t_filterType t, std::wstring const& v, int c, bool matchCase);

	std::wstring strValue;
	std::wstring lowerValue; // Name and path matches
	fz::datetime date; // If type is date
	int64_t value{}; // If type is size
	std::shared_ptr<void> pRegEx;

	t_filterType type{filter_name};
	int condition{};
};

class FZCUI_PUBLIC_SYMBOL CFilter final
{
public:
	enum t_matchType
	{
		all,
		any,
		none,
		not_all
	};

	bool empty() const { return filters.empty(); }

	explicit operator bool() const { return !filters.empty(); }

	std::vector<CFilterCondition> filters;

	std::wstring name;

	t_matchType matchType{ all };

	bool filterFiles{ true };
	bool filterDirs{ true };

	// Filenames on Windows ignore case
#ifdef FZ_WINDOWS
	bool matchCase{};
#else
	bool matchCase{ true };
#endif

	bool HasConditionOfType(t_filterType type) const;
	bool IsLocalFilter() const;
};

class FZCUI_PUBLIC_SYMBOL CFilterSet final
{
public:
	std::wstring name;
	std::vector<unsigned char> local;
	std::vector<unsigned char> remote;
};

class FZCUI_PUBLIC_SYMBOL filter_manager {
public:
	virtual ~filter_manager() = default;

	// Note: Under non-windows, attributes are permissions
	virtual bool FilenameFiltered(std::wstring const& name, std::wstring const& path, bool dir, int64_t size, bool local, int attributes, fz::datetime const& date) const = 0;

	static bool FilenameFiltered(std::vector<CFilter> const& filters, std::wstring const& name, std::wstring const& path, bool dir, int64_t size, int attributes, fz::datetime const& date);
	static bool FilenameFilteredByFilter(CFilter const& filter, std::wstring const& name, std::wstring const& path, bool dir, int64_t size, int attributes, fz::datetime const& date);
};

typedef std::pair<std::vector<CFilter>, std::vector<CFilter>> ActiveFilters;

struct FZCUI_PUBLIC_SYMBOL filter_data final {
	std::vector<CFilter> filters;
 	std::vector<CFilterSet> filter_sets;
	unsigned int current_filter_set{};
};

namespace pugi { class xml_node; }

bool FZCUI_PUBLIC_SYMBOL load_filter(pugi::xml_node& element, CFilter& filter);
void FZCUI_PUBLIC_SYMBOL load_filters(pugi::xml_node& element, filter_data& data);

void FZCUI_PUBLIC_SYMBOL save_filter(pugi::xml_node& element, CFilter const& filter);
void FZCUI_PUBLIC_SYMBOL save_filters(pugi::xml_node& element, filter_data const& data);

bool FZCUI_PUBLIC_SYMBOL valid_regex(std::wstring const& r);
std::shared_ptr<void> compile_regex(std::wstring const& r, bool matchCase);

#endif
