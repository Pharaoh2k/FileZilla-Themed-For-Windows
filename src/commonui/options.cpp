#include "options.h"

#include "fz_paths.h"
#include "hostkey_store.h"
#include "ipcmutex.h"
#include "xml_file.h"

#include "../include/version.h"

#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/translate.hpp>
#include <libfilezilla/util.hpp>


#ifdef FZ_WINDOWS
#include <shlobj.h>
#endif

#include <string.h>

#ifdef FZ_WINDOWS
//case insensitive
#define DEFAULT_FILENAME_SORT   0
#else
//case sensitive
#define DEFAULT_FILENAME_SORT   1
#endif

namespace {
#ifdef FZ_WINDOWS
	auto const platform_name = "win";
#elif defined(FZ_MAC)
	auto const platform_name = "mac";
#else
	auto const platform_name = "unix";
#endif

unsigned int register_common_options()
{
	// Note: A few options are versioned due to a changed
	// option syntax or past, unhealthy defaults
	static int const value = register_options({ { "Config Location", L"", option_flags::predefined_only | option_flags::platform },
		{ "Kiosk mode", 0, option_flags::predefined_priority, 0, 2 },

		{ "Master password encryptor", L"", option_flags::normal },
		{ "Trust system trust store", false, option_flags::normal },

		{ "Ascii Binary mode", 0, option_flags::normal, 0, 2 },
		{ "Auto Ascii files", L"ac|am|asp|bat|c|cfm|cgi|conf|cpp|css|dhtml|diff|diz|h|hpp|htm|html|in|inc|java|js|jsp|lua|m4|mak|md5|nfo|nsh|nsi|pas|patch|pem|php|phtml|pl|po|pot|py|qmail|sh|sha1|sha256|sha512|shtml|sql|svg|tcl|tpl|txt|vbs|xhtml|xml|xrc", option_flags::normal },
		{ "Auto Ascii no extension", L"1", option_flags::normal },
		{ "Auto Ascii dotfiles", true, option_flags::normal },
		{ "Comparison threshold", 1, option_flags::normal, 0, 1440 },
		{ "Remote recursive operation list refresh",  false, option_flags::normal }
	});

	return value;
}

option_registrator r(&register_common_options);
}

optionsIndex mapOption(commonOptions opt)
{
	static unsigned int const offset = register_common_options();

	auto ret = optionsIndex::invalid;
	if (opt < OPTIONS_COMMON_NUM) {
		return static_cast<optionsIndex>(opt + offset);
	}
	return ret;
}

XmlOptions::XmlOptions(std::string_view product_name)
	: product_name_(product_name)
{
}

bool XmlOptions::Load(std::wstring & error)
{
	bool ret{};

	LoadGlobalDefaultOptions();

	CLocalPath const dir = InitSettingsDir();

	CReentrantInterProcessMutexLocker mutex(MUTEX_OPTIONS);
	xmlFile_ = std::make_unique<CXmlFile>(dir.GetPath() + L"filezilla.xml");
	if (!xmlFile_->Load()) {
		error = xmlFile_->GetError();
	}
	else {
		auto settings = CreateSettingsXmlElement();
		Load(settings, false, false);
		int64_t version = xmlFile_->GetVersion();
		if (version > 0) {
			if (RunMigrations(version)) {
				std::wstring tmp;
				Save(false, tmp);
			}
		}
		ret = true;
	}

	{
		fz::scoped_write_lock l(mtx_);
		changed_.clear();
		can_notify_ = true;
	}

	return ret;
}

XmlOptions::~XmlOptions()
{
}

pugi::xml_node XmlOptions::CreateSettingsXmlElement()
{
	if (!xmlFile_) {
		return pugi::xml_node();
	}

	auto element = xmlFile_->GetElement();
	if (!element) {
		return element;
	}

	auto settings = element.child("Settings");
	if (!settings) {
		settings = element.append_child("Settings");
	}

	return settings;
}

void XmlOptions::Import(pugi::xml_node& element)
{
	Load(element, false, true);
}

