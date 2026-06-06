#ifndef FILEZILLA_COMMONUI_SITEMANAGER_HEADER
#define FILEZILLA_COMMONUI_SITEMANAGER_HEADER

#include "xml_file.h"
#include "visibility.h"

class FZCUI_PUBLIC_SYMBOL CSiteManagerXmlHandler
{
public:
	virtual ~CSiteManagerXmlHandler() = default;

	// Adds a folder and descents
	virtual bool AddFolder(std::wstring const& name, bool expanded) = 0;
	virtual bool AddSite(std::unique_ptr<Site> data) = 0;

	// Go up a level
	virtual bool LevelUp() { return true; } // *Ding*
};

class FZCUI_PUBLIC_SYMBOL CSiteManagerSaveXmlHandler
{
public:
	virtual ~CSiteManagerSaveXmlHandler() = default;
	virtual bool SaveTo(pugi::xml_node element) = 0;
};

class app_paths;
class COptionsBase;

// for now just read-only interface and rest still in interface CSiteManager waiting for refactoring
class FZCUI_PUBLIC_SYMBOL site_manager
{
public:
	virtual ~site_manager() = default;

	static std::pair<std::unique_ptr<Site>, Bookmark> GetSiteByPath(app_paths const& paths, std::wstring sitePath, std::wstring& error);

	static bool UnescapeSitePath(std::wstring path, std::vector<std::wstring>& result);
	static std::wstring EscapeSegment(std::wstring segment);

	static void UpdateOneDrivePath(CServerPath & path);
	static void UpdateGoogleDrivePath(CServerPath & path);
	static void UpgradeCloudflareR2Host(CServer & server, int64_t version);

	static site_colour GetColourFromIndex(int i);

	static bool Load(std::wstring const& settings_file, CSiteManagerXmlHandler& pHandler, std::wstring& error);
	static bool Save(std::wstring const& settings_file, CSiteManagerSaveXmlHandler& pHandler, std::wstring& error);
	static void Save(pugi::xml_node element, Site const& site, login_manager& lim, COptionsBase& options);

protected:
	static bool ReadBookmarkElement(Bookmark& bookmark, pugi::xml_node element);

	static bool Load(pugi::xml_node element, CSiteManagerXmlHandler& pHandler, int64_t version);

	static std::unique_ptr<Site> ReadServerElement(pugi::xml_node element, int64_t version);

	static pugi::xml_node GetElementByPath(pugi::xml_node node, std::vector<std::wstring> const& segments);
	static std::wstring BuildPath(wchar_t root, std::vector<std::wstring> const& segments);

	static bool LoadPredefined(CLocalPath const& defaults_dir, CSiteManagerXmlHandler& handler);
};

#endif
