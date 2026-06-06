#include "site_manager.h"
#include "fz_paths.h"
#include "ipcmutex.h"

#include "xmlfunctions.h"

#include "../include/version.h"

#include <libfilezilla/translate.hpp>

#include <cstring>

bool site_manager::Save(std::wstring const& settings_file, CSiteManagerSaveXmlHandler& pHandler, std::wstring& error)
{
	CXmlFile file(settings_file);
	auto document = file.Load();
	if (!document) {
		error = file.GetError();
		return false;
	}

	auto servers = document.child("Servers");
	while (servers) {
		document.remove_child(servers);
		servers = document.child("Servers");
	}
	auto element = document.append_child("Servers");

	if (!element) {
		return true;
	}

	bool res = pHandler.SaveTo(element);

	if (!file.Save()) {
		error = fz::sprintf(L"Could not write \"%s\", any changes to the Site Manager could not be saved: %s", file.GetFileName(), file.GetError());
		return false;
	}

	return res;
}

void site_manager::Save(pugi::xml_node element, Site const& site, login_manager& lim, COptionsBase& options)
{
	SetServer(element, site, lim, options);

	// Save comments
	if (!site.comments_.empty()) {
		AddTextElement(element, "Comments", site.comments_);
	}

	// Save colour
	if (site.m_colour != site_colour::none) {
		AddTextElement(element, "Colour", static_cast<int64_t>(site.m_colour));
	}

	// Save local dir
	if (!site.m_default_bookmark.m_localDir.empty()) {
		AddTextElement(element, "LocalDir", site.m_default_bookmark.m_localDir);
	}

	// Save remote dir
	auto const sp = site.m_default_bookmark.m_remoteDir.GetSafePath();
	if (!sp.empty()) {
		AddTextElement(element, "RemoteDir", sp);
	}

	AddTextElementUtf8(element, "SyncBrowsing", site.m_default_bookmark.m_sync ? "1" : "0");
	AddTextElementUtf8(element, "DirectoryComparison", site.m_default_bookmark.m_comparison ? "1" : "0");

	for (auto const& bookmark : site.m_bookmarks) {
		auto node = element.append_child("Bookmark");

		AddTextElement(node, "Name", bookmark.m_name);

		// Save local dir
		if (!bookmark.m_localDir.empty()) {
			AddTextElement(node, "LocalDir", bookmark.m_localDir);
		}

		// Save remote dir
		auto const sp = bookmark.m_remoteDir.GetSafePath();
		if (!sp.empty()) {
			AddTextElement(node, "RemoteDir", sp);
		}

		AddTextElementUtf8(node, "SyncBrowsing", bookmark.m_sync ? "1" : "0");
		AddTextElementUtf8(node, "DirectoryComparison", bookmark.m_comparison ? "1" : "0");
	}
}

bool site_manager::Load(std::wstring const& settings_file, CSiteManagerXmlHandler& handler, std::wstring& error)
{
	CXmlFile file(settings_file);
	auto document = file.Load();
	if (!document) {
		error = file.GetError();;
		return false;
	}

	auto element = document.child("Servers");
	if (!element) {
		return true;
	}

	return Load(element, handler, file.GetVersion());
}

bool site_manager::Load(pugi::xml_node element, CSiteManagerXmlHandler& handler, int64_t version)
{
	if (!element) {
		return false;
	}

	for (auto child = element.first_child(); child; child = child.next_sibling()) {
		if (!strcmp(child.name(), "Folder")) {
			std::wstring name = GetTextElement_Trimmed(child);
			if (name.empty()) {
				continue;
			}

			const bool expand = GetTextAttribute(child, "expanded") != L"0";
			if (!handler.AddFolder(name.substr(0, 255), expand)) {
				return false;
			}
			Load(child, handler, version);
			if (!handler.LevelUp()) {
				return false;
			}
		}
		else if (!strcmp(child.name(), "Server")) {
			std::unique_ptr<Site> data = ReadServerElement(child, version);

			if (data) {
				handler.AddSite(std::move(data));
			}
		}
	}

	return true;
}

void site_manager::UpdateOneDrivePath(CServerPath& path)
{
	if (!path.empty()) {
		auto const & remoteDir = path.GetPath();
		if (!(fz::starts_with(remoteDir, fztranslate("/SharePoint")) ||
		      fz::starts_with(remoteDir, fztranslate("/Groups")) ||
		      fz::starts_with(remoteDir, fztranslate("/Sites")) ||
		      fz::starts_with(remoteDir, fztranslate("/My Drives")) ||
		      fz::starts_with(remoteDir, fztranslate("/Shared with me"))))
		{
			path = CServerPath(fztranslate("/My Drives/OneDrive") + remoteDir);
		}
	}
}