void XmlOptions::Load(pugi::xml_node& settings, bool predefined, bool importing)
{
	if (!settings) {
		return;
	}

	fz::scoped_write_lock l(mtx_);
	add_missing(l);

	std::vector<uint8_t> seen;
	seen.resize(options_.size());

	pugi::xml_node next;
	for (auto setting = settings.child("Setting"); setting; setting = next) {
		next = setting.next_sibling("Setting");

		const char* name = setting.attribute("name").value();
		if (!name || !*name) {
			continue;
		}

		auto def_it = name_to_option_.find(name);
		if (def_it == name_to_option_.cend()) {
			continue;
		}

		auto const& def = options_[def_it->second];

		if (def.flags() & option_flags::platform) {
			char const* p = setting.attribute("platform").value();
			if (*p && strcmp(p, platform_name)) {
				continue;
			}
		}
		if (def.flags() & option_flags::product) {
			char const* p = setting.attribute("product").value();
			if (product_name_ != p) {
				continue;
			}
		}

		if (seen[def_it->second]) {
			if (!predefined && !importing) {
				settings.remove_child(setting);
				set_dirty();
			}
			continue;
		}
		seen[def_it->second] = 1;

		auto& val = values_[def_it->second];

		switch (def.type()) {
		case option_type::number:
		case option_type::boolean:
			set(static_cast<optionsIndex>(def_it->second), def, val, setting.text().as_int(), predefined);
			break;
		case option_type::xml:
		{
			pugi::xml_document doc;
			for (auto c = setting.first_child(); c; c = c.next_sibling()) {
				doc.append_copy(c);
			}
			set(static_cast<optionsIndex>(def_it->second), def, val, std::move(doc), predefined);
		}
		break;
		default:
			set(static_cast<optionsIndex>(def_it->second), def, val, fz::to_wstring_from_utf8(setting.child_value()), predefined);
		}
	}

	if (!predefined && !importing) {
		for (size_t i = 0; i < seen.size(); ++i) {
			if (seen[i]) {
				continue;
			}

			set_xml_value(settings, i, false);
		}
	}
}

void XmlOptions::LoadGlobalDefaultOptions()
{
	CLocalPath const defaultsDir = GetDefaultsDir();
	if (defaultsDir.empty()) {
		return;
	}
	CXmlFile file(defaultsDir.GetPath() + L"fzdefaults.xml");
	if (!file.Load()) {
		return;
	}

	auto element = file.GetElement();
	if (!element) {
		return;
	}

	element = element.child("Settings");
	if (!element) {
		return;
	}

	Load(element, true, false);
}

bool XmlOptions::Save(bool processChanged, std::wstring & error)
{
	if (processChanged) {
		continue_notify_changed();
	}

	if (!dirty_) {
		return true;
	}
	dirty_ = false;

	if (get_int(OPTION_DEFAULT_KIOSKMODE) == 2) {
		return true;
	}

	if (!xmlFile_) {
		error = fztranslate("No settings loaded to save.");
		return false;
	}

	CReentrantInterProcessMutexLocker mutex(MUTEX_OPTIONS);
	bool ret = xmlFile_->Save(true);
	error = xmlFile_->GetError();
	return ret;
}

bool XmlOptions::Cleanup()
{
	bool ret = false;

	fz::scoped_write_lock l(mtx_);

	// Clear known sensitive settings
	for (size_t i = 0; i < options_.size(); ++i) {
		if (options_[i].flags() & option_flags::sensitive_data) {
			set_default_value(static_cast<optionsIndex>(i));
			set_changed(static_cast<optionsIndex>(i));
		}
	}

	auto element = xmlFile_->GetElement();
	auto settings = element.child("Settings");

	// Remove all but the first settings element
	auto child = settings.next_sibling("Settings");
	while (child) {
		auto next = child.next_sibling("Settings");
		element.remove_child(child);
		child = next;
	}

	child = settings.first_child();

	while (child) {
		auto next = child.next_sibling();

		if (child.name() == std::string("Setting")) {
			// Remove sensitive settings
			if (!strcmp(child.attribute("sensitive").value(), "1")) {
				settings.remove_child(child);
				ret = true;
			}
		}
		else {
			// Only settings in the settings node.
			settings.remove_child(child);
			ret = true;
		}
		child = next;
	}

	if (ret) {
		set_dirty();
	}

	return ret;
}

