#include "filter.h"
#include "../include/misc.h"
#include "../include/xmlutils.h"

#include <libfilezilla/string.hpp>

#ifndef FZ_WINDOWS
#include <sys/stat.h>
#endif

#include <array>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if USE_BOOST_REGEX
#define BOOST_REGEX_STANDALONE
#include <boost/regex.hpp>
namespace regex_ns = boost;
#else
#include <regex>
namespace regex_ns = std;
#endif

std::shared_ptr<void> compile_regex(std::wstring const& r, bool matchCase)
{
	if (r.size() > 2000) {
		return {};
	}

	try {
		regex_ns::regex_constants::syntax_option_type flags = regex_ns::regex_constants::ECMAScript;
		if (!matchCase) {
			flags |= regex_ns::regex_constants::icase;
		}
		return std::make_shared<regex_ns::wregex>(r, flags);
	}
	catch (regex_ns::regex_error const&) {
		return {};
	}
}

bool valid_regex(std::wstring const& r)
{
	return compile_regex(r, true) != nullptr;
}


std::array<std::wstring, 4> const matchTypeXmlNames =
	{ L"All", L"Any", L"None", L"Not all" };

bool CFilterCondition::set(t_filterType t, std::wstring const& v, int c, bool matchCase)
{
	if (v.empty()) {
		return false;
	}

	type = t;
	condition = c;
	strValue = v;

	pRegEx.reset();

	switch (t) {
	case filter_name:
	case filter_path:
		if (condition == 4) {
			pRegEx = compile_regex(strValue, matchCase);
			if (!pRegEx) {
				return false;
			}
		}
		else {
			if (!matchCase) {
				lowerValue = fz::str_tolower(v);
			}
		}
		break;
	case filter_size:
	case filter_attributes:
	case filter_permissions:
		value = fz::to_integral<int64_t>(v);
		break;
	case filter_date:
		date = fz::datetime(v, fz::datetime::local);
		if (date.empty()) {
			return false;
		}
		break;
	}

	return true;
}

bool CFilter::HasConditionOfType(t_filterType type) const
{
	for (std::vector<CFilterCondition>::const_iterator iter = filters.begin(); iter != filters.end(); ++iter) {
		if (iter->type == type) {
			return true;
		}
	}

	return false;
}

bool CFilter::IsLocalFilter() const
{
	 return HasConditionOfType(filter_attributes) || HasConditionOfType(filter_permissions);
}

bool filter_manager::FilenameFiltered(std::vector<CFilter> const& filters, std::wstring const& name, std::wstring const& path, bool dir, int64_t size, int attributes, fz::datetime const& date)
{
	for (auto const& filter : filters) {
		if (FilenameFilteredByFilter(filter, name, path, dir, size, attributes, date)) {
			return true;
		}
	}

	return false;
}

static bool StringMatch(std::wstring const& subject, CFilterCondition const& condition, bool matchCase)
{
	bool match = false;

	switch (condition.condition)
	{
	case 0:
		if (matchCase) {
			if (subject.find(condition.strValue) != std::wstring::npos) {
				match = true;
			}
		}
		else {
			if (fz::str_tolower(subject).find(condition.lowerValue) != std::wstring::npos) {
				match = true;
			}
		}
		break;
	case 1:
		if (matchCase) {
			if (subject == condition.strValue) {
				match = true;
			}
		}
		else {
			if (fz::str_tolower(subject) == condition.lowerValue) {
				match = true;
			}
		}
		break;
	case 2:
		{
			if (matchCase) {
				match = fz::starts_with(subject, condition.strValue);
			}
			else {
				match = fz::starts_with(fz::str_tolower(subject), condition.lowerValue);
			}
		}
		break;
	case 3:
		{
			if (matchCase) {
				match = fz::ends_with(subject, condition.strValue);
			}
			else {
				match = fz::ends_with(fz::str_tolower(subject), condition.lowerValue);
			}
		}
		break;
	case 4:
		if (condition.pRegEx && regex_ns::regex_search(subject, *std::static_pointer_cast<regex_ns::wregex>(condition.pRegEx))) {
			match = true;
		}
		break;
	case 5:
		if (matchCase) {
			if (subject.find(condition.strValue) == std::wstring::npos) {
				match = true;
			}
		}
		else {
			if (fz::str_tolower(subject).find(condition.lowerValue) == std::wstring::npos) {
				match = true;
			}
		}
		break;
	}

	return match;
}

