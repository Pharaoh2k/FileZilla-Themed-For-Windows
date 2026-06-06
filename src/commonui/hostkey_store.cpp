#include "hostkey_store.h"

#include <fzssh/pubkey.hpp>

#include "fz_paths.h"
#include "options.h"

#include "ipcmutex.h"

#if FZ_WINDOWS
#include <libfilezilla/glue/registry.hpp>
#endif

#include <set>

HostkeyStore::~HostkeyStore()
{
}

bool HostkeyStore::is_insecure_algorithm(std::string_view const& alg)
{
	return
		alg == "aes128-cbc"sv ||
		alg == "aes192-cbc"sv ||
		alg == "aes256-cbc"sv ||
		alg == "ssh-rsa"sv ||
		alg == "diffie-hellman-group1-sha1"sv ||
		alg == "diffie-hellman-group14-sha1"sv ||
		alg == "diffie-hellman-group-exchange-sha1"sv;
}

std::string HostkeyStore::get_insecure_algorithms(fz::ssh::algorithm_info algs)
{
	std::string insecure;

	auto check = [&](std::string const& alg){
		if (is_insecure_algorithm(alg)) {
			fz::ssh::add_algorithm(insecure, alg);
		}
	};

	check(algs.kex_);
	check(algs.cipher_c2s_);
	check(algs.cipher_s2c_);
	check(algs.mac_c2s_);
	check(algs.mac_s2c_);
	check(algs.hostkey_signature_);

	return insecure;
}

bool HostkeyStore::entry::known_key(std::string_view const& key) const
{
	for (auto const& k : hostkeys_) {
		if (key == k) {
			return true;
		}
	}
	return false;
}

namespace {
bool is_subset(std::string_view const& subset, std::string_view const& all)
{
	for (auto const& alg : fz::strtokenizer(subset, ',', true)) {
		if (!fz::ssh::known_algorithm(alg, all)) {
			return false;
		}
	}
	return true;
}
}

bool HostkeyStore::IsTrusted(Site const& site, fz::ssh::public_key const& key, fz::ssh::algorithm_info const& info, bool silent)
{
	Load();

	auto host = fz::to_utf8(fz::str_tolower(site.server.GetHost()));

	entry * host_data{};

	auto it = session_trusted_keys_.find({host, site.server.GetPort()});
	if (it != session_trusted_keys_.end()) {
		host_data = &it->second;
	}
	else {
		it = trusted_keys_.find({host, site.server.GetPort()});
		if (it != trusted_keys_.end()) {
			host_data = &it->second;
		}
	}

	auto flags = verification_flags(0);

	auto insecure_algorithms = get_insecure_algorithms(info);
	if (!insecure_algorithms.empty()) {
		flags |= verification_flags::insecure;
	}

	if (host_data) {
		bool known = host_data->known_key(key.pubkey_blob());
		if (!known) {
			flags |= verification_flags::changed;
		}

		if (known && is_subset(insecure_algorithms, host_data->allowed_insecure_algorithms_)) {
			// All insecure algorithms have previously been allowed and the key is known

			if (insecure_algorithms != host_data->allowed_insecure_algorithms_) {
				// The serve has become more secure, update list of algorithms
				host_data->allowed_insecure_algorithms_ = insecure_algorithms;
				Save(silent);
			}
			return true;
		}
	}
	else {
		flags |= verification_flags::unknown;
	}

	if (silent) {
		return false;
	}

	bool remember{};
	bool trusted =  AskUnknownHostkey(site, key, info, flags, remember);

	if (trusted) {
		if (remember) {
			session_trusted_keys_.erase({host, site.server.GetPort()});
			Load();
			auto & e = trusted_keys_[{host, site.server.GetPort()}];
			e.hostkeys_.clear();
			e.hostkeys_.emplace_back(key.pubkey_blob());
			e.allowed_insecure_algorithms_ = insecure_algorithms;
			Save(false);
		}
		else {
			auto & e = session_trusted_keys_[{host, site.server.GetPort()}];
			e.hostkeys_.clear();
			e.hostkeys_.emplace_back(key.pubkey_blob());
			e.allowed_insecure_algorithms_ = insecure_algorithms;
		}
	}

	return trusted;
}


XmlHostkeyStore::XmlHostkeyStore(COptionsBase & options)
	: options_(options)
	, file_(options_.get_string(OPTION_DEFAULT_SETTINGSDIR) + L"hostkeys.xml")
{
}

