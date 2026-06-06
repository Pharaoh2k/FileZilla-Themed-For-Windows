#include "xml_file.h"
#include "buildinfo.h"

#include "../include/version.h"

#include <libfilezilla/file.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/translate.hpp>

#include <cstring>

//TODO: move these file functions to libfilezilla
static bool rename_file(std::wstring const& source, std::wstring const& dest) {
#ifdef FZ_WINDOWS
	return MoveFileW(source.c_str(), dest.c_str()) != 0;
#else
	return std::rename(fz::to_native(source).c_str(), fz::to_native(dest).c_str()) == 0;
#endif
}

static bool file_exists(std::wstring const& file) {
	return fz::local_filesys::get_file_type(fz::to_native(file), false) != fz::local_filesys::unknown;
}

static bool copy_file(std::wstring const& src, std::wstring const& dest, bool overwrite = true, bool fsync = false)
{
	if (!overwrite && file_exists(dest)) {
		return false;
	}

	char buffer[8192];
	fz::file in(fz::to_native(src), fz::file::reading, fz::file::existing);
	fz::file out(fz::to_native(dest), fz::file::writing, fz::file::empty);

	if (!in.opened() || !out.opened()) {
		return false;
	}

	while (true) {
		fz::rwresult read = in.read2(buffer, sizeof(buffer));
		if (!read) {
			return false;
		}
		if (!read.value_) {
			break;
		}
		char const* p = buffer;
		while (read.value_) {
			fz::rwresult written = out.write2(p, read.value_);
			if (!written) {
				return false;
			}
			read.value_ -= written.value_;
			p += written.value_;
		}
	}

	if (fsync && !out.fsync()) {
		return false;
	}

	return true;
}

CXmlFile::CXmlFile(std::wstring const& fileName, std::string const& root)
{
	if (!root.empty()) {
		m_rootName = root;
	}
	SetFileName(fileName);
}

void CXmlFile::SetFileName(std::wstring const& name)
{
	m_fileName = name;
	m_modificationTime = fz::datetime();
}

pugi::xml_node CXmlFile::Load(bool overwriteInvalid)
{
	Close();
	m_error.clear();

	if (m_fileName.empty()) {
		return m_element;
	}

	std::wstring redirectedName = GetRedirectedName();

	GetXmlFile(redirectedName);
	if (!m_element) {
		std::wstring err = fz::sprintf(fztranslate("The file '%s' could not be loaded."), m_fileName);
		if (m_error.empty()) {
			err += L"\n" + fztranslate("Make sure the file can be accessed and is a well-formed XML document.");
		}
		else {
			err += L"\n" + m_error;
		}

		// Try the backup file
		GetXmlFile(redirectedName + L"~");
		if (!m_element) {
			// Loading backup failed.

			// Create new one if we are allowed to create empty file
			bool createEmpty = overwriteInvalid;

			// Also, if both original and backup file are empty, create new file.
			if (fz::local_filesys::get_size(fz::to_native(redirectedName)) <= 0 && fz::local_filesys::get_size(fz::to_native(redirectedName + L"~")) <= 0) {
				createEmpty = true;
			}

			if (createEmpty) {
				m_error.clear();
				CreateEmpty();
				m_modificationTime = fz::local_filesys::get_modification_time(fz::to_native(redirectedName));
				return m_element;
			}

			// File corrupt and no functional backup, give up.
			m_error = err;
			m_modificationTime.clear();
			return m_element;
		}

		// Loading the backup file succeeded, restore file
		if (!copy_file(redirectedName + L"~", redirectedName, true, true)) {
			// Could not restore backup, give up.
			Close();
			m_error = err;
			m_error += L"\n" + fz::sprintf(fztranslate("The valid backup file %s could not be restored"), redirectedName + L"~");
			m_modificationTime.clear();
			return m_element;
		}

		// We no longer need the backup
		fz::remove_file(fz::to_native(redirectedName + L"~"), false);
		m_error.clear();
	}

	m_modificationTime = fz::local_filesys::get_modification_time(fz::to_native(redirectedName));
	return m_element;
}

bool CXmlFile::Modified() const
{
	if (m_fileName.empty()) {
		return false;
	}

	if (m_modificationTime.empty()) {
		return true;
	}

	fz::datetime const modificationTime = fz::local_filesys::get_modification_time(fz::to_native(m_fileName));
	if (!modificationTime.empty() && modificationTime == m_modificationTime) {
		return false;
	}

	return true;
}

void CXmlFile::Close()
{
	m_element = pugi::xml_node();
	m_document.reset();
}