bool filter_manager::FilenameFilteredByFilter(CFilter const& filter, std::wstring const& name, std::wstring const& path, bool dir, int64_t size, int attributes, fz::datetime const& date)
{
	if (dir && !filter.filterDirs) {
		return false;
	}
	else if (!dir && !filter.filterFiles) {
		return false;
	}

	for (auto const& condition : filter.filters) {
		bool match = false;

		switch (condition.type)
		{
		case filter_name:
			match = StringMatch(name, condition, filter.matchCase);
			break;
		case filter_path:
			match = StringMatch(path, condition, filter.matchCase);
			break;
		case filter_size:
			if (size == -1) {
				continue;
			}
			switch (condition.condition)
			{
			case 0:
				if (size > condition.value) {
					match = true;
				}
				break;
			case 1:
				if (size == condition.value) {
					match = true;
				}
				break;
			case 2:
				if (size != condition.value) {
					match = true;
				}
				break;
			case 3:
				if (size < condition.value) {
					match = true;
				}
				break;
			}
			break;
		case filter_attributes:
#ifndef FZ_WINDOWS
			continue;
#else
			if (!attributes) {
				continue;
			}

			{
				int flag = 0;
				switch (condition.condition)
				{
				case 0:
					flag = FILE_ATTRIBUTE_ARCHIVE;
					break;
				case 1:
					flag = FILE_ATTRIBUTE_COMPRESSED;
					break;
				case 2:
					flag = FILE_ATTRIBUTE_ENCRYPTED;
					break;
				case 3:
					flag = FILE_ATTRIBUTE_HIDDEN;
					break;
				case 4:
					flag = FILE_ATTRIBUTE_READONLY;
					break;
				case 5:
					flag = FILE_ATTRIBUTE_SYSTEM;
					break;
				}

				int set = (flag & attributes) ? 1 : 0;
				if (set == condition.value) {
					match = true;
				}
			}
#endif //FZ_WINDOWS
			break;
		case filter_permissions:
#ifdef FZ_WINDOWS
			continue;
#else
			if (attributes == -1) {
				continue;
			}

			{
				int flag = 0;
				switch (condition.condition)
				{
				case 0:
					flag = S_IRUSR;
					break;
				case 1:
					flag = S_IWUSR;
					break;
				case 2:
					flag = S_IXUSR;
					break;
				case 3:
					flag = S_IRGRP;
					break;
				case 4:
					flag = S_IWGRP;
					break;
				case 5:
					flag = S_IXGRP;
					break;
				case 6:
					flag = S_IROTH;
					break;
				case 7:
					flag = S_IWOTH;
					break;
				case 8:
					flag = S_IXOTH;
					break;
				}

				int set = (flag & attributes) ? 1 : 0;
				if (set == condition.value) {
					match = true;
				}
			}
#endif //FZ_WINDOWS
			break;
		case filter_date:
			if (!date.empty()) {
				int cmp = date.compare(condition.date);
				switch (condition.condition)
				{
				case 0: // Before
					match = cmp < 0;
					break;
				case 1: // Equals
					match = cmp == 0;
					break;
				case 2: // Not equals
					match = cmp != 0;
					break;
				case 3: // After
					match = cmp > 0;
					break;
				}
			}
			break;
		default:
			break;
		}
		if (match) {
			if (filter.matchType == CFilter::any) {
				return true;
			}
			else if (filter.matchType == CFilter::none) {
				return false;
			}
		}
		else {
			if (filter.matchType == CFilter::all) {
				return false;
			}
			else if (filter.matchType == CFilter::not_all) {
				return true;
			}
		}
	}

	if (filter.matchType == CFilter::not_all) {
		return false;
	}

	if (filter.matchType != CFilter::any || filter.filters.empty()) {
		return true;
	}

	return false;
}

bool load_filter(pugi::xml_node& element, CFilter& filter)
{
	filter.name = GetTextElement(element, "Name").substr(0, 255);
	filter.filterFiles = GetTextElement(element, "ApplyToFiles") == L"1";
	filter.filterDirs = GetTextElement(element, "ApplyToDirs") == L"1";

	std::wstring const matchType = GetTextElement(element, "MatchType");
	filter.matchType = CFilter::all;
	for (size_t i = 0; i < matchTypeXmlNames.size(); ++i) {
		if (matchType == matchTypeXmlNames[i]) {
			filter.matchType = static_cast<CFilter::t_matchType>(i);
		}
	}
	filter.matchCase = GetTextElement(element, "MatchCase") == L"1";

	auto xConditions = element.child("Conditions");
	if (!xConditions) {
		return false;
	}

	for (auto xCondition = xConditions.child("Condition"); xCondition; xCondition = xCondition.next_sibling("Condition")) {
		t_filterType type;
		switch (GetTextElementInt(xCondition, "Type", -1)) {
		case 0:
			type = filter_name;
			break;
		case 1:
			type = filter_size;
			break;
		case 2:
			type = filter_attributes;
			break;
		case 3:
			type = filter_permissions;
			break;
		case 4:
			type = filter_path;
			break;
		case 5:
			type = filter_date;
			break;
		default:
			continue;
		}

		std::wstring value = GetTextElement(xCondition, "Value");
		int cond = GetTextElementInt(xCondition, "Condition", 0);

		CFilterCondition condition;
		if (!condition.set(type, value, cond, filter.matchCase)) {
			continue;
		}

		if (filter.filters.size() < 1000) {
			filter.filters.push_back(condition);
		}
	}

	if (filter.filters.empty()) {
		return false;
	}
	return true;
}

