#include "filezilla.h"

#include <libfilezilla/format.hpp>

#include <algorithm>

void CDirentry::clear()
{
	*this = CDirentry();
}

std::wstring CDirentry::dump() const
{
	std::wstring str = fz::sprintf(L"name=%s\nsize=%d\npermissions=%s\nownerGroup=%s\ndir=%d\nlink=%d\ntarget=%s\nunsure=%d\n",
				name, size, *permissions, *ownerGroup, flags & flag_dir, flags & flag_link,
				target ? *target : std::wstring(), flags & flag_unsure);

	if (has_date()) {
		str += L"date=" + time.format(L"%Y-%m-%d", fz::datetime::local) + L"\n";
	}
	if (has_time()) {
		str += L"time=" + time.format(L"%H-%M-%S", fz::datetime::local) + L"\n";
	}
	return str;
}

bool CDirentry::operator==(const CDirentry &op) const
{
	if (name != op.name) {
		return false;
	}

	if (size != op.size) {
		return false;
	}

	if (permissions != op.permissions) {
		return false;
	}

	if (ownerGroup != op.ownerGroup) {
		return false;
	}

	if (flags != op.flags) {
		return false;
	}

	if (has_date()) {
		if (time != op.time) {
			return false;
		}
	}

	// Intentionally not considering target

	return true;
}

const CDirentry& CDirectoryListing::operator[](size_t index) const
{
	return *(*m_entries)[index];
}

CDirentry& CDirectoryListing::get(size_t index)
{
	// Commented out, too heavy speed penalty
	// assert(index < m_entryCount);
	return m_entries.get()[index].get();
}

void CDirectoryListing::Assign(std::vector<fz::shared_value<CDirentry>> && entries)
{
	std::vector<fz::shared_value<CDirentry>> & own_entries = m_entries.get();
	own_entries = std::move(entries);

	m_flags &= ~(listing_has_dirs | listing_has_perms | listing_has_usergroup);

	for (auto const& entry : own_entries) {
		if (entry->is_dir()) {
			m_flags |= listing_has_dirs;
		}
		if (!entry->permissions->empty()) {
			m_flags |= listing_has_perms;
		}
		if (!entry->ownerGroup->empty()) {
			m_flags |= listing_has_usergroup;
		}
	}

	m_searchmap_case.clear();
	m_searchmap_nocase.clear();
}

bool CDirectoryListing::RemoveEntry(size_t index)
{
	if (index >= size()) {
		return false;
	}

	m_searchmap_case.clear();
	m_searchmap_nocase.clear();

	std::vector<fz::shared_value<CDirentry> >& entries = m_entries.get();
	std::vector<fz::shared_value<CDirentry> >::iterator iter = entries.begin() + index;
	if ((*iter)->is_dir()) {
		m_flags |= CDirectoryListing::unsure_dir_removed;
	}
	else {
		m_flags |= CDirectoryListing::unsure_file_removed;
	}
	entries.erase(iter);

	return true;
}

void CDirectoryListing::GetFilenames(std::vector<std::wstring> &names) const
{
	names.reserve(size());
	for (size_t i = 0; i < size(); ++i) {
		names.push_back((*m_entries)[i]->name);
	}
}

size_t CDirectoryListing::FindFile_CmpCase(std::wstring const& name) const
{
	if (!m_entries || m_entries->empty()) {
		return std::string::npos;
	}

	if (!m_searchmap_case) {
		m_searchmap_case.get();
	}

	// Search map
	auto iter = m_searchmap_case->find(name);
	if (iter != m_searchmap_case->end()) {
		return iter->second;
	}

	size_t i = m_searchmap_case->size();
	if (i == m_entries->size()) {
		return std::string::npos;
	}

	auto & searchmap_case = m_searchmap_case.get();

	// Build map if not yet complete
	std::vector<fz::shared_value<CDirentry> >::const_iterator entry_iter = m_entries->begin() + i;
	for (; entry_iter != m_entries->end(); ++entry_iter, ++i) {
		std::wstring const& entry_name = (*entry_iter)->name;
		searchmap_case.emplace(entry_name, i);

		if (entry_name == name) {
			return i;
		}
	}

	// Map is complete, item not in it
	return std::string::npos;
}

size_t CDirectoryListing::FindFile_CmpNoCase(std::wstring const& name) const
{
	if (!m_entries || m_entries->empty()) {
		return std::string::npos;
	}

	if (!m_searchmap_nocase) {
		m_searchmap_nocase.get();
	}

	std::wstring lwr = fz::str_tolower(name);

	// Search map
	auto iter = m_searchmap_nocase->find(lwr);
	if (iter != m_searchmap_nocase->end()) {
		return iter->second;
	}

	size_t i = m_searchmap_nocase->size();
	if (i == m_entries->size()) {
		return std::string::npos;
	}

	auto& searchmap_nocase = m_searchmap_nocase.get();

	// Build map if not yet complete
	std::vector<fz::shared_value<CDirentry> >::const_iterator entry_iter = m_entries->begin() + i;
	for (; entry_iter != m_entries->end(); ++entry_iter, ++i) {
		std::wstring entry_lrw = fz::str_tolower((*entry_iter)->name);
		searchmap_nocase.emplace(entry_lrw, i);

		if (entry_lrw == lwr) {
			return i;
		}
	}

	// Map is complete, item not in it
	return std::string::npos;
}

void CDirectoryListing::ClearFindMap()
{
	if (!m_searchmap_case) {
		return;
	}

	m_searchmap_case.clear();
	m_searchmap_nocase.clear();
}

void CDirectoryListing::Append(CDirentry&& entry)
{
	m_entries.get().emplace_back(entry);
}

bool CheckInclusion(const CDirectoryListing& listing1, const CDirectoryListing& listing2)
{
	// Check if listing2 is contained within listing1

	if (listing2.size() > listing1.size()) {
		return false;
	}

	std::vector<std::wstring> names1, names2;
	listing1.GetFilenames(names1);
	listing2.GetFilenames(names2);
	std::sort(names1.begin(), names1.end());
	std::sort(names2.begin(), names2.end());

	std::vector<std::wstring>::const_iterator iter1, iter2;
	iter1 = names1.cbegin();
	iter2 = names2.cbegin();
	while (iter2 != names2.cbegin()) {
		if (iter1 == names1.cend()) {
			return false;
		}

		if (*iter1 != *iter2) {
			++iter1;
			continue;
		}

		++iter1;
		++iter2;
	}

	return true;
}
