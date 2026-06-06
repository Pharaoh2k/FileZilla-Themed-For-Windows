#ifndef FILEZILLA_COMMONUI_OPTIONS_HEADER
#define FILEZILLA_COMMONUI_OPTIONS_HEADER

#include "visibility.h"

#include "../include/local_path.h"
#include "../include/optionsbase.h"

#include <libfilezilla/mutex.hpp>

enum commonOptions : unsigned int {
	OPTION_DEFAULT_SETTINGSDIR, // If non-empty, guaranteed to be (back)slash-terminated
	OPTION_DEFAULT_KIOSKMODE,

	OPTION_MASTERPASSWORDENCRYPTOR,
	OPTION_TRUST_SYSTEM_TRUST_STORE,

	OPTION_ASCIIBINARY,
	OPTION_ASCIIFILES,
	OPTION_ASCIINOEXT,
	OPTION_ASCIIDOTFILE,

	OPTION_COMPARISON_THRESHOLD,

	OPTION_REMOTE_ROP_LISTING_REFRESH,

	OPTIONS_COMMON_NUM
};

optionsIndex FZCUI_PUBLIC_SYMBOL mapOption(commonOptions opt);

class CXmlFile;
class FZCUI_PUBLIC_SYMBOL XmlOptions : public COptionsBase
{
public:
	XmlOptions(std::string_view product_name);
	virtual ~XmlOptions();

	XmlOptions(XmlOptions const&) = delete;
	XmlOptions& operator=(XmlOptions const&) = delete;

	void Import(pugi::xml_node& element);

	bool Load(std::wstring & error);
	bool Save(bool processChanged, std::wstring & error);

	bool Cleanup(); // Removes all unknown elements from the XML

protected:
	std::unique_ptr<CXmlFile> xmlFile_;

	virtual bool RunMigrations(int64_t old_version);

private:

	void FZCUI_PRIVATE_SYMBOL Load(pugi::xml_node& settings, bool predefined, bool importing);

	pugi::xml_node FZCUI_PRIVATE_SYMBOL CreateSettingsXmlElement();

	void FZCUI_PRIVATE_SYMBOL LoadGlobalDefaultOptions();
	CLocalPath FZCUI_PRIVATE_SYMBOL InitSettingsDir();

	virtual void process_changed(watched_options const& changed) override;
	void FZCUI_PRIVATE_SYMBOL set_xml_value(pugi::xml_node& settings, size_t opt, bool clean);

	void FZCUI_PRIVATE_SYMBOL set_dirty();
	bool dirty_{};
	virtual void on_dirty() = 0;

	std::string const product_name_;
};

#endif