void load_filters(pugi::xml_node& element, filter_data& data)
{
	auto xFilters = element.child("Filters");
	if (xFilters) {

		auto xFilter = xFilters.child("Filter");
		while (xFilter) {
			CFilter filter;

			bool loaded = load_filter(xFilter, filter);

			if (loaded && !filter.name.empty() && !filter.filters.empty()) {
				data.filters.push_back(filter);
			}

			xFilter = xFilter.next_sibling("Filter");
		}

		auto xSets = element.child("Sets");
		if (xSets) {
			for (auto xSet = xSets.child("Set"); xSet; xSet = xSet.next_sibling("Set")) {
				CFilterSet set;
				auto xItem = xSet.child("Item");
				while (xItem) {
					std::wstring local = GetTextElement(xItem, "Local");
					std::wstring remote = GetTextElement(xItem, "Remote");
					set.local.push_back(local == L"1" ? true : false);
					set.remote.push_back(remote == L"1" ? true : false);

					xItem = xItem.next_sibling("Item");
				}

				if (!data.filter_sets.empty()) {
					set.name = GetTextElement(xSet, "Name").substr(0, 255);
					if (set.name.empty()) {
						continue;
					}
				}

				if (set.local.size() == data.filters.size()) {
					data.filter_sets.push_back(set);
				}
			}

			int value = GetAttributeInt(xSets, "Current");
			if (value >= 0 && static_cast<size_t>(value) < data.filter_sets.size()) {
				data.current_filter_set = value;
			}
		}
	}

	if (data.filter_sets.empty()) {
		CFilterSet set;
		set.local.resize(data.filters.size(), false);
		set.remote.resize(data.filters.size(), false);

		data.filter_sets.push_back(set);
	}
}

void save_filters(pugi::xml_node& element, filter_data const& data)
{
	auto xFilters = element.child("Filters");
	while (xFilters) {
		element.remove_child(xFilters);
		xFilters = element.child("Filters");
	}

	xFilters = element.append_child("Filters");

	for (auto const& filter : data.filters) {
		auto xFilter = xFilters.append_child("Filter");
		save_filter(xFilter, filter);
	}

	auto xSets = element.child("Sets");
	while (xSets) {
		element.remove_child(xSets);
		xSets = element.child("Sets");
	}

	xSets = element.append_child("Sets");
	SetAttributeInt(xSets, "Current", data.current_filter_set);

	for (auto const& set : data.filter_sets) {
		auto xSet = xSets.append_child("Set");

		if (!set.name.empty()) {
			AddTextElement(xSet, "Name", set.name);
		}

		for (unsigned int i = 0; i < set.local.size(); ++i) {
			auto xItem = xSet.append_child("Item");
			AddTextElement(xItem, "Local", set.local[i] ? "1" : "0");
			AddTextElement(xItem, "Remote", set.remote[i] ? "1" : "0");
		}
	}
}

void save_filter(pugi::xml_node& element, const CFilter& filter)
{
	AddTextElement(element, "Name", filter.name);
	AddTextElement(element, "ApplyToFiles", filter.filterFiles ? "1" : "0");
	AddTextElement(element, "ApplyToDirs", filter.filterDirs ? "1" : "0");
	AddTextElement(element, "MatchType", matchTypeXmlNames[filter.matchType]);
	AddTextElement(element, "MatchCase", filter.matchCase ? "1" : "0");

	auto xConditions = element.append_child("Conditions");
	for (std::vector<CFilterCondition>::const_iterator conditionIter = filter.filters.begin(); conditionIter != filter.filters.end(); ++conditionIter) {
		const CFilterCondition& condition = *conditionIter;

		int type;
		switch (condition.type)
		{
		case filter_name:
			type = 0;
			break;
		case filter_size:
			type = 1;
			break;
		case filter_attributes:
			type = 2;
			break;
		case filter_permissions:
			type = 3;
			break;
		case filter_path:
			type = 4;
			break;
		case filter_date:
			type = 5;
			break;
		default:
			continue;
		}

		auto xCondition = xConditions.append_child("Condition");
		AddTextElement(xCondition, "Type", type);
		AddTextElement(xCondition, "Condition", condition.condition);
		AddTextElement(xCondition, "Value", condition.strValue);
	}
}
