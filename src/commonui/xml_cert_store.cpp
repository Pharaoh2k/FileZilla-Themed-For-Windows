#include "xml_cert_store.h"

#include "ipcmutex.h"

xml_cert_store::xml_cert_store(std::wstring const& file)
	: m_xmlFile(file)
{
}

void xml_cert_store::LoadTrustedCerts()
{
	CReentrantInterProcessMutexLocker mutex(MUTEX_TRUSTEDCERTS);
	if (!m_xmlFile.Modified()) {
		return;
	}

	auto root = m_xmlFile.Load();
	if (!root) {
		return;
	}

	data_[persistent] = data{};

	pugi::xml_node element;

	bool modified = false;
	if ((element = root.child("TrustedCerts"))) {

		auto const processEntry = [&](pugi::xml_node const& cert)
		{
			std::wstring value = GetTextElement(cert, "Data");

			t_certData data;
			data.data = fz::hex_decode(value);
			if (data.data.empty()) {
				return false;
			}

			data.host = cert.child_value("Host");
			data.port = cert.child("Port").text().as_uint();
			if (data.host.empty() || data.port < 1 || data.port > 65535) {
				return false;
			}

			fz::datetime const now = fz::datetime::now();
			int64_t activationTime = GetTextElementInt(cert, "ActivationTime", 0);
			if (activationTime == 0 || activationTime > now.get_time_t()) {
				return false;
			}

			int64_t expirationTime = GetTextElementInt(cert, "ExpirationTime", 0);
			if (expirationTime == 0 || expirationTime < now.get_time_t()) {
				return false;
			}

			data.trustSans = GetTextElementBool(cert, "TrustSANs");

			// Weed out duplicates
			if (IsTrusted(data.host, data.port, data.data, true, false)) {
				return false;
			}

			data_[persistent].trusted_certs_.emplace_back(std::move(data));

			return true;
		};

		auto cert = element.child("Certificate");
		while (cert) {

			auto nextCert = cert.next_sibling("Certificate");
			if (!processEntry(cert)) {
				modified = true;
				element.remove_child(cert);
			}
			cert = nextCert;
		}
	}

	if ((element = root.child("InsecureHosts"))) {

		auto const processEntry = [&](pugi::xml_node const& node)
		{
			std::string host = node.child_value();
			unsigned int port = node.attribute("Port").as_uint();
			if (host.empty() || port < 1 || port > 65535) {
				return false;
			}

			for (auto const& cert : data_[persistent].trusted_certs_) {
				// A host can't be both trusted and insecure
				if (cert.host == host && cert.port == port) {
					return false;
				}
			}

			data_[persistent].insecure_hosts_.emplace(std::make_tuple(host, port));

			return true;
		};

		auto host = element.child("Host");
		while (host) {

			auto nextHost = host.next_sibling("Host");
			if (!processEntry(host)) {
				modified = true;
				element.remove_child(host);
			}
			host = nextHost;
		}
	}

	if ((element = root.child("FtpSessionResumption"))) {

		auto const processEntry = [&](pugi::xml_node const& node)
		{
			std::string host = node.attribute("Host").value();
			unsigned int port = node.attribute("Port").as_uint();
			if (host.empty() || port < 1 || port > 65535) {
				return false;
			}

			data_[persistent].ftp_tls_resumption_support_.emplace(std::make_tuple(host, port), node.text().as_bool());

			return true;
		};

		auto host = element.child("Entry");
		while (host) {
			auto nextHost = host.next_sibling("Entry");
			if (!processEntry(host)) {
				modified = true;
				element.remove_child(host);
			}
			host = nextHost;
		}
	}

	if (modified) {
		m_xmlFile.Save();
	}
}

void xml_cert_store::SetInsecureToXml(pugi::xml_node& root, std::string const& host, unsigned int port)
{
	auto certs = root.child("TrustedCerts");

	// Purge certificates for this host
	auto const processEntry = [&host, &port](pugi::xml_node const& cert)
	{
		return host != cert.child_value("Host") || port != GetTextElementInt(cert, "Port");
	};

	auto cert = certs.child("Certificate");
	while (cert) {
		auto nextCert = cert.next_sibling("Certificate");
		if (!processEntry(cert)) {
			certs.remove_child(cert);
		}
		cert = nextCert;
	}

	auto insecureHosts = root.child("InsecureHosts");
	if (!insecureHosts) {
		insecureHosts = root.append_child("InsecureHosts");
	}

	// Remember host as insecure
	auto xhost = insecureHosts.append_child("Host");
	xhost.append_attribute("Port").set_value(port);
	xhost.text().set(fz::to_utf8(host).c_str());
}