CLocalPath XmlOptions::InitSettingsDir()
{
	CLocalPath p;

	std::wstring dir = get_string(OPTION_DEFAULT_SETTINGSDIR);
	if (!dir.empty()) {
		dir = ExpandPath(dir);
		p.SetPath(GetDefaultsDir().GetPath());
		p.ChangePath(dir);
	}
	else {
		p = GetUnadjustedSettingsDir();
	}

	if (!p.empty() && !p.Exists()) {
		fz::mkdir(fz::to_native(p.GetPath()), true, fz::mkdir_permissions::cur_user_and_admins);
	}

	set(OPTION_DEFAULT_SETTINGSDIR, p.GetPath(), true);
	set_ipcmutex_lockfile_path(p.GetPath());

	return p;
}

void XmlOptions::process_changed(watched_options const& changed)
{
	auto settings = CreateSettingsXmlElement();
	if (!settings) {
		return;
	}
	for (size_t i = 0; i < changed.options_.size(); ++i) {
		uint64_t v = changed.options_[i];
		while (v) {
			auto bit = fz::bitscan(v);
			v ^= 1ull << bit;
			size_t opt = bit + i * 64;

			set_xml_value(settings, opt, true);
		}
	}
}

void XmlOptions::set_xml_value(pugi::xml_node& settings, size_t opt, bool clean)
{
	auto const& def = options_[opt];
	if (def.flags() & (option_flags::internal | option_flags::predefined_only)) {
		return;
	}
	if (def.name().empty()) {
		return;
	}

	if (clean) {
		for (pugi::xml_node it = settings.child("Setting"); it;) {
			auto cur = it;
			it = it.next_sibling("Setting");

			char const* attribute = cur.attribute("name").value();
			if (strcmp(attribute, def.name().c_str())) {
				continue;
			}

			if (def.flags() & option_flags::platform) {
				// Ignore items from the wrong platform
				char const* p = cur.attribute("platform").value();
				if (*p && strcmp(p, platform_name)) {
					continue;
				}
			}

			if (def.flags() & option_flags::product) {
				// Ignore items from the wrong product
				char const* p = cur.attribute("product").value();
				if (product_name_ != p) {
					continue;
				}
			}

			settings.remove_child(cur);
		}
	}

	auto setting = settings.append_child("Setting");
	setting.append_attribute("name").set_value(def.name().c_str());
	if (def.flags() & option_flags::platform) {
		setting.append_attribute("platform").set_value(platform_name);
	}
	if (def.flags() & option_flags::product && !product_name_.empty()) {
		setting.append_attribute("product").set_value(product_name_.c_str());
	}

	if (def.flags() & option_flags::sensitive_data) {
		setting.append_attribute("sensitive").set_value("1");
	}

	auto const& val = values_[opt];
	if (def.type() == option_type::xml) {
		for (auto c = val.xml_->first_child(); c; c = c.next_sibling()) {
			setting.append_copy(c);
		}
	}
	else {
		setting.text().set(fz::to_utf8(val.str_).c_str());
	}

	set_dirty();
}

void XmlOptions::set_dirty()
{
	dirty_ = true;
	on_dirty();
}

bool XmlOptions::RunMigrations(int64_t old_version)
{
	bool made_changes{};
	if (old_version < ConvertToVersionNumber(L"3.70.0-beta1")) {
		if (fz::local_filesys::get_file_type(fz::to_native(get_string(OPTION_DEFAULT_SETTINGSDIR) + L"hostkeys.xml"), true) == fz::local_filesys::unknown) {
			ImportPuttyHostkeys(*this);
			made_changes = true;
		}
	}

	return made_changes;
}
