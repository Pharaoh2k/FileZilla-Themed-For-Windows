#include "filezilla.h"
#include "sitemanager_controls.h"

#include "dialogex.h"
#include "encoding_converter.h"
#include "Options.h"
#if USE_MAC_SANDBOX
#include "osx_sandbox_userdirs.h"
#endif
#include "sitemanager.h"
#include "textctrlex.h"
#include "xrc_helper.h"
#include "wxext/spinctrlex.h"
#if ENABLE_STORJ
#include "storj_key_interface.h"
#endif

#include "../include/s3sse.h"

#include <fzssh/privkey.hpp>

#include <libfilezilla/translate.hpp>

#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/gbsizer.h>
#include <wx/hyperlink.h>
#include <wx/statline.h>

#include <array>
#include <map>

namespace {
struct ProtocolGroup {
	std::wstring name;
	std::vector<std::pair<ServerProtocol, std::wstring>> protocols;
};

std::array<ProtocolGroup, 2> const& protocolGroups()
{
	static auto const groups = std::array<ProtocolGroup, 2>{{
		{
			fztranslate("FTP - File Transfer Protocol"), {
				{ FTP, fztranslate("Use explicit FTP over TLS if available") },
				{ FTPES, fztranslate("Require explicit FTP over TLS") },
				{ FTPS, fztranslate("Require implicit FTP over TLS") },
				{ INSECURE_FTP, fztranslate("Only use plain FTP (insecure)") }
			}
		},
		{
			fztranslate("WebDAV"), {
				{ WEBDAV, fztranslate("Using secure HTTPS") },
				{ INSECURE_WEBDAV, fztranslate("Using insecure HTTP") }
			}
		}
	}};
	return groups;
}

std::pair<std::array<ProtocolGroup, 2>::const_iterator, std::vector<std::pair<ServerProtocol, std::wstring>>::const_iterator> findGroup(ServerProtocol protocol)
{
	auto const& groups = protocolGroups();
	for (auto group = groups.cbegin(); group != groups.cend(); ++group) {
		for (auto entry = group->protocols.cbegin(); entry != group->protocols.cend(); ++entry) {
			if (entry->first == protocol) {
				return std::make_pair(group, entry);
			}
		}
	}

	return std::make_pair(groups.cend(), std::vector<std::pair<ServerProtocol, std::wstring>>::const_iterator());
}
}

struct GeneralSiteControls::impl final
{
	impl(COptionsBase & options, std::function<void(ServerProtocol protocol, LogonType logon_type)> const& changeHandler)
		: options_(options)
		, changeHandler_(changeHandler)
	{
	}

	COptionsBase& options_;
	std::function<void(ServerProtocol protocol, LogonType logon_type)> const changeHandler_;

	wxChoice* protocol_choice_{};
	wxStaticText* host_desc_{};
	wxTextCtrlEx* host_{};
	wxStaticText* port_desc_{};
	wxTextCtrlEx* port_{};
	wxStaticText* encryption_desc_{};
	wxChoice* encryption_{};
	wxStaticText* jurisdiction_desc_{};
	wxChoice* jurisdiction_{};
	wxHyperlinkCtrl* storj_docs_{};
	wxHyperlinkCtrl* storj_signup_{};
	wxStaticText* extra_host_desc_{};
	wxTextCtrlEx* extra_host_{};
	wxChoice* logonType_;
	wxStaticText* user_desc_{};
	wxTextCtrlEx* user_{};
	wxStaticText* extra_user_desc_{};
	wxTextCtrlEx* extra_user_{};
	wxStaticText* pass_desc_{};
	wxTextCtrlEx* pass_{};
	wxStaticText* account_desc_{};
	wxTextCtrlEx* account_{};
	wxStaticText* keyfile_desc_{};
	wxTextCtrlEx* keyfile_{};
	wxButton* keyfile_browse_{};
	wxStaticText* extra_credentials_desc_{};
	wxTextCtrlEx* extra_credentials_{};
	wxStaticText* extra_extra_desc_{};
	wxTextCtrlEx* extra_extra_{};
};

std::vector<fz::ssh::private_key_info> LoadKeyfile(std::wstring keyFile, bool silent);