bool xml_cert_store::DoSetInsecure(std::string const& host, unsigned int port)
{
	CReentrantInterProcessMutexLocker mutex(MUTEX_TRUSTEDCERTS);

	if (!cert_store::DoSetInsecure(host, port)) {
		return false;
	}

	if (AllowedToSave()) {
		auto root = m_xmlFile.GetElement();
		if (root) {
			SetInsecureToXml(root, host, port);
			if (!m_xmlFile.Save()) {
				SavingFileFailed(m_xmlFile.GetFileName(), m_xmlFile.GetError());
			}
		}
	}

	return true;
}


void xml_cert_store::SetTrustedInXml(pugi::xml_node& root, t_certData const& cert, fz::x509_certificate const& certificate)
{
	auto certs = root.child("TrustedCerts");
	if (!certs) {
		certs = root.append_child("TrustedCerts");
	}

	auto xCert = certs.append_child("Certificate");
	AddTextElementUtf8(xCert, "Data", fz::hex_encode<std::string>(cert.data));
	AddTextElement(xCert, "ActivationTime", static_cast<int64_t>(certificate.get_activation_time().get_time_t()));
	AddTextElement(xCert, "ExpirationTime", static_cast<int64_t>(certificate.get_expiration_time().get_time_t()));
	AddTextElement(xCert, "Host", cert.host);
	AddTextElement(xCert, "Port", cert.port);
	AddTextElement(xCert, "TrustSANs", cert.trustSans ? L"1" : L"0");

	// Purge insecure host
	auto const processEntry = [&cert](pugi::xml_node const& xhost)
	{
		return fz::to_wstring(cert.host) != GetTextElement(xhost) || cert.port != xhost.attribute("Port").as_uint();
	};

	auto insecureHosts = root.child("InsecureHosts");
	auto xhost = insecureHosts.child("Host");
	while (xhost) {

		auto nextHost = xhost.next_sibling("Host");
		if (!processEntry(xhost)) {
			insecureHosts.remove_child(xhost);
		}
		xhost = nextHost;
	}
}

bool xml_cert_store::DoSetTrusted(t_certData const& cert, fz::x509_certificate const& certificate)
{
	CReentrantInterProcessMutexLocker mutex(MUTEX_TRUSTEDCERTS);

	if (!cert_store::DoSetTrusted(cert, certificate)) {
		return false;
	}

	if (AllowedToSave()) {
		auto root = m_xmlFile.GetElement();
		if (root) {
			SetTrustedInXml(root, cert, certificate);
			if (!m_xmlFile.Save()) {
				SavingFileFailed(m_xmlFile.GetFileName(), m_xmlFile.GetError());
			}
		}
	}

	return true;
}

void xml_cert_store::SetSessionResumptionSupportInXml(pugi::xml_node& root, std::string const& host, unsigned short port, bool secure)
{
	auto xhosts = root.child("FtpSessionResumption");
	if (!xhosts) {
		xhosts = root.append_child("FtpSessionResumption");
	}

	// Remember host as insecure
	auto xhost = xhosts.child("Entry");
	for (; xhost; xhost = xhost.next_sibling("Entry")) {
		if (xhost.attribute("Host").value() == host && xhost.attribute("Port").as_uint() == port) {
			break;
		}
	}
	if (!xhost) {
		xhost = xhosts.append_child("Entry");
		xhost.append_attribute("Host").set_value(host.c_str());
		xhost.append_attribute("Port").set_value(port);		
	}
	xhost.text().set(secure);
}

bool xml_cert_store::DoSetSessionResumptionSupport(std::string const& host, unsigned short port, bool secure)
{
	CReentrantInterProcessMutexLocker mutex(MUTEX_TRUSTEDCERTS);

	if (!cert_store::DoSetSessionResumptionSupport(host, port, secure)) {
		return false;
	}

	if (AllowedToSave()) {
		auto root = m_xmlFile.GetElement();
		if (root) {
			SetSessionResumptionSupportInXml(root, host, port, secure);
			if (!m_xmlFile.Save()) {
				SavingFileFailed(m_xmlFile.GetFileName(), m_xmlFile.GetError());
			}
		}
	}

	return true;
}