void CXmlFile::UpdateMetadata()
{
	if (!m_element || std::string(m_element.name()) != "FileZilla3") {
		return;
	}

	SetTextAttribute(m_element, "version", GetFileZillaVersion());

	std::string const platform =
#ifdef FZ_WINDOWS
		"windows";
#elif defined(FZ_MAC)
		"mac";
#else
		"*nix";
#endif
	SetTextAttributeUtf8(m_element, "platform", platform);
}

bool CXmlFile::Save(bool updateMetadata)
{
	m_error.clear();

	if (m_fileName.empty() || !m_document) {
		return false;
	}

	if (updateMetadata) {
		UpdateMetadata();
	}

	bool res = SaveXmlFile();
	m_modificationTime = fz::local_filesys::get_modification_time(fz::to_native(m_fileName));

	return res;
}

pugi::xml_node CXmlFile::CreateEmpty()
{
	Close();

	pugi::xml_node decl = m_document.append_child(pugi::node_declaration);
	decl.append_attribute("version") = "1.0";
	decl.append_attribute("encoding") = "UTF-8";

	m_element = m_document.append_child(m_rootName.c_str());
	return m_element;
}

// Opens the specified XML file if it exists or creates a new one otherwise.
// Returns false on error.
bool CXmlFile::GetXmlFile(std::wstring const& file)
{
	Close();

	if (fz::local_filesys::get_size(fz::to_native(file)) <= 0) {
		return false;
	}

	// File exists, open it
	fz::file f;
	fz::result r = f.open(fz::to_native(file), fz::file::reading);
	if (!r) {
		switch (r.error_) {
		case fz::result::noperm:
			m_error += fz::sprintf(fztranslate("No permission to open '%s'"), file);
			break;
		case fz::result::nofile:
			m_error += fz::sprintf(fztranslate("Not a file or does not exist: '%s'"), file);
			break;
		default:
			m_error += fz::sprintf(fztranslate("Error %d opening '%s'"), r.error_, file);
			break;
		}
		return false;
	}
	auto size = f.size();
	if (size < 0) {
		m_error += fz::sprintf(fztranslate("Could not get size of '%s'"), file);
		return false;
	}
	auto buffer = reinterpret_cast<uint8_t*>(pugi::get_memory_allocation_function()(static_cast<size_t>(size)));
	if (!buffer) {
		return false;
	}
	auto *p = buffer;
	auto to_read = size;
	while (to_read) {
		auto read = f.read2(p, to_read);

		if (!read || !read.value_) {
			m_error += fz::sprintf(fztranslate("Reading from '%s' failed."), file);
			pugi::get_memory_deallocation_function()(buffer);
			return false;
		}

		p += read.value_;
		to_read -= read.value_;
	}

	auto result = m_document.load_buffer_inplace_own(buffer, static_cast<size_t>(size));
	if (!result) {
		m_error += fz::sprintf(L"%s at offset %d.", result.description(), result.offset);
		return false;
	}

	m_element = m_document.child(m_rootName.c_str());
	if (!m_element) {
		if (m_document.first_child()) { // Beware: parse_declaration and parse_doctype can break this
			// Not created by FileZilla3
			Close();
			m_error = fztranslate("Unknown root element, the file does not appear to be generated by FileZilla.");
			return false;
		}
		m_element = m_document.append_child(m_rootName.c_str());
	}

	return true;
}

std::wstring CXmlFile::GetRedirectedName() const
{
	std::wstring redirectedName = m_fileName;
	bool isLink = false;
	if (fz::local_filesys::get_file_info(fz::to_native(redirectedName), isLink, 0, 0, 0) == fz::local_filesys::file) {
		if (isLink) {
			CLocalPath target(fz::to_wstring(fz::local_filesys::get_link_target(fz::to_native(redirectedName))));
			if (!target.empty()) {
				redirectedName = target.GetPath();
				redirectedName.pop_back();
			}
		}
	}
	return redirectedName;
}

