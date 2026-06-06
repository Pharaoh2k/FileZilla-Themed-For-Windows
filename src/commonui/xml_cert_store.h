#ifndef FILEZILLA_COMMONUI_XML_CERT_STORE_HEADER
#define FILEZILLA_COMMONUI_XML_CERT_STORE_HEADER

#include "cert_store.h"
#include "xml_file.h"
#include "visibility.h"

#include <list>
#include <set>

class FZCUI_PUBLIC_SYMBOL xml_cert_store : public cert_store
{
public:
	xml_cert_store(std::wstring const& file);

protected:
	virtual bool DoSetTrusted(t_certData const& cert, fz::x509_certificate const&) override;
	virtual bool DoSetInsecure(std::string const& host, unsigned int port) override;
	virtual bool DoSetSessionResumptionSupport(std::string const& host, unsigned short port, bool secure) override;
	virtual void LoadTrustedCerts() override;

	void SetInsecureToXml(pugi::xml_node& root, std::string const& host, unsigned int port);
	void SetTrustedInXml(pugi::xml_node& root, t_certData const& cert, fz::x509_certificate const& certificate);
	void SetSessionResumptionSupportInXml(pugi::xml_node& root, std::string const& host, unsigned short port, bool secure);

	virtual void SavingFileFailed(std::wstring const& /*file*/, std::wstring const& /*msg*/) {}
	virtual bool AllowedToSave() const { return true; }

private:
	CXmlFile m_xmlFile;
};

#endif