void site_manager::UpdateGoogleDrivePath(CServerPath& path)
{
	if (!path.empty()) {
		auto const & remoteDir = path;
		if (remoteDir == CServerPath(fztranslate("/Team drives"))) {
			path = CServerPath(fztranslate("/Shared drives"));
		}
		else if (remoteDir.IsSubdirOf(CServerPath(fztranslate("/Team drives")), false)) {
			CServerPath newRemoteDir(fztranslate("/Shared drives"));

			std::deque<std::wstring> segments;
			CServerPath dir = remoteDir;
			while (dir.HasParent()) {
				segments.push_back(dir.GetLastSegment());
				dir.MakeParent();
			}

			segments.pop_back();
			while (!segments.empty()) {
				newRemoteDir.AddSegment(segments.back());
				segments.pop_back();
			}

			path = newRemoteDir;
		}
	}
}

void site_manager::UpgradeCloudflareR2Host(CServer & server, int64_t version)
{
	// the first released version to support jurisdictions
	auto newVersion = ConvertToVersionNumber(L"3.68-rc1");
	if (version < newVersion) {
		auto host = server.GetHost();
		if (host != L"r2.cloudflarestorage.com" && host != L"eu.r2.cloudflarestorage.com" &&
			host != L"fedram.r2.cloudflarestorage.com") {
			server.SetHost(L"r2.cloudflarestorage.com", server.GetPort());
		}
	}
}

bool site_manager::ReadBookmarkElement(Bookmark & bookmark, pugi::xml_node element)
{
	bookmark.m_localDir = GetTextElement(element, "LocalDir");
	bookmark.m_remoteDir.SetSafePath(GetTextElement(element, "RemoteDir"));

	if (bookmark.m_localDir.empty() && bookmark.m_remoteDir.empty()) {
		return false;
	}

	if (!bookmark.m_localDir.empty() && !bookmark.m_remoteDir.empty()) {
		bookmark.m_sync = GetTextElementBool(element, "SyncBrowsing", false);
	}

	bookmark.m_comparison = GetTextElementBool(element, "DirectoryComparison", false);

	return true;
}

site_colour site_manager::GetColourFromIndex(int i)
{
	if (i < 0 || static_cast<unsigned int>(i) >= static_cast<unsigned int>(site_colour::end_of_list)) {
		return site_colour::none;
	}
	return site_colour(i);
}

std::unique_ptr<Site> site_manager::ReadServerElement(pugi::xml_node element, int64_t version)
{
	auto data = std::make_unique<Site>();
	if (!::GetServer(element, *data)) {
		return nullptr;
	}
	if (data->GetName().empty()) {
		return nullptr;
	}

	data->comments_ = GetTextElement(element, "Comments");
	data->m_colour = GetColourFromIndex(GetTextElementInt(element, "Colour"));

	ReadBookmarkElement(data->m_default_bookmark, element);
	if (data->server.GetProtocol() == ONEDRIVE) {
		UpdateOneDrivePath(data->m_default_bookmark.m_remoteDir);
	}
	else if (data->server.GetProtocol() == GOOGLE_DRIVE) {
		UpdateGoogleDrivePath(data->m_default_bookmark.m_remoteDir);
	}
	else if (data->server.GetProtocol() == CLOUDFLARE_R2) {
		UpgradeCloudflareR2Host(data->server, version);
	}

	// Bookmarks
	for (auto bookmark = element.child("Bookmark"); bookmark; bookmark = bookmark.next_sibling("Bookmark")) {
		std::wstring name = GetTextElement_Trimmed(bookmark, "Name");
		if (name.empty()) {
			continue;
		}

		Bookmark bookmarkData;
		if (ReadBookmarkElement(bookmarkData, bookmark)) {
			if (data->server.GetProtocol() == ONEDRIVE) {
				UpdateOneDrivePath(bookmarkData.m_remoteDir);
			}
			else if (data->server.GetProtocol() == GOOGLE_DRIVE) {
				UpdateGoogleDrivePath(bookmarkData.m_remoteDir);
			}

			bookmarkData.m_name = name.substr(0, 255);
			data->m_bookmarks.push_back(bookmarkData);
		}
	}

	return data;
}

bool site_manager::LoadPredefined(CLocalPath const& defaults_dir, CSiteManagerXmlHandler& handler)
{
	if (defaults_dir.empty()) {
		return false;
	}

	std::wstring const name(defaults_dir.GetPath() + L"fzdefaults.xml");
	CXmlFile file(name);

	auto document = file.Load();
	if (!document) {
		return false;
	}

	auto element = document.child("Servers");
	if (!element) {
		return false;
	}

	if (!Load(element, handler, file.GetVersion())) {
		return false;
	}

	return true;
}