bool CXmlFile::SaveXmlFile()
{
	bool exists = false;
	bool isLink = false;
	int flags = 0;

	std::wstring redirectedName = GetRedirectedName();
	if (fz::local_filesys::get_file_info(fz::to_native(redirectedName), isLink, 0, 0, &flags) == fz::local_filesys::file) {
#ifdef FZ_WINDOWS
		if (flags & FILE_ATTRIBUTE_HIDDEN) {
			SetFileAttributes(redirectedName.c_str(), flags & ~FILE_ATTRIBUTE_HIDDEN);
		}
#endif

		exists = true;
		if (!copy_file(redirectedName, redirectedName + L"~", true, true)) {
			m_error = fztranslate("Failed to create backup copy of xml file");
			return false;
		}
	}

	struct flushing_xml_writer final : public pugi::xml_writer
	{
	public:
		static bool save(pugi::xml_document const& document, std::wstring const& filename)
		{
			flushing_xml_writer writer(filename);
			if (!writer.file_.opened()) {
				return false;
			}
			document.save(writer);

			return writer.file_.opened() && writer.file_.fsync();
		}

	private:
		flushing_xml_writer(std::wstring const& filename)
			: file_(fz::to_native(filename), fz::file::writing, fz::file::empty)
		{
		}

		virtual void write(void const* data, size_t size) override {
			auto p = reinterpret_cast<uint8_t const*>(data);
			while (size && file_.opened()) {
				auto res = file_.write2(p, size);
				if (res) {
					size -= res.value_;
					p += res.value_;
				}
				else {
					file_.close();
				}
			}
		}

		fz::file file_;
	};

	bool success = flushing_xml_writer::save(m_document, redirectedName);
	if (!success) {
		fz::remove_file(fz::to_native(redirectedName), false);
		if (exists) {
			rename_file(redirectedName + L"~", redirectedName);
		}
		m_error = fztranslate("Failed to write xml file");
		return false;
	}

	if (exists) {
		fz::remove_file(fz::to_native(redirectedName + L"~"), false);
	}

	return true;
}

bool GetServer(pugi::xml_node node, Site & site)
{
	std::wstring host = GetTextElement(node, "Host");
	if (host.empty()) {
		return false;
	}

	unsigned int const port = node.child("Port").text().as_uint();
	if (port < 1 || port > 65535) {
		return false;
	}

	if (!site.server.SetHost(host, port)) {
		return false;
	}

	int const protocol = node.child("Protocol").text().as_int();
	if (protocol < 0 || protocol > ServerProtocol::MAX_VALUE) {
		return false;
	}
	site.server.SetProtocol(static_cast<ServerProtocol>(protocol));

	if (site.server.HasFeature(ProtocolFeature::ServerType)) {
		int type = GetTextElementInt(node, "Type");
		if (type < 0 || static_cast<unsigned int>(type) > SERVERTYPE_MAX) {
			return false;
		}
		if (type) {
			--type;
		}
		else {
			// See if we can recover type from a stored remote path
			std::wstring p = GetTextElement(node, "RemoteDir");
			if (p.empty()) {
				p = GetTextElement(node, "RemotePath");
			}
			size_t pos = p.find(' ');
			if (pos != std::string::npos) {
				auto v = fz::to_integral<unsigned int>(p.substr(0, pos));
				if (v > 0 && v <= SERVERTYPE_MAX) {
					type = --v;
				}
			}
		}
		site.server.SetType(static_cast<ServerType>(type));
	}

	int logonType = GetTextElementInt(node, "Logontype");
	if (logonType < 0 || logonType >= static_cast<int>(LogonType::count)) {
		return false;
	}

	site.SetLogonType(static_cast<LogonType>(logonType));

	if (site.credentials.logonType_ != LogonType::anonymous) {
		std::wstring user;

		bool const has_user = ProtocolHasUser(site.server.GetProtocol());
		if (has_user) {
			user = GetTextElement(node, "User");
			if (user.empty() && site.credentials.logonType_ != LogonType::interactive && site.credentials.logonType_ != LogonType::ask) {
				return false;
			}
		}

		std::wstring pass, key;
		if (site.credentials.logonType_ == LogonType::normal || site.credentials.logonType_ == LogonType::account) {
			auto passElement = node.child("Pass");
			if (passElement) {
				std::wstring encoding = GetTextAttribute(passElement, "encoding");

				if (encoding == L"base64") {
					std::string decoded = fz::base64_decode_s(passElement.child_value());
					pass = fz::to_wstring_from_utf8(decoded);
				}
				else if (encoding == L"crypt") {
					pass = fz::to_wstring_from_utf8(passElement.child_value());
					site.credentials.encrypted_ = fz::public_key::from_base64(passElement.attribute("pubkey").value());
					if (!site.credentials.encrypted_) {
						pass.clear();
						site.SetLogonType(LogonType::ask);
					}
				}
				else if (!encoding.empty()) {
					site.SetLogonType(LogonType::ask);
				}
				else {
					pass = GetTextElement(passElement);
				}
			}

			if (pass.empty() && !has_user) {
				return false;
			}
		}
		else if (site.credentials.logonType_ == LogonType::key) {
			if (site.server.GetProtocol() == SFTP) {
				if (user.empty()) {
					return false;
				}
				// password should be empty if we're using a key file
				pass.clear();
			}

			key = GetTextElement(node, "Keyfile");

			site.credentials.keyFile_ = key;
		}

		site.SetUser(user);
		site.credentials.SetPass(pass);

		site.credentials.account_ = GetTextElement(node, "Account");
	}

	int timezoneOffset = GetTextElementInt(node, "TimezoneOffset");
	if (!site.server.SetTimezoneOffset(timezoneOffset)) {
		return false;
	}

	std::string_view pasvMode = node.child_value("PasvMode");
	if (pasvMode == "MODE_PASSIVE") {
		site.server.SetPasvMode(MODE_PASSIVE);
	}
	else if (pasvMode == "MODE_ACTIVE") {
		site.server.SetPasvMode(MODE_ACTIVE);
	}
	else {
		site.server.SetPasvMode(MODE_DEFAULT);
	}

	auto maximumMultipleConnections = GetTextElementInt(node, "MaximumMultipleConnections");
	site.connection_limit_ = static_cast<unsigned int>(maximumMultipleConnections);

	std::string_view encodingType = node.child_value("EncodingType");
	if (encodingType == "Custom") {
		std::wstring customEncoding = GetTextElement(node, "CustomEncoding");
		if (customEncoding.empty()) {
			return false;
		}
		if (!site.server.SetEncodingType(ENCODING_CUSTOM, customEncoding)) {
			return false;
		}
	}
	else {
		site.server.SetEncodingType(ENCODING_UTF8);
	}

	if (CServer::ProtocolHasFeature(site.server.GetProtocol(), ProtocolFeature::PostLoginCommands)) {
		std::vector<std::wstring> postLoginCommands;
		auto element = node.child("PostLoginCommands");
		if (element) {
			for (auto commandElement = element.child("Command"); commandElement; commandElement = commandElement.next_sibling("Command")) {
				std::wstring command = fz::to_wstring_from_utf8(commandElement.child_value());
				if (!command.empty()) {
					postLoginCommands.emplace_back(std::move(command));
				}
			}
		}
		if (!site.server.SetPostLoginCommands(postLoginCommands)) {
			return false;
		}
	}

	site.server.SetBypassProxy(GetTextElementInt(node, "BypassProxy", false) == 1);
	site.SetName(GetTextElement_Trimmed(node, "Name"));

	if (site.GetName().empty()) {
		site.SetName(GetTextElement_Trimmed(node));
	}

	for (auto parameter = node.child("Parameter"); parameter; parameter = parameter.next_sibling("Parameter")) {
		site.server.SetExtraParameter(parameter.attribute("Name").value(), GetTextElement(parameter));
	}

	return true;
}

