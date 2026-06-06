#include "filezilla.h"
#include "sitemanager.h"

#include "dialogex.h"
#include "filezillaapp.h"
#include "loginmanager.h"
#include "Options.h"
#include "xmlfunctions.h"

#include "../commonui/fz_paths.h"
#include "../commonui/ipcmutex.h"
#include "../commonui/protect.h"

#include <libfilezilla/translate.hpp>

#include <wx/menu.h>

namespace {
struct background_color {
	site_colour const color;
	char const*const name;
};

//note: the order needs to be the same as in site_colour (site.h) as that is used as index for this array
background_color const background_colors[] = {
	{ site_colour::none, fztranslate_mark("None") },
	{ site_colour::red, fztranslate_mark("Red") },
	{ site_colour::green, fztranslate_mark("Green") },
	{ site_colour::blue, fztranslate_mark("Blue") },
	{ site_colour::yellow, fztranslate_mark("Yellow") },
	{ site_colour::cyan, fztranslate_mark("Cyan") },
	{ site_colour::magenta, fztranslate_mark("Magenta") },
	{ site_colour::orange, fztranslate_mark("Orange") },
	{ site_colour::end_of_list, nullptr }
};
}

std::map<int, std::unique_ptr<Site>> CSiteManager::m_idMap;

bool CSiteManager::Load(CSiteManagerXmlHandler& handler)
{
	std::wstring error;
	bool ret = site_manager::Load(wxGetApp().GetSettingsFile(_T("sitemanager")), handler, error);
	if (!ret) {
		wxMessageBoxEx(error, _("Error loading xml file"), wxICON_ERROR);
	}
	return ret;
}

class CSiteManagerXmlHandler_Menu : public CSiteManagerXmlHandler
{
public:
	CSiteManagerXmlHandler_Menu(wxMenu* pMenu, std::map<int, std::unique_ptr<Site>> *idMap, bool predefined)
		: m_pMenu(pMenu), m_idMap(idMap)
	{
		if (predefined) {
			path = _T("1");
		}
		else {
			path = _T("0");
		}
	}

	unsigned int GetInsertIndex(wxMenu* pMenu, const wxString& name)
	{
		unsigned int i;
		for (i = 0; i < pMenu->GetMenuItemCount(); ++i) {
			const wxMenuItem* const pItem = pMenu->FindItemByPosition(i);
			if (!pItem) {
				continue;
			}

			// Use same sorting as site tree in site manager
#ifdef __WXMSW__
			if (pItem->GetItemLabelText().CmpNoCase(name) > 0) {
				break;
			}
#else
			if (pItem->GetItemLabelText() > name) {
				break;
			}
#endif
		}

		return i;
	}

	virtual bool AddFolder(std::wstring const& name, bool)
	{
		m_parents.push_back(m_pMenu);
		m_childNames.push_back(name);
		m_paths.push_back(path);
		path += _T("/") + CSiteManager::EscapeSegment(name);

		m_pMenu = new wxMenu;

		return true;
	}

	virtual bool AddSite(std::unique_ptr<Site> data)
	{
		std::wstring newName(data->GetName());
		int i = GetInsertIndex(m_pMenu, newName);
		newName = LabelEscape(newName);
		wxMenuItem* pItem = m_pMenu->Insert(i, wxID_ANY, newName);

		data->SetSitePath(path + _T("/") + CSiteManager::EscapeSegment(data->GetName()));

		(*m_idMap)[pItem->GetId()] = std::move(data);

		return true;
	}

