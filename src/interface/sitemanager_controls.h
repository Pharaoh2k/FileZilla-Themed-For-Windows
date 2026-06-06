#ifndef FILEZILLA_INTERFACE_SITEMANAGER_CONTROLS_HEADER
#define FILEZILLA_INTERFACE_SITEMANAGER_CONTROLS_HEADER

struct DialogLayout;
class Site;
enum class LogonType;
class wxWindow;

class SiteControls
{
public:
	SiteControls(wxWindow & parent)
	    : parent_(parent)
	{}

	virtual ~SiteControls() = default;
	virtual void SetSite(Site const& site, bool predefined) = 0;

	virtual void SetControlVisibility(ServerProtocol /*protocol*/, LogonType /*type*/) {}

	virtual bool UpdateSite(Site & site, bool silent) = 0;
	virtual void SetControlState(bool /*predefined*/) {}

	virtual void UpdateWidth(int /*width*/) {}

	wxWindow & parent_;

	ServerProtocol protocol_{UNKNOWN};
	LogonType logonType_{};
};

class COptionsBase;
class GeneralSiteControls final : public SiteControls
{
public:
	GeneralSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer, COptionsBase & options, std::function<void(ServerProtocol protocol, LogonType logon_type)> const& changeHandler = nullptr);
	~GeneralSiteControls();

	virtual void SetControlVisibility(ServerProtocol protocol, LogonType) override;
	virtual void SetSite(Site const& site, bool predefined) override;
	virtual void SetControlState(bool predefined) override;

	virtual bool UpdateSite(Site & site, bool silent) override;

	virtual void UpdateWidth(int width) override;

private:
	void SetProtocol(ServerProtocol protocol);
	ServerProtocol GetProtocol() const;

	void SetLogonType(LogonType t);
	LogonType GetLogonType() const;

	void UpdateHostFromDefaults(ServerProtocol const newProtocol);

	bool ValidateGoogleAccountKeyFile(std::wstring_view filename);

	struct impl;
	std::unique_ptr<impl> impl_;

	std::map<ServerProtocol, int> mainProtocolListIndex_;
	typedef std::tuple<std::string, wxStaticText*, wxTextCtrl*> Parameter;
	std::vector<Parameter> extraParameters_[ParameterSection::section_count];
};

class AdvancedSiteControls final : public SiteControls
{
public:
	AdvancedSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer);
	~AdvancedSiteControls();

	virtual void SetSite(Site const& site, bool predefined) override;
	virtual void SetControlVisibility(ServerProtocol protocol, LogonType) override;
	virtual bool UpdateSite(Site & site, bool silent) override;

	struct impl;
	std::unique_ptr<impl> impl_;
};

class TransferSettingsSiteControls final : public SiteControls
{
public:
	TransferSettingsSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer);
	~TransferSettingsSiteControls();

	virtual void SetSite(Site const& site, bool predefined) override;
	virtual void SetControlVisibility(ServerProtocol protocol, LogonType) override;
	virtual bool UpdateSite(Site & site, bool silent) override;

private:
	struct impl;
	std::unique_ptr<impl> impl_;
};

class CharsetSiteControls final : public SiteControls
{
public:
	CharsetSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer);
	~CharsetSiteControls();

	virtual void SetSite(Site const& site, bool predefined) override;
	virtual bool UpdateSite(Site & site, bool silent) override;

	struct impl;
	std::unique_ptr<impl> impl_;
};

class SftpSiteControls final : public SiteControls
{
public:
	SftpSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer);
	~SftpSiteControls();

	virtual void SetSite(Site const& site, bool predefined) override;
	virtual void SetControlVisibility(ServerProtocol protocol, LogonType) override;
	virtual bool UpdateSite(Site & site, bool silent) override;

private:
	struct impl;
	std::unique_ptr<impl> impl_;
};

class S3SiteControls final : public SiteControls
{
public:
	S3SiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer);

	virtual void SetSite(Site const& site, bool predefined) override;
	virtual void SetControlVisibility(ServerProtocol protocol, LogonType) override;
	virtual bool UpdateSite(Site & site, bool silent) override;

private:
	virtual void SetControlState(bool predefined) override;

	wxFlexGridSizer* sts_sizer_{};
	wxFlexGridSizer* sso_sizer_{};
};

class DropboxSiteControls final : public SiteControls
{
public:
	DropboxSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer);

	virtual void SetSite(Site const& site, bool predefined) override;
	virtual bool UpdateSite(Site & site, bool silent) override;
};

class SwiftSiteControls final : public SiteControls
{
public:
	SwiftSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer);

	virtual void SetSite(Site const& site, bool predefined) override;
	virtual bool UpdateSite(Site & site, bool silent) override;

private:
	virtual void SetControlState(bool predefined) override;
};

#endif
