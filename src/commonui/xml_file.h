#ifndef FILEZILLA_COMMONUI_XML_FILE_HEADER
#define FILEZILLA_COMMONUI_XML_FILE_HEADER

#include "site.h"
#include "../include/xmlutils.h"
#include "visibility.h"

class FZCUI_PUBLIC_SYMBOL CXmlFile final
{
public:
	CXmlFile() = default;
	explicit CXmlFile(std::wstring const& fileName, std::string const& root = std::string());

	CXmlFile(CXmlFile const&) = delete;
	CXmlFile& operator=(CXmlFile const&) = delete;

	pugi::xml_node CreateEmpty();

	std::wstring GetFileName() const { return m_fileName; }
	void SetFileName(std::wstring const& name);

	bool HasFileName() const { return !m_fileName.empty(); }

	// Sets error description on failure
	pugi::xml_node Load(bool overwriteInvalid = false);

	std::wstring GetError() const { return m_error; }
	size_t GetRawDataLength();
	void GetRawDataHere(char* p, size_t size); // p has to big enough to hold at least GetRawDataLength() bytes

	bool ParseData(uint8_t const* data, size_t len);

	void Close();

	pugi::xml_node GetElement() { return m_element; }
	pugi::xml_node const GetElement() const { return m_element; }

	bool Modified() const;

	bool Save(bool updateMetadata = true);

	bool IsFromFutureVersion() const;
	int64_t GetVersion() const;

protected:
	std::wstring GetRedirectedName() const;

	// Opens the specified XML file if it exists or creates a new one otherwise.
	// Returns 0 on error.
	bool GetXmlFile(std::wstring const& file);

	// Sets version and platform in root element
	void UpdateMetadata();

	// Save the XML document to the given file
	bool SaveXmlFile();

	fz::datetime m_modificationTime;
	std::wstring m_fileName;
	pugi::xml_document m_document;
	pugi::xml_node m_element;

	std::wstring m_error;

	std::string m_rootName{"FileZilla3"};
};

// Function to retrieve CServer objects from the XML
bool FZCUI_PUBLIC_SYMBOL GetServer(pugi::xml_node node, Site& site);

#endif