void XmlHostkeyStore::Load()
{
	CInterProcessMutex mutex(MUTEX_TRUSTEDHOSTKEYS);
	if (!file_.Modified()) {
		return;
	}

	auto root = file_.Load();
	if (!root) {
		file_.CreateEmpty();
	}

	trusted_keys_.clear();

	pugi::xml_node element;
	for (element = root.child("Server"); element; element = element.next_sibling("Server")) {
		auto host = element.attribute("Host").value();
		auto port = static_cast<uint16_t>(element.attribute("Port").as_uint());

		std::vector<std::string> blobs;

		for (auto hostkey = element.child("Hostkey"); hostkey; hostkey = hostkey.next_sibling("Hostkey")) {
			auto blob = fz::base64_decode_s(hostkey.text().get());
			if (!blob.empty()) {
				blobs.emplace_back(std::move(blob));
			}
		}

		if (!*host || !port || blobs.empty()) {
			continue;
		}

		auto & e = trusted_keys_[{host, port}];
		e.hostkeys_ = std::move(blobs);
		e.allowed_insecure_algorithms_ = element.child("AllowedInsecureAlgorithms").text().get();

	}
}

void XmlHostkeyStore::Save(bool silent)
{
	if (options_.get_int(OPTION_DEFAULT_KIOSKMODE) == 2) {
		return;
	}

	{
		CInterProcessMutex mutex(MUTEX_TRUSTEDHOSTKEYS);

		auto root = file_.GetElement();
		if (!root) {
			return;
		}

		for (auto server = root.child("Server"); server; server = root.child("Server")) {
			root.remove_child(server);
		}

		for (auto const& server : trusted_keys_) {
			auto server_elem = root.append_child("Server");
			server_elem.append_attribute("Host").set_value(server.first.first.c_str());
			server_elem.append_attribute("Port").set_value(server.first.second);

			entry const& e = server.second;
			for (auto const& blob : e.hostkeys_) {
				server_elem.append_child("Hostkey").text().set(fz::base64_encode(blob).c_str());
			}
			if (!e.allowed_insecure_algorithms_.empty()) {
				server_elem.append_child("AllowedInsecureAlgorithms").text().set(e.allowed_insecure_algorithms_.c_str());
			}
		}

		file_.Save();
	}
	auto err = file_.GetError();
	if (!err.empty() && !silent) {
		OnSaveError(err);
	}
}

namespace {
class PuttyHostkeyImporter final : XmlHostkeyStore
{
public:
	using XmlHostkeyStore::XmlHostkeyStore;
	void Import();

private:
	virtual bool AskUnknownHostkey(Site const&, fz::ssh::public_key const&, fz::ssh::algorithm_info const&, verification_flags, bool &) override { return false; }
	virtual void OnSaveError(std::wstring const&) override {}
};

void PuttyHostkeyImporter::Import()
{
	Load();

	std::vector<std::unique_ptr<fz::ssh::public_key>> keys;

#if FZ_WINDOWS
	auto key = fz::regkey(HKEY_CURRENT_USER, L"Software\\SimonTatham\\PuTTY\\SshHostKeys", true);
	for (auto const& n : key) {
		if (n.type != REG_SZ) {
			continue;
		}
		std::wstring v = key.value(n.name);
		if (!v.empty()) {
			auto pubkey = fz::ssh::load_public_key(fz::to_utf8(n.name + L" " + v), fz::get_null_logger());
			if (pubkey) {
				keys.emplace_back(std::move(pubkey));
			}
		}
	}
#else
	auto home = fz::to_string(GetHomeDir().GetPath());
	if (home.empty()) {
		return;
	}
	fz::buffer b;
	if (fz::read_file(home + ".putty/sshhostkeys", b, 10 * 1024 * 1024)) {
		keys = fz::ssh::load_public_keys(b.to_view(), fz::get_null_logger());
	}
#endif

	std::set<std::string> inserted;
	for (auto const& key : keys) {
		size_t pos = key->comment_.rfind(':');
		if (!pos || pos == std::string::npos) {
			continue;
		}

		auto port = fz::to_integral<uint16_t>(key->comment_.substr(pos + 1));
		if (!port) {
			continue;
		}
		auto host = fz::to_utf8(fz::str_tolower(fz::to_wstring_from_utf8(key->comment_.substr(0, pos))));
		if (host.empty()) {
			continue;
		}

		auto valid = [&](){
			for (auto const& c : host) {
				if (static_cast<uint8_t>(c) <= 32) {
					return false;
				}
			}
			return true;
		};
		if (!valid()) {
			continue;
		}

		auto & e = trusted_keys_[{host, port}];
		if (!e.hostkeys_.empty() && inserted.find(key->comment_) == inserted.end()) {
			continue;
		}

		e.hostkeys_.emplace_back(key->pubkey_blob());
		inserted.emplace(key->comment_);
	}

	Save(true);
}
}

void ImportPuttyHostkeys(COptionsBase & options)
{
	PuttyHostkeyImporter store(options);
	store.Import();
}