GeneralSiteControls::GeneralSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer, COptionsBase & options, std::function<void(ServerProtocol protocol, LogonType logon_type)> const& changeHandler)
    : SiteControls(parent)
	, impl_(std::make_unique<impl>(options, changeHandler))
{
	if (!sizer.IsColGrowable(0)) {
		sizer.AddGrowableCol(0);
	}

	auto * bag = lay.createGridBag(2);
	bag->AddGrowableCol(1);
	sizer.Add(bag, 0, wxGROW);

	bag->SetEmptyCellSize(wxSize(-bag->GetHGap(), -bag->GetVGap()));

	lay.gbNewRow(bag);
	lay.gbAdd(bag, new wxStaticText(&parent, nullID, _("Pro&tocol:")), lay.valign);
	impl_->protocol_choice_ = new wxChoice(&parent, nullID);
	lay.gbAdd(bag, impl_->protocol_choice_, lay.valigng);

	lay.gbNewRow(bag);
	impl_->host_desc_ = new wxStaticText(&parent, nullID, _("&Host:"));
	lay.gbAdd(bag, impl_->host_desc_, lay.valign);
	auto * row = lay.createFlex(0, 1);
	row->AddGrowableCol(0);
	lay.gbAdd(bag, row, lay.valigng);
	impl_->host_ = new wxTextCtrlEx(&parent, nullID);
	row->Add(impl_->host_, lay.valigng);
	impl_->port_desc_ = new wxStaticText(&parent, nullID, _("&Port:"));
	row->Add(impl_->port_desc_, lay.valign);
	impl_->port_ = new wxTextCtrlEx(&parent, nullID, wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(27), -1));
	impl_->port_->SetMaxLength(5);
	row->Add(impl_->port_, lay.valign);

	lay.gbNewRow(bag);
	impl_->jurisdiction_desc_ = new wxStaticText(&parent, nullID, _("&Jurisdiction:"));
	lay.gbAdd(bag, impl_->jurisdiction_desc_, lay.valign);

	auto jrow = lay.createFlex(0, 1);
	jrow->AddGrowableCol(0);
	lay.gbAdd(bag, jrow, lay.valigng);

	impl_->jurisdiction_ = new wxChoice(&parent, nullID);
	jrow->Add(impl_->jurisdiction_, lay.valigng);

	lay.gbNewRow(bag);
	impl_->encryption_desc_ = new wxStaticText(&parent, nullID, _("&Encryption:"));
	lay.gbAdd(bag, impl_->encryption_desc_, lay.valign);
	auto brow = new wxBoxSizer(wxHORIZONTAL);
	lay.gbAdd(bag, brow, lay.valigng);
	impl_->encryption_ = new wxChoice(&parent, nullID);
	brow->Add(impl_->encryption_, 1);
	impl_->storj_docs_ = new wxHyperlinkCtrl(&parent, nullID, _("Docs"), L"https://docs.storj.io/how-tos/set-up-filezilla-for-decentralized-file-transfer");
	brow->Add(impl_->storj_docs_, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, lay.dlgUnits(2))->Show(false);
	impl_->storj_signup_ = new wxHyperlinkCtrl(&parent, nullID, _("Signup"), L"https://storj.io/signup/?partner=filezilla");
	brow->Add(impl_->storj_signup_, lay.valign)->Show(false);
	brow->AddSpacer(0);

	lay.gbNewRow(bag);
	impl_->extra_host_desc_ = new wxStaticText(&parent, nullID, wxString());
	lay.gbAdd(bag, impl_->extra_host_desc_, lay.valign)->Show(false);
	impl_->extra_host_ = new wxTextCtrlEx(&parent, nullID);
	lay.gbAdd(bag, impl_->extra_host_, lay.valigng)->Show(false);

	lay.gbAddRow(bag, new wxStaticLine(&parent), lay.grow);

	lay.gbNewRow(bag);
	lay.gbAdd(bag, new wxStaticText(&parent, nullID, _("&Logon Type:")), lay.valign);
	impl_->logonType_ = new wxChoice(&parent, nullID);
	lay.gbAdd(bag, impl_->logonType_, lay.valigng);

	lay.gbNewRow(bag);
	impl_->user_desc_ = new wxStaticText(&parent, nullID, _("&User:"));
	lay.gbAdd(bag, impl_->user_desc_, lay.valign);
	impl_->user_ = new wxTextCtrlEx(&parent, nullID);
	lay.gbAdd(bag, impl_->user_, lay.valigng);

	lay.gbNewRow(bag);
	impl_->extra_user_desc_ = new wxStaticText(&parent, nullID, L"");
	lay.gbAdd(bag, impl_->extra_user_desc_, lay.valign)->Show(false);
	impl_->extra_user_ = new wxTextCtrlEx(&parent, nullID);
	lay.gbAdd(bag, impl_->extra_user_, lay.valigng)->Show(false);

	lay.gbNewRow(bag);
	impl_->pass_desc_ = new wxStaticText(&parent, nullID, _("Pass&word:"));
	lay.gbAdd(bag, impl_->pass_desc_, lay.valign);
	impl_->pass_ = new wxTextCtrlEx(&parent, nullID, L"", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	lay.gbAdd(bag, impl_->pass_, lay.valigng);

	lay.gbNewRow(bag);
	impl_->account_desc_ = new wxStaticText(&parent, nullID, _("&Account:"));
	lay.gbAdd(bag, impl_->account_desc_, lay.valign);
	impl_->account_ = new wxTextCtrlEx(&parent, nullID);
	lay.gbAdd(bag, impl_->account_, lay.valigng);

	lay.gbNewRow(bag);
	impl_->keyfile_desc_ = new wxStaticText(&parent, nullID, _("&Key file:"));
	lay.gbAdd(bag, impl_->keyfile_desc_, lay.valign)->Show(false);
	row = lay.createFlex(0, 1);
	row->AddGrowableCol(0);
	lay.gbAdd(bag, row, lay.valigng);
	impl_->keyfile_ = new wxTextCtrlEx(&parent, nullID);
	row->Add(impl_->keyfile_, lay.valigng)->Show(false);
	impl_->keyfile_browse_ = new wxButton(&parent, nullID, _("Browse..."));
	row->Add(impl_->keyfile_browse_, lay.valign)->Show(false);

	lay.gbNewRow(bag);
	impl_->extra_credentials_desc_ = new wxStaticText(&parent, nullID, L"");
	lay.gbAdd(bag, impl_->extra_credentials_desc_, lay.valign)->Show(false);
	impl_->extra_credentials_ = new wxTextCtrlEx(&parent, nullID, L"", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	lay.gbAdd(bag, impl_->extra_credentials_, lay.valigng)->Show(false);

	lay.gbNewRow(bag);
	impl_->extra_extra_desc_ = new wxStaticText(&parent, nullID, L"");
	lay.gbAdd(bag, impl_->extra_extra_desc_, lay.valign)->Show(false);
	impl_->extra_extra_ = new wxTextCtrlEx(&parent, nullID);
	lay.gbAdd(bag, impl_->extra_extra_, lay.valigng)->Show(false);

	extraParameters_[ParameterSection::host].emplace_back("", impl_->extra_host_desc_, impl_->extra_host_);
	extraParameters_[ParameterSection::user].emplace_back("", impl_->extra_user_desc_, impl_->extra_user_);
	extraParameters_[ParameterSection::credentials].emplace_back("", impl_->extra_credentials_desc_, impl_->extra_credentials_);
	extraParameters_[ParameterSection::extra].emplace_back("", impl_->extra_extra_desc_, impl_->extra_extra_);

	for (auto const& proto : CServer::GetDefaultProtocols()) {
		auto const entry = findGroup(proto);
		if (entry.first != protocolGroups().cend()) {
			if (entry.second == entry.first->protocols.cbegin()) {
				mainProtocolListIndex_[proto] = impl_->protocol_choice_->Append(entry.first->name);
			}
			else {
				mainProtocolListIndex_[proto] = mainProtocolListIndex_[entry.first->protocols.front().first];
			}
		}
		else {
			mainProtocolListIndex_[proto] = impl_->protocol_choice_->Append(CServer::GetProtocolName(proto));
		}
	}
	if (impl_->protocol_choice_->GetCount()) {
		impl_->protocol_choice_->Select(0);
	}

	for (int i = 0; i < static_cast<int>(LogonType::count); ++i) {
		impl_->logonType_->Append(GetNameFromLogonType(static_cast<LogonType>(i)));
	}

	impl_->protocol_choice_->Bind(wxEVT_CHOICE, [this](wxEvent const&) {
		auto p = GetProtocol();
		SetProtocol(p);
		if (impl_->changeHandler_) {
			impl_->changeHandler_(p, GetLogonType());
		}
	});

	impl_->jurisdiction_->Append(_("North America"));
	impl_->jurisdiction_->Append(_("Europe Union"));
	impl_->jurisdiction_->Append(_("FedRAMP"));
	impl_->jurisdiction_->Select(0);

	impl_->logonType_->Bind(wxEVT_CHOICE, [this](wxEvent const&) {
		if (impl_->changeHandler_) {
			impl_->changeHandler_(GetProtocol(), GetLogonType());
		}
	});

	impl_->keyfile_browse_->Bind(wxEVT_BUTTON, [this](wxEvent const&) {
#if FZ_WINDOWS
		wchar_t const* all = L"*.*";
#else
		wchar_t const* all = L"*";
#endif
		wxString wildcards;
		auto protocol = GetProtocol();

		if (protocol == SFTP) {
			wildcards = wxString::Format(L"%s|*.ppk|%s|*.pem|%s|%s",
											  _("PPK files"), _("PEM files"), _("All files"), all);
		}
		if (protocol == GOOGLE_CLOUD_SVC_ACC) {
			wildcards = wxString::Format(L"%s|*.json|%s|%s", _("JSON files"), _("All files"), all);
		}

		wxFileDialog dlg(&parent_, _("Choose a key file"), wxString(), wxString(), wildcards, wxFD_OPEN|wxFD_FILE_MUST_EXIST);

		if (dlg.ShowModal() == wxID_OK) {
			std::wstring keyFile = dlg.GetPath().ToStdWstring();

			if (protocol == SFTP) {
				auto infos = LoadKeyfile(keyFile, false);
				if (!infos.empty()) {
					impl_->keyfile_->ChangeValue(keyFile);
#if USE_MAC_SANDBOX
					OSXSandboxUserdirs::Get().AddFile(keyFile);
#endif
				}
				else {
					impl_->keyfile_->SetFocus();
				}
			}
			if (protocol == GOOGLE_CLOUD_SVC_ACC) {
				if (ValidateGoogleAccountKeyFile(keyFile)) {
					impl_->keyfile_->ChangeValue(keyFile);
#if USE_MAC_SANDBOX
					OSXSandboxUserdirs::Get().AddFile(keyFile);
#endif
				}
				else {
					impl_->keyfile_->SetFocus();
				}
			}
		}
	});
}

GeneralSiteControls::~GeneralSiteControls()
{
}

void GeneralSiteControls::SetSite(Site const& site, bool)
{
	if (!site) {
		// Empty all site information
		SetProtocol(FTP);
		impl_->host_->ChangeValue(wxString());
		impl_->port_->ChangeValue(wxString());
		impl_->jurisdiction_->SetSelection(0);
		impl_->logonType_->SetStringSelection(GetNameFromLogonType(logonType_));
		impl_->user_->ChangeValue(wxString());
		impl_->pass_->ChangeValue(wxString());
		impl_->pass_->SetHint(wxString());
		impl_->account_->ChangeValue(wxString());
		impl_->keyfile_->ChangeValue(wxString());
	}
	else {
		impl_->host_->ChangeValue(site.Format(ServerFormat::host_only));

		unsigned int port = site.server.GetPort();
		if (port != CServer::GetDefaultPort(site.server.GetProtocol())) {
			impl_->port_->ChangeValue(wxString::Format(_T("%d"), port));
		}
		else {
			impl_->port_->ChangeValue(wxString());
		}

		ServerProtocol protocol = site.server.GetProtocol();
		SetProtocol(protocol);

		impl_->logonType_->SetStringSelection(GetNameFromLogonType(site.credentials.logonType_));

		impl_->user_->ChangeValue(site.server.GetUser());
		impl_->account_->ChangeValue(site.credentials.account_);

		std::wstring pass = site.credentials.GetPass();

		if (logonType_ != LogonType::anonymous && logonType_ != LogonType::interactive &&
			(protocol != SFTP || logonType_ != LogonType::key) && protocol != GOOGLE_CLOUD_SVC_ACC) {
			if (site.credentials.encrypted_) {
				impl_->pass_->ChangeValue(wxString());

				// @translator: Keep this string as short as possible
				impl_->pass_->SetHint(_("Leave empty to keep existing password."));
				for (auto & control : extraParameters_[ParameterSection::credentials]) {
					std::get<2>(control)->SetHint(_("Leave empty to keep existing data."));
				}
			}
			else {
				impl_->pass_->ChangeValue(pass);
				impl_->pass_->SetHint(wxString());

				auto it = extraParameters_[ParameterSection::credentials].begin();

				auto const& traits = ExtraServerParameterTraits(protocol);
				for (auto const& trait : traits) {
					if (trait.section_ != ParameterSection::credentials) {
						continue;
					}

					std::get<2>(*it)->ChangeValue(site.credentials.GetExtraParameter(trait.name_));
					++it;
				}
			}
		}
		else {
			impl_->pass_->ChangeValue(wxString());
			impl_->pass_->SetHint(wxString());
		}

		std::vector<GeneralSiteControls::Parameter>::iterator paramIt[ParameterSection::section_count];
		for (int i = 0; i < ParameterSection::section_count; ++i) {
			paramIt[i] = extraParameters_[i].begin();
		}
		auto const& traits = ExtraServerParameterTraits(protocol);
		for (auto const& trait : traits) {
			if (trait.section_ == ParameterSection::credentials || trait.flags_ & ParameterTraits::custom) {
				continue;
			}

			std::wstring value = site.server.GetExtraParameter(trait.name_);
			std::get<2>(*paramIt[trait.section_])->ChangeValue(value.empty() ? trait.default_ : value);
			++paramIt[trait.section_];
		}

		impl_->keyfile_->ChangeValue(site.credentials.keyFile_);

		if (site.server.GetProtocol() == CLOUDFLARE_R2) {
			auto region = site.server.GetExtraParameter("jurisdiction");
			if (region == L"eu") {
				impl_->jurisdiction_->SetSelection(1);
			}
			else if (region == L"fedramp") {
				impl_->jurisdiction_->SetSelection(2);
			}
			else {
				impl_->jurisdiction_->SetSelection(0);
			}
		}
	}
}

void GeneralSiteControls::SetControlVisibility(ServerProtocol protocol, LogonType type)
{
	protocol_ = protocol;
	logonType_ = type;

	auto const group = findGroup(protocol);
	bool const isFtp = group.first != protocolGroups().cend() && group.first->protocols.front().first == FTP;

	impl_->encryption_desc_->Show(group.first != protocolGroups().cend());
	impl_->encryption_->Show(group.first != protocolGroups().cend());

	impl_->storj_docs_->Show(protocol == STORJ || protocol == STORJ_GRANT);
	impl_->storj_signup_->Show(protocol == STORJ || protocol == STORJ_GRANT);

	auto const supportedlogonTypes = GetSupportedLogonTypes(protocol);
	assert(!supportedlogonTypes.empty());

	impl_->logonType_->Clear();

	if (std::find(supportedlogonTypes.cbegin(), supportedlogonTypes.cend(), type) == supportedlogonTypes.cend()) {
		type = supportedlogonTypes.front();
	}

	for (auto const supportedLogonType : supportedlogonTypes) {
		impl_->logonType_->Append(GetNameFromLogonType(supportedLogonType));
		if (supportedLogonType == type) {
			impl_->logonType_->SetSelection(impl_->logonType_->GetCount() - 1);
		}
	}

	impl_->host_desc_->Show(protocol != CLOUDFLARE_R2);
	impl_->host_->Show(protocol != CLOUDFLARE_R2);
	impl_->port_desc_->Show(protocol != CLOUDFLARE_R2);
	impl_->port_->Show(protocol != CLOUDFLARE_R2);
	impl_->jurisdiction_desc_->Show(protocol == CLOUDFLARE_R2);
	impl_->jurisdiction_->Show(protocol == CLOUDFLARE_R2);

	bool const hasUser = ProtocolHasUser(protocol) && type != LogonType::anonymous;
	bool const hasPw = type != LogonType::anonymous && type != LogonType::interactive &&
		(protocol != SFTP || type != LogonType::key) &&
		(!(protocol == S3 || protocol == CLOUDFLARE_R2) || type != LogonType::profile) &&
		protocol != GOOGLE_CLOUD_SVC_ACC;

	impl_->user_desc_->Show(hasUser);
	impl_->user_->Show(hasUser);
	impl_->pass_desc_->Show(hasPw);
	impl_->pass_->Show(hasPw);
	impl_->account_desc_->Show(isFtp && type == LogonType::account);
	impl_->account_->Show(isFtp && type == LogonType::account);
	impl_->keyfile_desc_->Show((protocol == SFTP || protocol == GOOGLE_CLOUD_SVC_ACC) && type == LogonType::key);
	impl_->keyfile_->Show((protocol == SFTP || protocol == GOOGLE_CLOUD_SVC_ACC) && type == LogonType::key);
	impl_->keyfile_browse_->Show((protocol == SFTP || protocol == GOOGLE_CLOUD_SVC_ACC) && type == LogonType::key);

	wxString hostLabel = _("&Host:");
	wxString hostHint;
	wxString userHint;
	wxString userLabel = _("&User:");
	wxString passLabel = _("Pass&word:");
	switch (protocol) {
	case STORJ:
		// @translator: Keep short
		hostLabel = _("S&atellite:");
		// @translator: Keep short
		userLabel = _("API &Key:");
		// @translator: Keep short
		passLabel = _("Encr&yption Passphrase:");
		break;
	case STORJ_GRANT:
		// @translator: Keep short
		hostLabel = _("S&atellite:");
		// @translator: Keep short
		passLabel = _("Access &Grant:");
		break;
	case S3:
	case CLOUDFLARE_R2:
		// @translator: Keep short
		userLabel = _("&Access key ID:");
		if (type == LogonType::profile) {
			userLabel = _("&Profile:");
		}
		// @translator: Keep short
		passLabel = _("Secret Access &Key:");
		if (protocol == CLOUDFLARE_R2) {
			hostLabel = _("Jurisdiction:");
		}
		break;
	case S3_SSO:
		// @translator: Keep short
		userLabel = _("&Account ID:");
		if (type == LogonType::profile) {
			userLabel = _("&Profile:");
		}
		break;
	case AZURE_FILE:
	case AZURE_BLOB:
		// @translator: Keep short
		userLabel = _("Storage &account:");
		passLabel = _("Access &Key:");
		break;
	case GOOGLE_CLOUD:
		userLabel = _("Pro&ject ID:");
		break;
	case SWIFT:
	case RACKSPACE:
		// @translator: Keep short
		hostLabel = _("Identity &host:");
		// @translator: Keep short
		hostHint = _("Host name of identity service");
		userLabel = _("Pro&ject:");
		// @translator: Keep short
		userHint = _("Project (or tenant) name or ID");
		break;
	case B2:
		// @translator: Keep short
		userLabel = _("&Application Key ID:");
		// @translator: Keep short
		passLabel = _("Application &Key:");
		break;
	default:
		break;
	}
	impl_->host_desc_->SetLabel(hostLabel);
	impl_->host_->SetHint(hostHint);
	impl_->user_desc_->SetLabel(userLabel);
	impl_->user_->SetHint(userHint);
	impl_->pass_desc_->SetLabel(passLabel);

	auto InsertRow = [](std::vector<GeneralSiteControls::Parameter> & rows, std::string const &name, bool password) {

		if (rows.empty()) {
			return rows.end();
		}

		wxWindow* const parent = std::get<1>(rows.back())->GetParent();
		wxGridBagSizer* const sizer = dynamic_cast<wxGridBagSizer*>(std::get<1>(rows.back())->GetContainingSizer());

		if (!sizer) {
			return rows.end();
		}
		auto pos = sizer->GetItemPosition(std::get<1>(rows.back()));

		for (int row = sizer->GetRows() - 1; row > pos.GetRow(); --row) {
			auto left = sizer->FindItemAtPosition(wxGBPosition(row, 0));
			auto right = sizer->FindItemAtPosition(wxGBPosition(row, 1));
			if (!left) {
				break;
			}
			left->SetPos(wxGBPosition(row + 1, 0));
			if (right) {
				right->SetPos(wxGBPosition(row + 1, 1));
			}
		}
		auto label = new wxStaticText(parent, nullID, L"");
		auto text = new wxTextCtrlEx(parent, nullID, wxString(), wxDefaultPosition, wxDefaultSize, password ? wxTE_PASSWORD : 0);
		sizer->Add(label, wxGBPosition(pos.GetRow() + 1, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
		sizer->Add(text, wxGBPosition(pos.GetRow() + 1, 1), wxDefaultSpan, wxALIGN_CENTER_VERTICAL|wxGROW);

		rows.emplace_back(name, label, text);
		return rows.end() - 1;
	};

	auto SetLabel = [](wxStaticText & label, ServerProtocol const protocol, std::string const& name) {
		if (name == "login_hint") {
			label.SetLabel(_("Login (optional):"));
		}
		else if (name == "identpath") {
			// @translator: Keep short
			label.SetLabel(_("Identity service path:"));
		}
		else if (name == "identuser") {
			if (protocol == CLOUDFLARE_R2) {
				label.SetLabel(_("Acco&unt ID:"));
			}
			else {
				label.SetLabel(_("&User:"));
			}
		}
		else {
			label.SetLabel(fz::to_wstring_from_utf8(name));
		}
	};

	std::vector<GeneralSiteControls::Parameter>::iterator paramIt[ParameterSection::section_count];
	for (int i = 0; i < ParameterSection::section_count; ++i) {
		paramIt[i] = extraParameters_[i].begin();
	}

	std::map<std::pair<std::string, ParameterSection::type>, std::wstring> values;
	for (int i = 0; i < ParameterSection::section_count; ++i) {
		for (auto const& row : extraParameters_[i]) {
			auto const& name = std::get<0>(row);
			if (!name.empty()) {
				auto value = std::get<2>(row)->GetValue().ToStdWstring();
				if (!value.empty()) {
					values[std::make_pair(name, static_cast<ParameterSection::type>(i))] = std::move(value);
				}
			}
		}
	}

	std::vector<ParameterTraits> const& parameterTraits = ExtraServerParameterTraits(protocol);
	for (auto const& trait : parameterTraits) {
		if (trait.flags_ & ParameterTraits::custom) {
			continue;
		}
		auto & parameters = extraParameters_[trait.section_];
		auto & it = paramIt[trait.section_];

		if (it == parameters.cend()) {
			it = InsertRow(parameters, trait.name_, trait.section_ == ParameterSection::credentials);
		}

		if (it == parameters.cend()) {
			continue;
		}

		std::get<0>(*it) = trait.name_;
		std::get<1>(*it)->Show();
		SetLabel(*std::get<1>(*it), protocol, trait.name_);

		auto * pValue = std::get<2>(*it);
		pValue->Show();
		auto valueIt = values.find(std::make_pair(trait.name_, trait.section_));
		if (valueIt != values.cend()) {
			pValue->ChangeValue(valueIt->second);
		}
		else {
			pValue->ChangeValue(wxString());
		}
		pValue->SetHint(trait.hint_);

		++it;
	}

	auto encSizer = impl_->encryption_->GetContainingSizer();
	encSizer->Show(encSizer->GetItemCount() - 1, paramIt[ParameterSection::host] == extraParameters_[ParameterSection::host].cbegin());

	for (int i = 0; i < ParameterSection::section_count; ++i) {
		for (; paramIt[i] != extraParameters_[i].cend(); ++paramIt[i]) {
			std::get<0>(*paramIt[i]).clear();
			std::get<1>(*paramIt[i])->Hide();
			std::get<2>(*paramIt[i])->Hide();
		}
	}

	auto keyfileSizer = impl_->keyfile_desc_->GetContainingSizer();
	if (keyfileSizer) {
		keyfileSizer->CalcMin();
		keyfileSizer->Layout();
	}
}

bool GeneralSiteControls::UpdateSite(Site & site, bool silent)
{
	ServerProtocol protocol = GetProtocol();
	if (protocol == UNKNOWN) {
		// How can this happen?
		return false;
	}

	std::wstring user = impl_->user_->GetValue().ToStdWstring();
	std::wstring pw = impl_->pass_->GetValue().ToStdWstring();

	{
		std::wstring host = impl_->host_->GetValue().ToStdWstring();
		std::wstring port = impl_->port_->GetValue().ToStdWstring();

		if (host.empty()) {
			if (!silent) {
				impl_->host_->SetFocus();
				wxMessageBoxEx(_("You have to enter a hostname."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
			}
			return false;
		}

		Site parsedSite;
		std::wstring error;
		site.m_default_bookmark.m_remoteDir = CServerPath();
		if (!parsedSite.ParseUrl(host, port, user, pw, error, site.m_default_bookmark.m_remoteDir, protocol)) {
			if (!silent) {
				impl_->host_->SetFocus();
				wxMessageBoxEx(error, _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
			}
			return false;
		}

		protocol = parsedSite.server.GetProtocol();
		site.server.SetProtocol(protocol);
		site.server.SetHost(parsedSite.server.GetHost(), parsedSite.server.GetPort());

		if (!parsedSite.server.GetUser().empty()) {
			user = parsedSite.server.GetUser();
		}
		if (!parsedSite.credentials.GetPass().empty()) {
			pw = parsedSite.credentials.GetPass();
		}
	}

	auto logon_type = GetLogonType();
	auto const supportedlogonTypes = GetSupportedLogonTypes(protocol);
	if (std::find(supportedlogonTypes.cbegin(), supportedlogonTypes.cend(), logon_type) == supportedlogonTypes.cend()) {
		logon_type = supportedlogonTypes.front();
	}

	if (impl_->options_.get_int(OPTION_DEFAULT_KIOSKMODE) != 0 &&
			impl_->pass_->IsEnabled() &&
	        (logon_type == LogonType::account || logon_type == LogonType::normal))
	{
		if (!silent) {
			wxString msg;
			if (impl_->options_.get_int(OPTION_DEFAULT_KIOSKMODE) != 0 && impl_->options_.predefined(OPTION_DEFAULT_KIOSKMODE)) {
				msg = _("Saving of password has been disabled by your system administrator.");
			}
			else {
				msg = _("Saving of passwords has been disabled by you.");
			}
			msg += _T("\n");
			msg += _("'Normal' and 'Account' logontypes are not available. Your entry has been changed to 'Ask for password'.");
			wxMessageBoxEx(msg, _("Site Manager - Cannot remember password"), wxICON_INFORMATION, wxGetTopLevelParent(&parent_));
		}
		logon_type = LogonType::ask;
	}
	site.SetLogonType(logon_type);

	// At this point we got:
	// - Valid protocol, host, port, logontype
	// - Optional remotePath
	// - Optional, unvalidated username, password

	if (!ProtocolHasUser(protocol) || logon_type == LogonType::anonymous) {
		user.clear();
	}
	else if (logon_type != LogonType::ask &&
	         (logon_type != LogonType::interactive || protocol == S3_SSO) &&
	         user.empty())
	{
		// Require username for non-anonymous, non-ask logon type
		if (!silent) {
			impl_->user_->SetFocus();
			wxMessageBoxEx(_("You have to specify a user name"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
		}
		return false;
	}

	// We don't allow username of only spaces, confuses both users and XML libraries
	if (!user.empty()) {
		bool space_only = true;
		for (auto const& c : user) {
			if (c != ' ') {
				space_only = false;
				break;
			}
		}
		if (space_only) {
			if (!silent) {
				impl_->user_->SetFocus();
				wxMessageBoxEx(_("Username cannot be a series of spaces"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
			}
			return false;
		}
	}
	site.SetUser(user);

	// At this point username has been validated

	// Require account for account logon type
	if (logon_type == LogonType::account) {
		std::wstring account = impl_->account_->GetValue().ToStdWstring();
		if (account.empty()) {
			if (!silent) {
				impl_->account_->SetFocus();
				wxMessageBoxEx(_("You have to enter an account name"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
			}
			return false;
		}
		site.credentials.account_ = account;
	}

	// In key file logon type, check that the provided key file exists
	if (logon_type == LogonType::key) {
		std::wstring keyFile = impl_->keyfile_->GetValue().ToStdWstring();
		if (keyFile.empty()) {
			if (!silent) {
				wxMessageBoxEx(_("You have to enter a key file path"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
				impl_->keyfile_->SetFocus();
			}
			return false;
		}

		if (protocol == SFTP) {
			auto infos = LoadKeyfile(keyFile, silent);
			if (infos.empty()) {
				if (!silent) {
					impl_->keyfile_->SetFocus();
				}
				return false;
			}
		}

		if (protocol == GOOGLE_CLOUD_SVC_ACC) {
			if (!ValidateGoogleAccountKeyFile(keyFile)) {
				if (!silent) {
					impl_->keyfile_->SetFocus();
				}
				return false;
			}
		}

		site.credentials.keyFile_ = keyFile;
	}
	else {
		site.credentials.keyFile_.clear();
	}

	site.server.ClearExtraParameters();

	std::vector<ParameterTraits> const& parameterTraits = ExtraServerParameterTraits(protocol);
	for (auto const& trait : parameterTraits) {
		if (trait.section_ == ParameterSection::credentials || trait.flags_ & ParameterTraits::custom) {
			continue;
		}
		for (auto const& row : extraParameters_[trait.section_]) {
			if (std::get<0>(row) == trait.name_) {
				std::wstring value = std::get<2>(row)->GetValue().ToStdWstring();
				if (!(trait.flags_ & ParameterTraits::optional)) {
					if (value.empty()) {
						if (!silent) {
							std::get<2>(row)->SetFocus();
							wxMessageBoxEx(_("You need to enter a value."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
						}
						return false;
					}
				}
				if (trait.section_ == ParameterSection::credentials) {
					site.credentials.SetExtraParameter(protocol, trait.name_, value);
				}
				else {
					site.server.SetExtraParameter(trait.name_, value);
				}
				break;
			}
		}
	}

	if (protocol == S3 && logon_type == LogonType::profile) {
		pw.clear();
	}

#if ENABLE_STORJ
		if (protocol == STORJ_GRANT && logon_type == LogonType::normal) {
			fz::trim(pw);
			if (pw.empty() && !site.credentials.encrypted_) {
				wxMessageBoxEx(_("You need to enter an access grant."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
				impl_->pass_->SetFocus();
			}
			else if (!pw.empty()) {
				CStorjKeyInterface ki(impl_->options_, wxGetTopLevelParent(&parent_));
				auto [valid, satellite] = ki.ValidateGrant(pw, silent);
				if (valid) {
					if (!satellite.empty()) {
						size_t pos = satellite.rfind('@');
						if (pos != std::wstring::npos) {
							satellite = satellite.substr(pos + 1);
						}
						pos = satellite.rfind(':');
						std::wstring port = L"7777";
						if (pos != std::wstring::npos) {
							port = satellite.substr(pos + 1);
							satellite = satellite.substr(0, pos);
						}
						site.server.SetHost(satellite, fz::to_integral<unsigned short>(port, 7777));
					}
				}
				else {
					if (!silent) {
						impl_->pass_->SetFocus();
						wxString msg = wxString::Format(_("You have to enter a valid access grant.\nError: %s"), satellite);
						wxMessageBoxEx(msg, _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
					}
					return false;
				}
			}
		}
#endif

	if (site.server.GetProtocol() == CLOUDFLARE_R2) {
		auto jurisdiction = impl_->jurisdiction_->GetSelection();
		if (jurisdiction == 1) {
			site.server.SetExtraParameter("jurisdiction", L"eu");
			site.server.SetHost(L"eu.r2.cloudflarestorage.com", site.server.GetPort());
		}
		else if (jurisdiction == 2) {
			site.server.SetExtraParameter("jurisdiction", L"fedramp");
			site.server.SetHost(L"fedramp.r2.cloudflarestorage.com", site.server.GetPort());
		}
		else {
			site.server.ClearExtraParameter("jurisdiction");
			site.server.SetHost(L"r2.cloudflarestorage.com", site.server.GetPort());
		}
	}

	if (site.credentials.encrypted_) {
		if (!pw.empty()) {
			site.credentials.encrypted_ = fz::public_key();
			site.credentials.SetPass(pw);
		}
	}
	else {
		site.credentials.SetPass(pw);
	}

	return true;
}

LogonType GeneralSiteControls::GetLogonType() const
{
	return GetLogonTypeFromName(impl_->logonType_->GetStringSelection().ToStdWstring());
}

void GeneralSiteControls::SetProtocol(ServerProtocol protocol)
{
	if (protocol == UNKNOWN) {
		protocol = FTP;
	}

	auto const entry = findGroup(protocol);
	if (entry.first != protocolGroups().cend()) {
		impl_->encryption_->Clear();
		for (auto const& prot : entry.first->protocols) {
			std::wstring name = prot.second;
			if (!CServer::ProtocolHasFeature(prot.first, ProtocolFeature::Security)) {
				name += ' ';
				name += 0x26a0; // Unicode's warning emoji
				name += 0xfe0f; // Variant selector, makes it colorful
			}
			impl_->encryption_->AppendString(name);
		}
		impl_->encryption_desc_->Show();
		impl_->encryption_->Show();
		impl_->encryption_->SetSelection(entry.second - entry.first->protocols.cbegin());
	}
	else {
		impl_->encryption_desc_->Hide();
		impl_->encryption_->Hide();
	}

	auto const protoIt = mainProtocolListIndex_.find(protocol);
	if (protoIt != mainProtocolListIndex_.cend()) {
		impl_->protocol_choice_->SetSelection(protoIt->second);
	}
	else if (protocol != ServerProtocol::UNKNOWN) {
		auto const entry = findGroup(protocol);
		if (entry.first != protocolGroups().cend()) {
			mainProtocolListIndex_[protocol] = impl_->protocol_choice_->Append(entry.first->name);
			for (auto const& sub : entry.first->protocols) {
				mainProtocolListIndex_[sub.first] = mainProtocolListIndex_[protocol];
			}
		}
		else {
			mainProtocolListIndex_[protocol] = impl_->protocol_choice_->Append(CServer::GetProtocolName(protocol));
		}

		impl_->protocol_choice_->SetSelection(mainProtocolListIndex_[protocol]);
	}
	else {
		impl_->protocol_choice_->SetSelection(mainProtocolListIndex_[FTP]);
	}
	UpdateHostFromDefaults(GetProtocol());
}

ServerProtocol GeneralSiteControls::GetProtocol() const
{
	int const sel = impl_->protocol_choice_->GetSelection();

	ServerProtocol protocol = UNKNOWN;
	for (auto const it : mainProtocolListIndex_) {
		if (it.second == sel) {
			protocol = it.first;
			break;
		}
	}

	auto const group = findGroup(protocol);
	if (group.first != protocolGroups().cend()) {
		int encSel = impl_->encryption_->GetSelection();
		if (encSel < 0 || encSel >= static_cast<int>(group.first->protocols.size())) {
			encSel = 0;
		}
		protocol = group.first->protocols[encSel].first;
	}

	return protocol;
}

void GeneralSiteControls::UpdateHostFromDefaults(ServerProtocol const newProtocol)
{
	if (newProtocol != protocol_) {
		auto const oldDefault = std::get<0>(GetDefaultHost(protocol_));
		auto const newDefault = GetDefaultHost(newProtocol);

		std::wstring const host = impl_->host_->GetValue().ToStdWstring();
		if (host.empty() || host == oldDefault) {
			impl_->host_->ChangeValue(std::get<0>(newDefault));
		}
		impl_->host_->SetHint(std::get<1>(newDefault));
	}
}

void GeneralSiteControls::SetControlState(bool predefined)
{
	impl_->host_->Enable(!predefined);
	impl_->port_->Enable(!predefined);
	impl_->protocol_choice_->Enable(!predefined);
	impl_->encryption_->Enable(!predefined);
	impl_->logonType_->Enable(!predefined);

	impl_->user_->Enable(!predefined && logonType_ != LogonType::anonymous);
	impl_->pass_->Enable(!predefined && (logonType_ == LogonType::normal || logonType_ == LogonType::account));
	impl_->account_->Enable(!predefined && logonType_ == LogonType::account);
	impl_->keyfile_->Enable(!predefined && logonType_ == LogonType::key);
	impl_->keyfile_browse_->Enable(!predefined && logonType_ == LogonType::key);

	for (int i = 0; i < ParameterSection::section_count; ++i) {
		for (auto & param : extraParameters_[i]) {
			std::get<2>(param)->Enable(!predefined);
		}
	}
}

bool GeneralSiteControls::ValidateGoogleAccountKeyFile(std::wstring_view)
{
	return true;
}

void GeneralSiteControls::UpdateWidth(int width)
{
	wxSize const descSize = impl_->encryption_desc_->GetSize();
	wxSize const encSize = impl_->encryption_->GetSize();

	int dataWidth = std::max(encSize.GetWidth(), impl_->protocol_choice_->GetSize().GetWidth());

	auto generalSizer = static_cast<wxGridBagSizer*>(impl_->protocol_choice_->GetContainingSizer());
	width = std::max(width, static_cast<int>(descSize.GetWidth() * 2 + dataWidth + generalSizer->GetHGap() * 3));

	wxSize page_min_size = parent_.GetSizer()->GetMinSize();
	if (page_min_size.x < width) {
		page_min_size.x = width;
		parent_.GetSizer()->SetMinSize(page_min_size);
	}

	// Set min height of general page sizer
	generalSizer->SetMinSize(generalSizer->GetMinSize());

	// Set min height of encryption row
	auto encSizer = impl_->encryption_->GetContainingSizer();
	encSizer->GetItem(encSizer->GetItemCount() - 1)->SetMinSize(0, std::max(descSize.GetHeight(), encSize.GetHeight()));
}

struct AdvancedSiteControls::impl final
{
	wxStaticText* servertype_desc_{};
	wxChoice* servertype_{};
	wxCheckBox* bypass_proxy_{};
	wxTextCtrlEx* local_dir_{};
	wxButton* local_dir_browse_{};
	wxTextCtrlEx* remote_dir_{};
	wxCheckBox* sync_browsing_{};
	wxCheckBox* comparison_{};
	wxSpinCtrlEx* time_offset_hours_{};
	wxSpinCtrlEx* time_offset_minutes_{};
};

AdvancedSiteControls::AdvancedSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer)
    : SiteControls(parent)
	, impl_(std::make_unique<impl>())

{
	if (!sizer.IsColGrowable(0)) {
		sizer.AddGrowableCol(0);
	}

	auto* row = lay.createFlex(0, 1);
	sizer.Add(row);

	impl_->servertype_desc_ = new wxStaticText(&parent, nullID, _("Server &type:"));
	row->Add(impl_->servertype_desc_, lay.valign);

	impl_->servertype_ = new wxChoice(&parent, nullID);
	row->Add(impl_->servertype_, lay.valign);

	for (int i = 0; i < SERVERTYPE_MAX; ++i) {
		impl_->servertype_->Append(CServer::GetNameFromServerType(static_cast<ServerType>(i)));
	}
	impl_->servertype_->Select(0);

	sizer.AddSpacer(0);
	impl_->bypass_proxy_ = new wxCheckBox(&parent, nullID, _("B&ypass proxy"));
	sizer.Add(impl_->bypass_proxy_);

	sizer.Add(new wxStaticLine(&parent), lay.grow);

	sizer.Add(new wxStaticText(&parent, nullID, _("Default &local directory:")));

	row = lay.createFlex(0, 1);
	sizer.Add(row, lay.grow);
	row->AddGrowableCol(0);
	impl_->local_dir_ = new wxTextCtrlEx(&parent, nullID);
	row->Add(impl_->local_dir_, lay.valigng);
	impl_->local_dir_browse_ = new wxButton(&parent, nullID, _("&Browse..."));
	row->Add(impl_->local_dir_browse_, lay.valign);

	impl_->local_dir_browse_->Bind(wxEVT_BUTTON, [&](wxEvent const&) {
		wxDirDialog dlg(wxGetTopLevelParent(&parent_), _("Choose the default local directory"), impl_->local_dir_->GetValue(), wxDD_NEW_DIR_BUTTON);
		if (dlg.ShowModal() == wxID_OK) {
			impl_->local_dir_->ChangeValue(dlg.GetPath());
		}
	});

	sizer.AddSpacer(0);
	sizer.Add(new wxStaticText(&parent, nullID, _("Default r&emote directory:")));
	impl_->remote_dir_ = new wxTextCtrlEx(&parent, nullID);
	sizer.Add(impl_->remote_dir_, lay.grow);
	sizer.AddSpacer(0);
	impl_->sync_browsing_ = new wxCheckBox(&parent, nullID, _("&Use synchronized browsing"));
	sizer.Add(impl_->sync_browsing_);
	impl_->comparison_ = new wxCheckBox(&parent, nullID, _("Directory comparison"));
	sizer.Add(impl_->comparison_);

	sizer.Add(new wxStaticLine(&parent), lay.grow);

	sizer.Add(new wxStaticText(&parent, nullID, _("&Adjust server time, offset by:")));
	row = lay.createFlex(0, 1);
	sizer.Add(row);
	impl_->time_offset_hours_ = new wxSpinCtrlEx(&parent, nullID, wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(26), -1));
	impl_->time_offset_hours_->SetRange(-24, 24);
	impl_->time_offset_hours_->SetMaxLength(3);
	row->Add(impl_->time_offset_hours_, lay.valign);
	row->Add(new wxStaticText(&parent, nullID, _("Hours,")), lay.valign);
	impl_->time_offset_minutes_ = new wxSpinCtrlEx(&parent, nullID, wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(26), -1));
	impl_->time_offset_minutes_->SetRange(-59, 59);
	impl_->time_offset_minutes_->SetMaxLength(3);
	row->Add(impl_->time_offset_minutes_, lay.valign);
	row->Add(new wxStaticText(&parent, nullID, _("Minutes")), lay.valign);
}

AdvancedSiteControls::~AdvancedSiteControls()
{
}

void AdvancedSiteControls::SetSite(Site const& site, bool predefined)
{
	impl_->servertype_->Enable(!predefined);
	impl_->bypass_proxy_->Enable(!predefined);
	impl_->sync_browsing_->Enable(!predefined);
	impl_->comparison_->Enable(!predefined);
	impl_->local_dir_->Enable(!predefined);
	impl_->local_dir_browse_->Enable(!predefined);
	impl_->remote_dir_->Enable(!predefined);
	impl_->time_offset_hours_->Enable(!predefined);
	impl_->time_offset_minutes_->Enable(!predefined);

	if (site) {
		impl_->servertype_->SetSelection(site.server.GetType());
		impl_->bypass_proxy_->SetValue(site.server.GetBypassProxy());
		impl_->local_dir_->ChangeValue(site.m_default_bookmark.m_localDir);
		impl_->remote_dir_->ChangeValue(site.m_default_bookmark.m_remoteDir.GetPath());
		impl_->sync_browsing_->SetValue(site.m_default_bookmark.m_sync);
		impl_->comparison_->SetValue(site.m_default_bookmark.m_comparison);
		impl_->time_offset_hours_->SetValue(site.server.GetTimezoneOffset() / 60);
		impl_->time_offset_minutes_->SetValue(site.server.GetTimezoneOffset() % 60);
	}
	else {
		impl_->servertype_->SetSelection(0);
		impl_->bypass_proxy_->SetValue(false);
		impl_->sync_browsing_->SetValue(false);
		impl_->comparison_->SetValue(false);
		impl_->local_dir_->ChangeValue(wxString());
		impl_->remote_dir_->ChangeValue(wxString());
		impl_->time_offset_hours_->SetValue(0);
		impl_->time_offset_minutes_->SetValue(0);
	}
}

void AdvancedSiteControls::SetControlVisibility(ServerProtocol protocol, LogonType)
{
	bool const hasServerType = CServer::ProtocolHasFeature(protocol, ProtocolFeature::ServerType);
	impl_->servertype_desc_->Show(hasServerType);
	impl_->servertype_->Show(hasServerType);
	auto * serverTypeSizer = impl_->servertype_desc_->GetContainingSizer()->GetContainingWindow()->GetSizer();
	serverTypeSizer->CalcMin();
	serverTypeSizer->Layout();
}

bool AdvancedSiteControls::UpdateSite(Site & site, bool silent)
{
	ServerType serverType = UNIX;
	if (!site.m_default_bookmark.m_remoteDir.empty()) {
		if (site.server.HasFeature(ProtocolFeature::ServerType)) {
			serverType = site.m_default_bookmark.m_remoteDir.GetType();
		}
		else if (site.m_default_bookmark.m_remoteDir.GetType() != UNIX) {
			site.m_default_bookmark.m_remoteDir = CServerPath();
		}
	}
	else {
		if (site.server.HasFeature(ProtocolFeature::ServerType)) {
			serverType = CServer::GetServerTypeFromName(impl_->servertype_->GetStringSelection().ToStdWstring());
		}
	}

	site.server.SetType(serverType);

	site.server.SetBypassProxy(impl_->bypass_proxy_->GetValue());

	if (site.m_default_bookmark.m_remoteDir.empty()) {
		std::wstring const remotePathRaw = impl_->remote_dir_->GetValue().ToStdWstring();
		if (!remotePathRaw.empty()) {
			site.m_default_bookmark.m_remoteDir.SetType(serverType);
			if (!site.m_default_bookmark.m_remoteDir.SetPath(remotePathRaw)) {
				if (!silent) {
					impl_->remote_dir_->SetFocus();
					auto msg = site.server.HasFeature(ProtocolFeature::ServerType) ? _("Default remote path cannot be parsed. Make sure it is a valid absolute path for the selected server type.") : _("Default remote path cannot be parsed. Make sure it is a valid absolute path.");
					wxMessageBoxEx(msg, _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
				}
				return false;
			}
		}
	}

	std::wstring const localPath = impl_->local_dir_->GetValue().ToStdWstring();
	site.m_default_bookmark.m_localDir = localPath;
	if (impl_->sync_browsing_->GetValue()) {
		if (site.m_default_bookmark.m_remoteDir.empty() || localPath.empty()) {
			if (!silent) {
				impl_->sync_browsing_->SetFocus();
				wxMessageBoxEx(_("You need to enter both a local and a remote path to enable synchronized browsing for this site."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
			}
			return false;
		}
	}


	site.m_default_bookmark.m_sync = impl_->sync_browsing_->GetValue();
	site.m_default_bookmark.m_comparison = impl_->comparison_->GetValue();

	int hours = impl_->time_offset_hours_->GetValue();
	int minutes = impl_->time_offset_minutes_->GetValue();

	site.server.SetTimezoneOffset(hours * 60 + minutes);

	return true;
}


struct TransferSettingsSiteControls::impl final
{
	wxStaticText* transfermode_desc_{};
	wxRadioButton* transfermode_default_{};
	wxRadioButton* transfermode_active_{};
	wxRadioButton* transfermode_passive_{};
	wxCheckBox* limit_max_conns_{};
	wxSpinCtrlEx* max_conns_{};
};

TransferSettingsSiteControls::TransferSettingsSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer)
	: SiteControls(parent)
	, impl_(std::make_unique<impl>())
{
	impl_->transfermode_desc_ = new wxStaticText(&parent, nullID, _("&Transfer mode:"));
	sizer.Add(impl_->transfermode_desc_);
	auto * row = lay.createFlex(0, 1);
	sizer.Add(row);
	impl_->transfermode_default_ = new wxRadioButton(&parent, nullID, _("D&efault"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	row->Add(impl_->transfermode_default_, lay.valign);
	impl_->transfermode_active_ = new wxRadioButton(&parent, nullID, _("&Active"));
	row->Add(impl_->transfermode_active_, lay.valign);
	impl_->transfermode_passive_ = new wxRadioButton(&parent, nullID, _("&Passive"));
	row->Add(impl_->transfermode_passive_, lay.valign);
	sizer.AddSpacer(0);

	impl_->limit_max_conns_ = new wxCheckBox(&parent, nullID, _("&Limit number of simultaneous connections"));
	sizer.Add(impl_->limit_max_conns_);
	row = lay.createFlex(0, 1);
	sizer.Add(row, 0, wxLEFT, lay.dlgUnits(10));
	row->Add(new wxStaticText(&parent, nullID, _("&Maximum number of connections:")), lay.valign);
	impl_->max_conns_ = new wxSpinCtrlEx(&parent, nullID, wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(26), -1));
	impl_->max_conns_->SetMaxLength(2);
	impl_->max_conns_->SetRange(1, 10);
	row->Add(impl_->max_conns_, lay.valign);

	impl_->limit_max_conns_->Bind(wxEVT_CHECKBOX, [&](wxCommandEvent const& ev){ impl_->max_conns_->Enable(ev.IsChecked()); });
}

TransferSettingsSiteControls::~TransferSettingsSiteControls()
{
}

void TransferSettingsSiteControls::SetSite(Site const& site, bool predefined)
{
	impl_->transfermode_default_->Enable(!predefined);
	impl_->transfermode_active_->Enable(!predefined);
	impl_->transfermode_passive_->Enable(!predefined);
	impl_->limit_max_conns_->Enable(!predefined);

	if (!site) {
		impl_->transfermode_default_->SetValue(true);
		impl_->limit_max_conns_->SetValue(false);
		impl_->max_conns_->Enable(false);
		impl_->max_conns_->SetValue(1);
	}
	else {
		if (CServer::ProtocolHasFeature(site.server.GetProtocol(), ProtocolFeature::TransferMode)) {
			PasvMode pasvMode = site.server.GetPasvMode();
			if (pasvMode == MODE_ACTIVE) {
				impl_->transfermode_active_->SetValue(true);
			}
			else if (pasvMode == MODE_PASSIVE) {
				impl_->transfermode_passive_->SetValue(true);
			}
			else {
				impl_->transfermode_default_->SetValue(true);
			}
		}

		impl_->limit_max_conns_->SetValue(site.connection_limit_ != 0);
		if (site.connection_limit_ != 0) {
			impl_->max_conns_->Enable(!predefined);
			impl_->max_conns_->SetValue(site.connection_limit_);
		}
		else {
			impl_->max_conns_->Enable(false);
			impl_->max_conns_->SetValue(1);
		}

	}
}

bool TransferSettingsSiteControls::UpdateSite(Site & site, bool)
{
	if (CServer::ProtocolHasFeature(site.server.GetProtocol(), ProtocolFeature::TransferMode)) {
		if (impl_->transfermode_active_->GetValue()) {
			site.server.SetPasvMode(MODE_ACTIVE);
		}
		else if (impl_->transfermode_passive_->GetValue()) {
			site.server.SetPasvMode(MODE_PASSIVE);
		}
		else {
			site.server.SetPasvMode(MODE_DEFAULT);
		}
	}
	else {
		site.server.SetPasvMode(MODE_DEFAULT);
	}

	if (impl_->limit_max_conns_->GetValue()) {
		site.connection_limit_ = impl_->max_conns_->GetValue();
	}
	else {
		site.connection_limit_ = 0;
	}

	return true;
}

void TransferSettingsSiteControls::SetControlVisibility(ServerProtocol protocol, LogonType)
{
	bool const hasTransferMode = CServer::ProtocolHasFeature(protocol, ProtocolFeature::TransferMode);
	impl_->transfermode_desc_->Show(hasTransferMode);
	impl_->transfermode_default_->Show(hasTransferMode);
	impl_->transfermode_active_->Show(hasTransferMode);
	impl_->transfermode_passive_->Show(hasTransferMode);
	impl_->transfermode_desc_->GetContainingSizer()->CalcMin();
	impl_->transfermode_desc_->GetContainingSizer()->Layout();
}

struct CharsetSiteControls::impl final
{
	wxRadioButton* charset_utf8_{};
	wxRadioButton* charset_custom_{};
	wxTextCtrlEx* encoding_{};
};

CharsetSiteControls::CharsetSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer)
    : SiteControls(parent)
	, impl_(std::make_unique<impl>())
{
	sizer.Add(new wxStaticText(&parent, nullID, _("The server uses following charset encoding for filenames:")));
	impl_->charset_utf8_ = new wxRadioButton(&parent, nullID, _("&UTF-8"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	sizer.Add(impl_->charset_utf8_);
	impl_->charset_custom_ = new wxRadioButton(&parent, nullID, _("Use &custom charset"));
	sizer.Add(impl_->charset_custom_);

	auto * row = lay.createFlex(0, 1);
	row->Add(new wxStaticText(&parent, nullID, _("&Encoding:")), lay.valign);
	impl_->encoding_ = new wxTextCtrlEx(&parent, nullID, wxString(), wxDefaultPosition, lay.defTextCtrlSize);
	row->Add(impl_->encoding_, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 18);
	sizer.Add(row);
	sizer.AddSpacer(lay.dlgUnits(6));
	sizer.Add(new wxStaticText(&parent, nullID, _("Using the wrong charset can result in filenames not displaying properly.")));

	impl_->charset_utf8_->Bind(wxEVT_RADIOBUTTON, [&](wxEvent const&){ impl_->encoding_->Disable(); });
	impl_->charset_custom_->Bind(wxEVT_RADIOBUTTON, [&](wxEvent const&){ impl_->encoding_->Enable(); });
}

CharsetSiteControls::~CharsetSiteControls()
{
}

void CharsetSiteControls::SetSite(Site const& site, bool predefined)
{
	impl_->charset_utf8_->Enable(!predefined);
	impl_->charset_custom_->Enable(!predefined);
	impl_->encoding_->Enable(!predefined && site.server.GetEncodingType() == ENCODING_CUSTOM);

	if (!site) {
		impl_->charset_utf8_->SetValue(true);
		impl_->encoding_->ChangeValue(wxString());
		impl_->encoding_->Enable(false);
	}
	else {
		switch (site.server.GetEncodingType()) {
		default:
		case ENCODING_UTF8:
			impl_->charset_utf8_->SetValue(true);
			break;
		case ENCODING_CUSTOM:
			impl_->charset_custom_->SetValue(true);
			break;
		}
		impl_->encoding_->ChangeValue(site.server.GetCustomEncoding());
	}
}

bool CharsetSiteControls::UpdateSite(Site & site, bool silent)
{
	if (CServer::ProtocolHasFeature(site.server.GetProtocol(), ProtocolFeature::Charset)) {
		if (impl_->charset_utf8_->GetValue()) {
			site.server.SetEncodingType(ENCODING_UTF8);
		}
		if (impl_->charset_custom_->GetValue()) {
			std::wstring encoding = impl_->encoding_->GetValue().ToStdWstring();

			if (encoding.empty()) {
				if (!silent) {
					impl_->encoding_->SetFocus();
					wxMessageBoxEx(_("Need to specify a character encoding"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
				}
				return false;
			}

			if (!CustomEncodingConverter::isValidEncoding(encoding)) {
				if (!silent) {
					impl_->encoding_->SetFocus();
					wxMessageBoxEx(_("The entered encoding is not valid. Please enter a valid encoding name, such as CP1252, ISO8859-1, SHIFT-JIS, ..."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
				}
				return false;
			}

			site.server.SetEncodingType(ENCODING_CUSTOM, encoding);
		}
		else {
			site.server.SetEncodingType(ENCODING_UTF8);
		}
	}
	else {
		site.server.SetEncodingType(ENCODING_UTF8);
	}

	return true;
}

struct SftpSiteControls::impl final
{
	wxCheckBox* allow_non_crlf_identification_{};
	wxCheckBox* allow_unknown_agent_keys_{};
	wxCheckBox* ignore_unknown_flags_in_attributes_{};
};

SftpSiteControls::SftpSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer)
	: SiteControls(parent)
	, impl_(std::make_unique<impl>())
{
	sizer.Add(new wxStaticText(&parent, nullID, _("Compatibility flags:")));
	impl_->allow_non_crlf_identification_ = new wxCheckBox(&parent, nullID, _("Allow peer identification string not terminated by CRLF"));
	sizer.Add(impl_->allow_non_crlf_identification_, 0, wxLEFT, lay.indent);
	impl_->allow_unknown_agent_keys_ = new wxCheckBox(&parent, nullID, _("Allow SSH agent keys of unknown type"));
	sizer.Add(impl_->allow_unknown_agent_keys_, 0, wxLEFT, lay.indent);
	impl_->ignore_unknown_flags_in_attributes_ = new wxCheckBox(&parent, nullID, _("Ignore unknown flags while parsing attributes"));
	sizer.Add(impl_->ignore_unknown_flags_in_attributes_, 0, wxLEFT, lay.indent);
	sizer.Add(new wxStaticText(&parent, nullID, _("Use with caution, compatibility flags may degrade connection security")), 0, wxLEFT, lay.indent);
}

SftpSiteControls::~SftpSiteControls()
{
}

void SftpSiteControls::SetSite(Site const& site, bool predefined)
{
	impl_->allow_non_crlf_identification_->SetValue(site.server.GetExtraParameter("allow_non_crlf_identification_string"sv) == L"1"sv);
	impl_->allow_unknown_agent_keys_->SetValue(site.server.GetExtraParameter("allow_agent_keys_of_unknown_type"sv) == L"1"sv);
	impl_->ignore_unknown_flags_in_attributes_->SetValue(site.server.GetExtraParameter("ignore_unknown_flags_in_attributes"sv) == L"1"sv);
}

void SftpSiteControls::SetControlVisibility(ServerProtocol, LogonType)
{
}

bool SftpSiteControls::UpdateSite(Site & site, bool /*silent*/)
{
	site.server.SetExtraParameter("allow_non_crlf_identification_string"sv, impl_->allow_non_crlf_identification_->GetValue() ? L"1"s : L""s);
	site.server.SetExtraParameter("allow_agent_keys_of_unknown_type"sv, impl_->allow_unknown_agent_keys_->GetValue() ? L"1"s : L""s);
	site.server.SetExtraParameter("ignore_unknown_flags_in_attributes"sv, impl_->ignore_unknown_flags_in_attributes_->GetValue() ? L"1"s : L""s);
	return true;
}


S3SiteControls::S3SiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer)
    : SiteControls(parent)
{
	if (!sizer.IsColGrowable(0)) {
		sizer.AddGrowableCol(0);
	}

	auto * options_row = lay.createFlex(2);
	options_row->AddGrowableCol(1);
	sizer.Add(options_row, lay.grow);
	options_row->Add(new wxStaticText(&parent, nullID, _("Initial re&gion:")), lay.valign);
	options_row->Add(new wxTextCtrlEx(&parent, XRCID("ID_S3_REGION")), lay.valigng);

	options_row->Add(new wxCheckBox(&parent, XRCID("ID_S3_ACCELERATE"), _("Use a&ccelerated endpoint")));

	sizer.Add(new wxStaticLine(&parent), lay.grow);
	sizer.Add(new wxStaticText(&parent, nullID, _("Server Side Encryption:")));

	auto none = new wxRadioButton(&parent, XRCID("ID_S3_NOENCRYPTION"), _("N&o encryption"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	sizer.Add(none);

	auto aes = new wxRadioButton(&parent, XRCID("ID_S3_AES256"), _("&AWS S3 encryption"));
	sizer.Add(aes);

	auto kms = new wxRadioButton(&parent, XRCID("ID_S3_AWSKMS"), _("AWS &KMS encryption"));
	sizer.Add(kms);

	auto * key_row = lay.createFlex(2);
	key_row->AddGrowableCol(1);
	sizer.Add(key_row, 0, wxLEFT|wxGROW, lay.indent);
	key_row->Add(new wxStaticText(&parent, nullID, _("&Select a key:")), lay.valign);
	auto * choice = new wxChoice(&parent, XRCID("ID_S3_KMSKEY"));
	choice->Append(_("Default (AWS/S3)"));
	choice->Append(_("Custom KMS ARN"));
	choice->SetSelection(0);
	key_row->Add(choice, lay.valigng);
	key_row->Add(new wxStaticText(&parent, nullID, _("C&ustom KMS ARN:")), lay.valign);
	key_row->Add(new wxTextCtrlEx(&parent, XRCID("ID_S3_CUSTOM_KMS")), lay.valigng);

	auto customer = new wxRadioButton(&parent, XRCID("ID_S3_CUSTOMER_ENCRYPTION"), _("Cus&tomer encryption"));
	sizer.Add(customer);
	key_row = lay.createFlex(2);
	key_row->AddGrowableCol(1);
	sizer.Add(key_row, 0, wxLEFT | wxGROW, lay.indent);
	key_row->Add(new wxStaticText(&parent, nullID, _("Customer Ke&y:")), lay.valign);
	key_row->Add(new wxTextCtrlEx(&parent, XRCID("ID_S3_CUSTOMER_KEY")), lay.valigng);

	sizer.Add(new wxStaticLine(&parent, nullID), lay.grow);

	sts_sizer_ = lay.createFlex(1);
	sts_sizer_->AddGrowableCol(0);
	sizer.Add(sts_sizer_, lay.grow);
	sts_sizer_->Add(new wxStaticText(&parent, nullID, _("Security Token Service:")));

	auto * sts_row = lay.createFlex(2);
	sts_row->AddGrowableCol(1);
	sts_sizer_->Add(sts_row, 0, wxLEFT | wxGROW, lay.indent);
	sts_row->Add(new wxStaticText(&parent, nullID, _("Ro&le ARN:")), lay.valign);
	sts_row->Add(new wxTextCtrlEx(&parent, XRCID("ID_S3_ROLE_ARN")), lay.valigng);
	sts_row->Add(new wxStaticText(&parent, nullID, _("MFA D&evice Serial:")), lay.valign);
	sts_row->Add(new wxTextCtrlEx(&parent, XRCID("ID_S3_MFA_SERIAL")), lay.valigng);
	sts_sizer_->Show(false);

	sso_sizer_ = lay.createFlex(1);
	sso_sizer_->AddGrowableCol(0);
	sizer.Add(sso_sizer_, lay.grow);
	sso_sizer_->Add(new wxStaticText(&parent, nullID, _("S3 via IAM Identity Center (formerly SSO):")));

	auto * sso_row = lay.createFlex(2);
	sso_row->AddGrowableCol(1);
	sso_sizer_->Add(sso_row, 0, wxLEFT | wxGROW, lay.indent);
	sso_row->Add(new wxStaticText(&parent, nullID, _("Regio&n:")), lay.valign);
	sso_row->Add(new wxTextCtrlEx(&parent, XRCID("ID_S3_SSO_REGION")), lay.valigng);
	sso_row->Add(new wxStaticText(&parent, nullID, _("Ro&le name:")), lay.valign);
	sso_row->Add(new wxTextCtrlEx(&parent, XRCID("ID_S3_SSO_ROLE")), lay.valigng);
	sso_row->Add(new wxStaticText(&parent, nullID, _("Start &URL:")), lay.valign);
	sso_row->Add(new wxTextCtrlEx(&parent, XRCID("ID_S3_SSO_URL")), lay.valigng);

	auto l = [this](wxEvent const&) { SetControlState(false); };
	none->Bind(wxEVT_RADIOBUTTON, l);
	aes->Bind(wxEVT_RADIOBUTTON, l);
	kms->Bind(wxEVT_RADIOBUTTON, l);
	customer->Bind(wxEVT_RADIOBUTTON, l);
	choice->Bind(wxEVT_CHOICE, l);
}

void S3SiteControls::SetControlState(bool predefined)
{
	bool enableKey{};
	bool enableKMS{};
	bool enableCustomer{};
	if (xrc_call(parent_, "ID_S3_AWSKMS", &wxRadioButton::GetValue)) {
		enableKey = true;
		if (xrc_call(parent_, "ID_S3_KMSKEY", &wxChoice::GetSelection) == static_cast<int>(s3_sse::KmsKey::CUSTOM)) {
			enableKMS = true;
		}
	}
	else if (xrc_call(parent_, "ID_S3_CUSTOMER_ENCRYPTION", &wxRadioButton::GetValue)) {
		enableCustomer = true;
	}
	xrc_call(parent_, "ID_S3_KMSKEY", &wxWindow::Enable, !predefined && enableKey);
	xrc_call(parent_, "ID_S3_CUSTOM_KMS", &wxWindow::Enable, !predefined && enableKMS);
	xrc_call(parent_, "ID_S3_CUSTOMER_KEY", &wxWindow::Enable, !predefined && enableCustomer);
}

void S3SiteControls::SetSite(Site const& site, bool predefined)
{
	xrc_call(parent_, "ID_S3_KMSKEY", &wxWindow::Enable, !predefined);
	xrc_call(parent_, "ID_S3_NOENCRYPTION", &wxWindow::Enable, !predefined);
	xrc_call(parent_, "ID_S3_AES256", &wxWindow::Enable, !predefined);
	xrc_call(parent_, "ID_S3_AWSKMS", &wxWindow::Enable, !predefined);
	xrc_call(parent_, "ID_S3_CUSTOMER_ENCRYPTION", &wxWindow::Enable, !predefined);

	auto& server = site.server;
	auto const protocol = server.GetProtocol();

	if (protocol == S3 || protocol == S3_SSO) {
		auto region = site.server.GetExtraParameter("region");
		xrc_call(parent_, "ID_S3_REGION", &wxTextCtrl::ChangeValue, region);

		auto accelerate = site.server.GetExtraParameter("accelerate");
		xrc_call(parent_, "ID_S3_ACCELERATE", &wxCheckBox::SetValue, accelerate == L"1");

		xrc_call(parent_, "ID_S3_KMSKEY", &wxChoice::SetSelection, static_cast<int>(s3_sse::KmsKey::DEFAULT));
		auto sse_algorithm = site.server.GetExtraParameter("ssealgorithm");
		if (sse_algorithm.empty()) {
			xrc_call(parent_, "ID_S3_NOENCRYPTION", &wxRadioButton::SetValue, true);
		}
		else if (sse_algorithm == "AES256") {
			xrc_call(parent_, "ID_S3_AES256", &wxRadioButton::SetValue, true);
		}
		else if (sse_algorithm == "aws:kms") {
			xrc_call(parent_, "ID_S3_AWSKMS", &wxRadioButton::SetValue, true);
			auto sse_kms_key = site.server.GetExtraParameter("ssekmskey");
			if (!sse_kms_key.empty()) {
				xrc_call(parent_, "ID_S3_KMSKEY", &wxChoice::SetSelection, static_cast<int>(s3_sse::KmsKey::CUSTOM));
				xrc_call(parent_, "ID_S3_CUSTOM_KMS", &wxTextCtrl::ChangeValue, sse_kms_key);
			}
			else {
				xrc_call(parent_, "ID_S3_KMSKEY", &wxChoice::SetSelection, static_cast<int>(s3_sse::KmsKey::DEFAULT));
			}
		}
		else if (sse_algorithm == "customer") {
			xrc_call(parent_, "ID_S3_CUSTOMER_ENCRYPTION", &wxRadioButton::SetValue, true);
			auto customer_key = server.GetExtraParameter("ssecustomerkey");
			xrc_call(parent_, "ID_S3_CUSTOMER_KEY", &wxTextCtrl::ChangeValue, customer_key);
		}

		if (protocol == S3) {
			auto value = site.server.GetExtraParameter("stsrolearn");
			xrc_call(parent_, "ID_S3_ROLE_ARN", &wxTextCtrl::ChangeValue, value);
			value = server.GetExtraParameter("stsmfaserial");
			xrc_call(parent_, "ID_S3_MFA_SERIAL", &wxTextCtrl::ChangeValue, value);
		}
		if (protocol == S3_SSO) {
			auto value = server.GetExtraParameter("ssoregion");
			xrc_call(parent_, "ID_S3_SSO_REGION", &wxTextCtrl::ChangeValue, value);
			value = server.GetExtraParameter("ssorole");
			xrc_call(parent_, "ID_S3_SSO_ROLE", &wxTextCtrl::ChangeValue, value);
			value = server.GetExtraParameter("ssourl");
			xrc_call(parent_, "ID_S3_SSO_URL", &wxTextCtrl::ChangeValue, value);
		}
	}
}

void S3SiteControls::SetControlVisibility(ServerProtocol protocol, LogonType)
{
	sts_sizer_->Show(protocol == S3);
	sso_sizer_->Show(protocol == S3_SSO);

	auto * s = sts_sizer_->GetContainingWindow()->GetSizer();
	s->Layout();
	s->CalcMin();
}

bool S3SiteControls::UpdateSite(Site & site, bool silent)
{
	auto& server = site.server;
	auto const protocol = server.GetProtocol();

	if (protocol == S3 || protocol == S3_SSO) {
		server.SetExtraParameter("region", fz::to_wstring(xrc_call(parent_, "ID_S3_REGION", &wxTextCtrl::GetValue)));

		if (xrc_call(parent_, "ID_S3_ACCELERATE", &wxCheckBox::GetValue)) {
			server.SetExtraParameter("accelerate", L"1");
		}
		else {
			server.ClearExtraParameter("accelerate");
		}

		if (xrc_call(parent_, "ID_S3_NOENCRYPTION", &wxRadioButton::GetValue)) {
			server.ClearExtraParameter("ssealgorithm");
		}
		else if (xrc_call(parent_, "ID_S3_AES256", &wxRadioButton::GetValue)) {
			server.SetExtraParameter("ssealgorithm", L"AES256");
		}
		else if (xrc_call(parent_, "ID_S3_AWSKMS", &wxRadioButton::GetValue)) {
			server.SetExtraParameter("ssealgorithm", L"aws:kms");
			if (xrc_call(parent_, "ID_S3_KMSKEY", &wxChoice::GetSelection) == static_cast<int>(s3_sse::KmsKey::CUSTOM)) {
				auto keyId = xrc_call(parent_, "ID_S3_CUSTOM_KMS", &wxTextCtrl::GetValue).ToStdWstring();
				if (keyId.empty()) {
					if (!silent) {
						xrc_call(parent_, "ID_S3_CUSTOM_KMS", &wxWindow::SetFocus);
						wxMessageBoxEx(_("Custom KMS ARN id cannot be empty."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
					}
					return false;
				}
				server.SetExtraParameter("ssekmskey", fz::to_wstring(xrc_call(parent_, "ID_S3_CUSTOM_KMS", &wxTextCtrl::GetValue)));
			}
		}
		else if (xrc_call(parent_, "ID_S3_CUSTOMER_ENCRYPTION", &wxRadioButton::GetValue)) {
			auto keyId = xrc_call(parent_, "ID_S3_CUSTOMER_KEY", &wxTextCtrl::GetValue).ToStdString();
			if (keyId.empty()) {
				if (!silent) {
					xrc_call(parent_, "ID_S3_CUSTOMER_KEY", &wxWindow::SetFocus);
					wxMessageBoxEx(_("Custom key cannot be empty."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
				}
				return false;
			}

			std::string const base64prefix = "base64:";
			if (fz::starts_with(keyId, base64prefix)) {
				keyId = keyId.substr(base64prefix.size());
				keyId = fz::base64_decode_s(keyId);
			}

			if (keyId.size() != 32) {		// 256-bit encryption key
				if (!silent) {
					xrc_call(parent_, "ID_S3_CUSTOMER_KEY", &wxWindow::SetFocus);
					wxMessageBoxEx(_("Custom key length must be 256-bit."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
				}
				return false;
			}

			server.SetExtraParameter("ssealgorithm", L"customer");
			server.SetExtraParameter("ssecustomerkey", fz::to_wstring(xrc_call(parent_, "ID_S3_CUSTOMER_KEY", &wxTextCtrl::GetValue)));
		}

		auto value = fz::to_wstring(xrc_call(parent_, "ID_S3_ROLE_ARN", &wxTextCtrl::GetValue));
		if (!value.empty()) {
			server.SetExtraParameter("stsrolearn", value);
			value = fz::to_wstring(xrc_call(parent_, "ID_S3_MFA_SERIAL", &wxTextCtrl::GetValue));
			if (!value.empty()) {
				server.SetExtraParameter("stsmfaserial", value);
			}
		}

		if (protocol == S3_SSO) {
			auto role = fz::to_wstring(xrc_call(parent_, "ID_S3_SSO_ROLE", &wxTextCtrl::GetValue));
			auto url = fz::to_wstring(xrc_call(parent_, "ID_S3_SSO_URL", &wxTextCtrl::GetValue));
			if (role.empty() || url.empty()) {
				if (!silent) {
					xrc_call(parent_, "ID_S3_SSO_ROLE", &wxWindow::SetFocus);
					wxMessageBoxEx(_("You have to enter both role name and start URL."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
				}
				return false;
			}
			server.SetExtraParameter("ssorole", role);
			server.SetExtraParameter("ssourl", url);

			value = fz::to_wstring(xrc_call(parent_, "ID_S3_SSO_REGION", &wxTextCtrl::GetValue));
			if (!value.empty()) {
				server.SetExtraParameter("ssoregion", value);
			}
		}
	}

	return true;
}

SwiftSiteControls::SwiftSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer)
	: SiteControls(parent)
{
	if (!sizer.IsColGrowable(0)) {
		sizer.AddGrowableCol(0);
	}

	sizer.Add(new wxStaticText(&parent, nullID, _("Identity (Keystone):")));

	auto keystone3 = new wxCheckBox(&parent, XRCID("ID_SWIFT_KEYSTONE_V3"), _("&Version 3"));
	sizer.Add(keystone3);

	auto *keyRow = lay.createFlex(2);
	keyRow->AddGrowableCol(1);
	sizer.Add(keyRow, 0, wxLEFT|wxGROW, lay.indent);
	keyRow->Add(new wxStaticText(&parent, nullID, _("&Domain:")), lay.valign);
	keyRow->Add(new wxTextCtrlEx(&parent, XRCID("ID_SWIFT_DOMAIN")), lay.valigng);

	auto l = [this](wxEvent const&) { SetControlState(false); };
	keystone3->Bind(wxEVT_CHECKBOX, l);
}

void SwiftSiteControls::SetControlState(bool predefined)
{
	auto v3 = xrc_call(parent_, "ID_SWIFT_KEYSTONE_V3", &wxCheckBox::GetValue);

	xrc_call(parent_, "ID_SWIFT_DOMAIN", &wxWindow::Enable, !predefined && v3);
}

void SwiftSiteControls::SetSite(Site const& site, bool)
{
	if (site.server.GetProtocol() == SWIFT) {
		bool v3{};
		auto pv3 = site.server.GetExtraParameter("keystone_version");
		if (pv3.empty()) {
			v3 = fz::starts_with(site.server.GetExtraParameter("identpath"), std::wstring(L"/v3"));
		}
		else {
			v3 = pv3 == L"3";
		}
		xrc_call(parent_, "ID_SWIFT_KEYSTONE_V3", &wxCheckBox::SetValue, v3);
		auto domain = site.server.GetExtraParameter("domain");
		if (domain.empty()) {
			domain = L"Default";
		}
		xrc_call(parent_, "ID_SWIFT_DOMAIN", &wxTextCtrl::ChangeValue, domain);
	}
}

bool SwiftSiteControls::UpdateSite(Site & site, bool)
{
	CServer &server = site.server;
	if (server.GetProtocol() == SWIFT) {
		auto v3 = xrc_call(parent_, "ID_SWIFT_KEYSTONE_V3", &wxCheckBox::GetValue);
		if (v3) {
			server.SetExtraParameter("keystone_version", L"3");
		}
		else {
			server.SetExtraParameter("keystone_version", L"2");
		}

		if (v3) {
			auto domain = fz::to_wstring(xrc_call(parent_, "ID_SWIFT_DOMAIN", &wxTextCtrl::GetValue));
			server.SetExtraParameter("domain", domain);
		}
		else {
			server.ClearExtraParameter("domain");
		}
	}

	return true;
}

DropboxSiteControls::DropboxSiteControls(wxWindow & parent, DialogLayout const&, wxFlexGridSizer & sizer)
	: SiteControls(parent)
{
	if (!sizer.IsColGrowable(0)) {
		sizer.AddGrowableCol(0);
	}

	sizer.Add(new wxStaticText(&parent, nullID, _("Dropbox for Business:")));

	auto root_ns = new wxCheckBox(&parent, XRCID("ID_USE_ROOT_NS"), _("Use &team root namespace"));
	sizer.Add(root_ns);
}

void DropboxSiteControls::SetSite(Site const& site, bool predefined)
{
	if (site.server.GetProtocol() == DROPBOX) {
		auto root_ns = site.server.GetExtraParameter("root_namespace");
		xrc_call(parent_, "ID_USE_ROOT_NS", &wxCheckBox::SetValue, root_ns == L"1");
	}
	xrc_call(parent_, "ID_USE_ROOT_NS", &wxCheckBox::Enable, !predefined);
}

bool DropboxSiteControls::UpdateSite(Site & site, bool)
{
	CServer & server = site.server;
	if (server.GetProtocol() == DROPBOX) {
		if (xrc_call(parent_, "ID_USE_ROOT_NS", &wxCheckBox::GetValue)) {
			server.SetExtraParameter("root_namespace", L"1");
		}
		else {
			server.ClearExtraParameter("root_namespace");
		}
	}

	return true;
}