	// Go up a level
	virtual bool LevelUp()
	{
		if (m_parents.empty() || m_childNames.empty()) {
			return false;
		}

		wxMenu* pChild = m_pMenu;
		m_pMenu = m_parents.back();
		if (pChild->GetMenuItemCount()) {
			std::wstring name = m_childNames.back();
			int i = GetInsertIndex(m_pMenu, name);
			name = LabelEscape(name);

			wxMenuItem* pItem = new wxMenuItem(m_pMenu, wxID_ANY, name, _T(""), wxITEM_NORMAL, pChild);
			m_pMenu->Insert(i, pItem);
		}
		else {
			delete pChild;
		}
		m_childNames.pop_back();
		m_parents.pop_back();

		path = m_paths.back();
		m_paths.pop_back();

		return true;
	}

protected:
	wxMenu* m_pMenu;

	std::map<int, std::unique_ptr<Site>> *m_idMap;

	std::deque<wxMenu*> m_parents;
	std::deque<std::wstring> m_childNames;

	std::wstring path;
	std::deque<std::wstring> m_paths;
};


class CSiteManagerXmlHandler_Stats : public CSiteManagerXmlHandler
{
public:
	virtual bool AddFolder(std::wstring const&, bool)
	{
		++directories_;
		return true;
	}

	virtual bool AddSite(std::unique_ptr<Site> site)
	{
		if (site) {
			++sites_;
			bookmarks_ += site->m_bookmarks.size();
		}
		return true;
	}

	unsigned int sites_{};
	unsigned int directories_{};
	unsigned int bookmarks_{};
};

std::unique_ptr<wxMenu> CSiteManager::GetSitesMenu()
{
	ClearIdMap();

	// We have to synchronize access to sitemanager.xml so that multiple processed don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	auto predefinedSites = GetSitesMenu_Predefined(m_idMap);

	auto pMenu = std::make_unique<wxMenu>();
	CSiteManagerXmlHandler_Menu handler(pMenu.get(), &m_idMap, false);

	bool res = Load(handler);
	if (!res || !pMenu->GetMenuItemCount()) {
		pMenu.reset();
	}

	if (pMenu) {
		if (!predefinedSites) {
			return pMenu;
		}

		auto pRootMenu = std::make_unique<wxMenu>();
		pRootMenu->AppendSubMenu(predefinedSites.release(), _("Predefined Sites"));
		pRootMenu->AppendSubMenu(pMenu.release(), _("My Sites"));

		return pRootMenu;
	}

	if (predefinedSites) {
		return predefinedSites;
	}

	pMenu = std::make_unique<wxMenu>();
	wxMenuItem* pItem = pMenu->Append(wxID_ANY, _("No sites available"));
	pItem->Enable(false);
	return pMenu;
}

void CSiteManager::ClearIdMap()
{
	m_idMap.clear();
}

bool CSiteManager::LoadPredefined(CSiteManagerXmlHandler& handler)
{
	return site_manager::LoadPredefined(GetDefaultsDir(), handler);
}

std::unique_ptr<wxMenu> CSiteManager::GetSitesMenu_Predefined(std::map<int, std::unique_ptr<Site>> &idMap)
{
	auto pMenu = std::make_unique<wxMenu>();
	CSiteManagerXmlHandler_Menu handler(pMenu.get(), &idMap, true);

	if (!LoadPredefined(handler)) {
		return 0;
	}

	if (!pMenu->GetMenuItemCount()) {
		return 0;
	}

	return pMenu;
}

std::unique_ptr<Site> CSiteManager::GetSiteById(int id)
{
	auto iter = m_idMap.find(id);

	std::unique_ptr<Site> pData;
	if (iter != m_idMap.end()) {
		pData = std::move(iter->second);
	}
	ClearIdMap();

	return pData;
}

std::pair<std::unique_ptr<Site>, Bookmark> CSiteManager::GetSiteByPath(COptionsBase & options, std::wstring const& sitePath, bool printErrors)
{
	std::wstring error;

	CLocalPath settings_path{options.get_string(OPTION_DEFAULT_SETTINGSDIR)};
	app_paths paths{settings_path, GetDefaultsDir()};
	auto ret = site_manager::GetSiteByPath(paths, sitePath, error);
	if (!ret.first && printErrors) {
		wxMessageBoxEx(_("Site does not exist."), error);
	}

	return ret;
}