namespace {
struct xml_memory_writer : pugi::xml_writer
{
	size_t written{};
	char* buffer{};
	size_t remaining{};

	virtual void write(void const* data, size_t size)
	{
		if (buffer && size <= remaining) {
			memcpy(buffer, data, size);
			buffer += size;
			remaining -= size;
		}
		written += size;
	}
};
}

size_t CXmlFile::GetRawDataLength()
{
	if (!m_document) {
		return 0;
	}

	xml_memory_writer writer;
	m_document.save(writer);
	return writer.written;
}

void CXmlFile::GetRawDataHere(char* p, size_t size) // p has to big enough to hold at least GetRawDataLength() bytes
{
	if (size) {
		memset(p, 0, size);
	}
	xml_memory_writer writer;
	writer.buffer = p;
	writer.remaining = size;
	m_document.save(writer);
}

bool CXmlFile::ParseData(uint8_t const* data, size_t len)
{
	Close();
	m_document.load_buffer(data, len);
	m_element = m_document.child(m_rootName.c_str());
	if (!m_element) {
		Close();
	}
	return !!m_element;
}

bool CXmlFile::IsFromFutureVersion() const
{
	auto const ownVer = GetFileZillaVersion();
	if (!m_element || ownVer.empty()) {
		return false;
	}
	std::wstring const version = GetTextAttribute(m_element, "version");
	return ConvertToVersionNumber(ownVer.c_str()) < ConvertToVersionNumber(version.c_str());
}

int64_t CXmlFile::GetVersion() const
{
	if (!m_element) {
		return -1;
	}

	auto const version = GetTextAttribute(m_element, "version");
	return ConvertToVersionNumber(version.c_str());
}