bool site_manager::UnescapeSitePath(std::wstring path, std::vector<std::wstring>& result)
{
	result.clear();

	std::wstring name;
	wchar_t const* p = path.c_str();

	// Undo escapement
	bool lastBackslash = false;
	while (*p) {
		const wchar_t& c = *p;
		if (c == '\\') {
			if (lastBackslash) {
				name += L"\\";
				lastBackslash = false;
			}
			else {
				lastBackslash = true;
			}
		}
		else if (c == '/') {
			if (lastBackslash) {
				name += L"/";
				lastBackslash = 0;
			}
			else {
				if (!name.empty()) {
					result.push_back(name);
				}
				name.clear();
			}
		}
		else {
			name += *p;
		}
		++p;
	}
	if (lastBackslash) {
		return false;
	}
	if (!name.empty()) {
		result.push_back(name);
	}

	return !result.empty();
}

std::wstring site_manager::EscapeSegment(std::wstring segment)
{
	fz::replace_substrings(segment, L"\\", L"\\\\");
	fz::replace_substrings(segment, L"/", L"\\/");
	return segment;
}

std::wstring site_manager::BuildPath(wchar_t root, std::vector<std::wstring> const& segments)
{
	std::wstring ret;
	ret += root;
	for (auto const& segment : segments) {
		ret += L"/" + EscapeSegment(segment);
	}

	return ret;
}

std::pair<std::unique_ptr<Site>, Bookmark> site_manager::GetSiteByPath(app_paths const& paths, std::wstring sitePath, std::wstring& error)
{
	std::pair<std::unique_ptr<Site>, Bookmark> ret;
	wchar_t c = sitePath.empty() ? 0 : sitePath[0];
	if (c != '0' && c != '1') {
		error = fz::translate("Site path has to begin with 0 or 1.");
		return ret;
	}

	sitePath = sitePath.substr(1);

	// We have to synchronize access to sitemanager.xml so that multiple processed don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	CXmlFile file;
	if (c == '0') {
		file.SetFileName(paths.settings_file(L"sitemanager"));
	}
	else {
		CLocalPath const defaultsDir = paths.defaults_path;
		if (defaultsDir.empty()) {
			error = fz::translate("Site does not exist.");
			return ret;
		}
		file.SetFileName(defaultsDir.GetPath() + L"fzdefaults.xml");
	}

	auto document = file.Load();
	if (!document) {
		error = fz::translate("Error loading xml file");
		return ret;
	}

	auto element = document.child("Servers");
	if (!element) {
		error = fz::translate("Site does not exist.");
		return ret;
	}

	std::vector<std::wstring> segments;
	if (!UnescapeSitePath(sitePath, segments) || segments.empty()) {
		error = fz::translate("Site path is malformed.");
		return ret;
	}

	auto child = GetElementByPath(element, segments);
	if (!child) {
		error = fz::translate("Site does not exist.");
		return ret;
	}

	pugi::xml_node bookmark;
	if (!strcmp(child.name(), "Bookmark")) {
		bookmark = child;
		child = child.parent();
		segments.pop_back();
	}

	auto version = file.GetVersion();

	ret.first = ReadServerElement(child, version);
	if (!ret.first) {
		error = fz::translate("Could not read server item.");
	}
	else {
		if (bookmark) {
			Bookmark bm;
			if (ReadBookmarkElement(bm, bookmark)) {
				ret.second = bm;
			}
		}
		else {
			ret.second = ret.first->m_default_bookmark;
		}

		ret.first->SetSitePath(BuildPath(c, segments));
	}

	return ret;
}

pugi::xml_node site_manager::GetElementByPath(pugi::xml_node node, std::vector<std::wstring> const& segments)
{
	for (auto const& segment : segments) {
		pugi::xml_node child;
		for (child = node.first_child(); child; child = child.next_sibling()) {
			if (strcmp(child.name(), "Server") && strcmp(child.name(), "Folder") && strcmp(child.name(), "Bookmark")) {
				continue;
			}

			std::wstring name = GetTextElement_Trimmed(child, "Name");
			if (name.empty()) {
				name = GetTextElement_Trimmed(child);
			}
			if (name.empty()) {
				continue;
			}

			if (name == segment) {
				break;
			}
		}
		if (!child) {
			return pugi::xml_node();
		}

		node = child;
		continue;
	}

	return node;
}