std::wstring CSiteManager::AddServer(Site site, COptionsBase & options, login_manager & lim)
{
	// We have to synchronize access to sitemanager.xml so that multiple processed don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("sitemanager")));
	auto document = file.Load();
	if (!document) {
		wxString msg = file.GetError() + _T("\n") + _("The server could not be added.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return std::wstring();
	}

	auto element = document.child("Servers");
	if (!element) {
		element = document.append_child("Servers");
	}

	std::vector<std::wstring> names;
	for (auto child = element.child("Server"); child; child = child.next_sibling("Server")) {
		std::wstring name = GetTextElement(child, "Name");
		if (name.empty()) {
			continue;
		}

		names.push_back(name);
	}

	std::wstring name = fztranslate("New site");
	int i = 1;

	for (;;) {
		std::vector<std::wstring>::const_iterator iter;
		for (iter = names.cbegin(); iter != names.cend(); ++iter) {
			if (*iter == name) {
				break;
			}
		}
		if (iter == names.cend()) {
			break;
		}

		name = _("New site").ToStdWstring() + fz::sprintf(L" %d", ++i);
	}

	site.SetName(name);

	auto xServer = element.append_child("Server");
	SetServer(xServer, site, lim, options);
	AddTextElement(xServer, name);

	if (!file.Save()) {
		if (options.get_int(OPTION_DEFAULT_KIOSKMODE) == 2) {
			return std::wstring();
		}

		std::wstring msg = fz::sprintf(fztranslate("Could not write \"%s\", any changes to the Site Manager could not be saved: %s"), file.GetFileName(), file.GetError());
		wxMessageBoxEx(msg, _("Error writing xml file"), wxICON_ERROR);
		return std::wstring();
	}

	return L"0/" + EscapeSegment(name);
}

bool CSiteManager::AddBookmark(std::wstring sitePath, wxString const& name, wxString const& local_dir, CServerPath const& remote_dir, bool sync, bool comparison)
{
	if (local_dir.empty() && remote_dir.empty()) {
		return false;
	}

	auto const c = sitePath[0];
	if (c != '0') {
		return false;
	}

	sitePath = sitePath.substr(1);

	// We have to synchronize access to sitemanager.xml so that multiple processed don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("sitemanager")));
	auto document = file.Load();
	if (!document) {
		wxString msg = file.GetError() + _T("\n") + _("The bookmark could not be added.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	auto element = document.child("Servers");
	if (!element) {
		return false;
	}

	std::vector<std::wstring> segments;
	if (!UnescapeSitePath(sitePath, segments)) {
		wxMessageBoxEx(_("Site path is malformed."), _("Invalid site path"));
		return 0;
	}

	auto child = GetElementByPath(element, segments);
	if (!child || strcmp(child.name(), "Server")) {
		wxMessageBoxEx(_("Site does not exist."), _("Invalid site path"));
		return 0;
	}

	// Bookmarks
	pugi::xml_node insertBefore, bookmark;
	for (bookmark = child.child("Bookmark"); bookmark; bookmark = bookmark.next_sibling("Bookmark")) {
		std::wstring old_name = GetTextElement_Trimmed(bookmark, "Name");
		if (old_name.empty()) {
			continue;
		}

		if (name == old_name) {
			wxMessageBoxEx(_("Name of bookmark already exists."), _("New bookmark"), wxICON_EXCLAMATION);
			return false;
		}
		if (name < old_name && !insertBefore) {
			insertBefore = bookmark;
		}
	}

	if (insertBefore) {
		bookmark = child.insert_child_before("Bookmark", insertBefore);
	}
	else {
		bookmark = child.append_child("Bookmark");
	}
	AddTextElement(bookmark, "Name", name.ToStdWstring());
	if (!local_dir.empty()) {
		AddTextElement(bookmark, "LocalDir", local_dir.ToStdWstring());
	}
	if (!remote_dir.empty()) {
		AddTextElement(bookmark, "RemoteDir", remote_dir.GetSafePath());
	}
	if (sync) {
		AddTextElementUtf8(bookmark, "SyncBrowsing", "1");
	}
	if (comparison) {
		AddTextElementUtf8(bookmark, "DirectoryComparison", "1");
	}

	if (!file.Save()) {
		if (COptions::Get()->get_int(OPTION_DEFAULT_KIOSKMODE) == 2) {
			return true;
		}

		wxString msg = wxString::Format(_("Could not write \"%s\", the selected sites could not be exported: %s"), file.GetFileName(), file.GetError());
		wxMessageBoxEx(msg, _("Error writing xml file"), wxICON_ERROR);
	}

	return true;
}

bool CSiteManager::ClearBookmarks(std::wstring sitePath)
{
	wxChar const c = sitePath.empty() ? 0 : sitePath[0];
	if (c != '0') {
		return false;
	}

	sitePath = sitePath.substr(1);

	// We have to synchronize access to sitemanager.xml so that multiple processed don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("sitemanager")));
	auto document = file.Load();
	if (!document) {
		wxString msg = file.GetError() + _T("\n") + _("The bookmarks could not be cleared.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	auto element = document.child("Servers");
	if (!element) {
		return false;
	}

	std::vector<std::wstring> segments;
	if (!UnescapeSitePath(sitePath, segments)) {
		wxMessageBoxEx(_("Site path is malformed."), _("Invalid site path"));
		return 0;
	}

	auto child = GetElementByPath(element, segments);
	if (!child || strcmp(child.name(), "Server")) {
		wxMessageBoxEx(_("Site does not exist."), _("Invalid site path"));
		return 0;
	}

	auto bookmark = child.child("Bookmark");
	while (bookmark) {
		child.remove_child(bookmark);
		bookmark = child.child("Bookmark");
	}

	if (!file.Save()) {
		if (COptions::Get()->get_int(OPTION_DEFAULT_KIOSKMODE) == 2) {
			return true;
		}

		wxString msg = wxString::Format(_("Could not write \"%s\", the selected sites could not be exported: %s"), file.GetFileName(), file.GetError());
		wxMessageBoxEx(msg, _("Error writing xml file"), wxICON_ERROR);
	}

	return true;
}

bool CSiteManager::HasSites()
{
	CSiteManagerXmlHandler_Stats handler;
	Load(handler);

	return handler.sites_ > 0;
}

int CSiteManager::GetColourIndex(site_colour const& c)
{
	for (int i = 0; background_colors[i].name; ++i) {
		if (c == background_colors[i].color) {
			return i;
		}
	}

	return 0;
}

wxString CSiteManager::GetColourName(int i)
{
	if (i < 0 || static_cast<unsigned int>(i) + 1 >= (sizeof(background_colors) / sizeof(background_color))) {
		return wxString();
	}
	return wxGetTranslation(background_colors[i].name);
}

void CSiteManager::Rewrite(CLoginManager & loginManager, COptionsBase& options, pugi::xml_node element, bool on_failure_set_to_ask, int64_t version)
{
	bool const forget = options.get_int(OPTION_DEFAULT_KIOSKMODE) != 0;
	for (auto child = element.first_child(); child; child = child.next_sibling()) {
		if (!strcmp(child.name(), "Folder")) {
			Rewrite(loginManager, options, child, on_failure_set_to_ask, version);
		}
		else if (!strcmp(child.name(), "Server")) {
			auto site = ReadServerElement(child, version);
			if (site) {
				if (!forget) {
					loginManager.AskDecryptor(site->credentials.encrypted_, true, false);
					unprotect(site->credentials, loginManager.GetDecryptor(site->credentials.encrypted_), on_failure_set_to_ask);
				}
				protect(site->credentials, loginManager, options);
				site_manager::Save(child, *site, loginManager, options);
			}
		}
	}
}

void CSiteManager::Rewrite(CLoginManager & loginManager, COptionsBase& options, bool on_failure_set_to_ask)
{
	if (options.get_int(OPTION_DEFAULT_KIOSKMODE) == 2) {
		return;
	}
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("sitemanager")));
	auto document = file.Load();
	if (!document) {
		wxMessageBoxEx(file.GetError(), _("Error loading xml file"), wxICON_ERROR);
		return;
	}

	auto element = document.child("Servers");
	if (!element) {
		return;
	}

	Rewrite(loginManager, options, element, on_failure_set_to_ask, file.GetVersion());

	SaveWithErrorDialog(file);
}

namespace {
pugi::xml_node GetChildWithName(pugi::xml_node element, std::wstring const& name)
{
	pugi::xml_node child;
	for (child = element.first_child(); child; child = child.next_sibling()) {
		std::wstring childName = GetTextElement_Trimmed(child, "Name");
		if (childName.empty()) {
			childName = GetTextElement_Trimmed(child);
		}
		if (!fz::stricmp(name, childName)) {
			return child;
		}
	}

	return pugi::xml_node();
}

pugi::xml_node GetOrCreateFolderWithName(pugi::xml_node element, std::wstring const& name)
{
	pugi::xml_node child = GetChildWithName(element, name);
	if (child) {
		if (strcmp(child.name(), "Folder")) {
			// We do not allow servers and directories to share the same name
			child = pugi::xml_node();
		}
	}
	else {
		child = element.append_child("Folder");
		AddTextElement(child, name);
	}
	return child;
}
}

bool CSiteManager::ImportSites(pugi::xml_node sites, COptionsBase & options, login_manager& lim)
{
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("sitemanager")));
	auto element = file.Load();
	if (!element) {
		wxString msg = wxString::Format(_("Could not load \"%s\", please make sure the file is valid and can be accessed.\nAny changes made in the Site Manager will not be saved."), file.GetFileName());
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	auto currentSites = element.child("Servers");
	if (!currentSites) {
		currentSites = element.append_child("Servers");
	}

	if (!ImportSites(sites, currentSites, file.GetVersion(), options, lim)) {
		return false;
	}

	return SaveWithErrorDialog(file);
}

bool CSiteManager::ImportSites(pugi::xml_node sitesToImport, pugi::xml_node existingSites, int64_t version, COptionsBase & options, login_manager& lim)
{
	for (auto importFolder = sitesToImport.child("Folder"); importFolder; importFolder = importFolder.next_sibling("Folder")) {
		std::wstring name = GetTextElement_Trimmed(importFolder, "Name").substr(0, 255);
		if (name.empty()) {
			name = GetTextElement_Trimmed(importFolder);
		}
		if (name.empty()) {
			continue;
		}

		std::wstring newName = name.substr(0, 240);
		int i = 2;
		pugi::xml_node folder;
		while (!(folder = GetOrCreateFolderWithName(existingSites, newName))) {
			newName = fz::sprintf(L"%s %d", name.substr(0, 240), i++);
		}

		ImportSites(importFolder, folder, version, options, lim);
	}

	for (auto importSite = sitesToImport.child("Server"); importSite; importSite = importSite.next_sibling("Server")) {
		auto site = ReadServerElement(importSite, version);
		if (!site) {
			continue;
		}

		// Find free name
		auto const& name = site->GetName();
		std::wstring newName = name;
		int i = 2;
		while (GetChildWithName(existingSites, newName)) {
			newName = fz::sprintf(L"%s %d", std::wstring_view(name).substr(0, 240), i++);
		}
		site->SetName(newName);

		unprotect(site->credentials, lim.GetDecryptor(site->credentials.encrypted_), false);
		protect(site->credentials, lim, options);

		auto xsite = existingSites.append_child("Server");
		site_manager::Save(xsite, *site, lim, options);
	}

	return true;
}
