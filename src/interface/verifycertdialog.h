#ifndef FILEZILLA_INTERFACE_VERIFYCERTDIALOG_HEADER
#define FILEZILLA_INTERFACE_VERIFYCERTDIALOG_HEADER

#include "dialogex.h"
#include "xmlfunctions.h"

#include "../commonui/xml_cert_store.h"

#include <list>
#include <set>

class CertStore final : public xml_cert_store
{
public:
	CertStore(bool kiosk_mode);

private:
	void SavingFileFailed(std::wstring const& file, std::wstring const& msg) override;
	bool AllowedToSave() const { return !kiosk_mode_; }

	bool const kiosk_mode_{};
};

class COptionsBase;
class TimeFormatter;

class CVerifyCertDialog final : protected wxDialogEx
{
public:
	static void ShowVerificationDialog(cert_store & certStore, CCertificateNotification& notification, COptionsBase & options, TimeFormatter & time_formatter);

	static void DisplayCertificate(fz::tls_session_info const& info, COptionsBase & options, TimeFormatter & time_formatter);

private:
	bool CreateVerificationDialog(fz::tls_session_info const& info, COptionsBase & options, bool displayOnly);

	CVerifyCertDialog(TimeFormatter & time_formatter);
	~CVerifyCertDialog();

	void AddAlgorithm(wxWindow* parent, wxGridBagSizer* sizer, std::string const& name, bool insecure);

	bool DisplayCert(fz::x509_certificate const& cert);

	void ParseDN(wxWindow* parent, std::wstring const& dn, wxSizer* pSizer);
	void ParseDN_by_prefix(wxWindow* parent, std::vector<std::pair<std::wstring, std::wstring>>& tokens, std::wstring const& prefix, wxString const& name, wxSizer* pSizer);

	int line_height_{};

	void OnCertificateChoice(wxCommandEvent const& event);

	bool warning_{};
	bool sanTrustAllowed_{};

	struct impl;
	std::unique_ptr<impl> impl_;
};

void ConfirmInsecureConection(wxWindow* parent, cert_store & certStore, CInsecureConnectionNotification & notification);

void ConfirmFtpTlsNoResumptionNotification(wxWindow* parent, cert_store & certStore, FtpTlsNoResumptionNotification & notification);

#endif
