#include "filezilla.h"
#include "manual_transfer.h"
#include "../commonui/auto_ascii_files.h"
#include "state.h"
#include "Options.h"
#include "sitemanager.h"
#include "sitemanager_controls.h"
#include "queue.h"
#include "QueueView.h"
#include "textctrlex.h"

#include <libfilezilla/local_filesys.hpp>

#include <wx/filedlg.h>
#include <wx/menu.h>
#include <wx/statbox.h>

struct CManualTransfer::impl
{
	wxRadioButton* download_{};

	wxRadioButton* type_auto_{};
	wxRadioButton* type_ascii_{};
	wxRadioButton* type_binary_{};
	wxStaticText* type_label_{};

	wxTextCtrlEx* local_file_{};
	wxButton* browse_{};

	wxTextCtrlEx* remote_file_{};
	wxTextCtrlEx* remote_path_{};

	wxCheckBox* immediately_{};

	wxRadioButton* server_current_{};
	wxRadioButton* server_site_{};
	wxRadioButton* server_custom_{};
	wxButton* select_site_{};
	wxStaticText* site_name_{};

	std::unique_ptr<GeneralSiteControls> controls_;
};

CManualTransfer::CManualTransfer(COptionsBase& options, CQueueView* pQueueView)
	: impl_(std::make_unique<impl>())
	, options_(options)
	, queue_(pQueueView)
{
}

CManualTransfer::~CManualTransfer()
{
}

void CManualTransfer::Run(wxWindow* parent, CState* pState)
{
	if (!wxDialogEx::Create(parent, nullID, _("Manual transfer"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxSYSTEM_MENU | wxRESIZE_BORDER | wxCLOSE_BOX)) {
		return;
	}

	state_ = pState;


	auto& lay = layout();
	auto main = lay.createMain(this, 1);
	main->AddGrowableCol(0);
	main->AddGrowableRow(4);

	{
		auto [box, inner] = lay.createStatBox(main, _("Transfer &direction"), 2);
		impl_->download_ = new wxRadioButton(box, nullID, _("Download"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		impl_->download_->SetValue(true);
		impl_->download_->SetFocus();
		inner->Add(impl_->download_);
		auto upload = new wxRadioButton(box, nullID, _("Upload"));
		inner->Add(upload);

		impl_->download_->Bind(wxEVT_RADIOBUTTON, &CManualTransfer::OnDirection, this);
		upload->Bind(wxEVT_RADIOBUTTON, &CManualTransfer::OnDirection, this);
	}

	auto file_row = lay.createFlex(2);
	file_row->AddGrowableCol(0);
	file_row->AddGrowableCol(1);
	main->Add(file_row, lay.grow);

	{
		auto [box, inner] = lay.createStatBox(file_row, _("Local file"), 3);
		inner->AddGrowableCol(1);
		inner->Add(new wxStaticText(box, nullID, _("&File:")), lay.valign);
		impl_->local_file_ = new wxTextCtrlEx(box, nullID);
		inner->Add(impl_->local_file_, lay.valigng);
		impl_->browse_ = new wxButton(box, nullID, _("&Browse"));
		inner->Add(impl_->browse_, lay.valign);

		impl_->local_file_->Bind(wxEVT_TEXT, &CManualTransfer::OnLocalChanged, this);
		impl_->browse_->Bind(wxEVT_BUTTON, &CManualTransfer::OnLocalBrowse, this);
	}

	{
		auto [box, inner] = lay.createStatBox(file_row, _("Remote file"), 2);
		inner->AddGrowableCol(1);
		inner->Add(new wxStaticText(box, nullID, _("&Remote path:")), lay.valign);
		impl_->remote_path_ = new wxTextCtrlEx(box, nullID);
		inner->Add(impl_->remote_path_, lay.valigng);
		inner->Add(new wxStaticText(box, nullID, _("Fil&e:")), lay.valign);
		impl_->remote_file_ = new wxTextCtrlEx(box, nullID);
		inner->Add(impl_->remote_file_, lay.valigng);

		impl_->remote_file_->Bind(wxEVT_TEXT, &CManualTransfer::OnRemoteChanged, this);
	}

	auto server_row = lay.createFlex(2);
	server_row->AddGrowableCol(0);
	server_row->AddGrowableCol(1);
	main->Add(server_row, lay.grow);


	{
		auto [box, inner] = lay.createStatBox(server_row, _("&Server"), 1);
		impl_->server_current_ = new wxRadioButton(box, nullID, _("Use server currently connected to"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		impl_->server_site_ = new wxRadioButton(box, nullID, _("Use server from site manager"));
		impl_->server_custom_ = new wxRadioButton(box, nullID, _("Custom server"));
		impl_->select_site_ = new wxButton(box, nullID, _("Sele&ct server"));

		impl_->site_name_ = new wxStaticText(box, nullID, _("None selected yet"));

		inner->Add(impl_->server_current_);
		inner->Add(impl_->server_site_);

		auto row = lay.createFlex(2);
		inner->Add(row, 0, wxLEFT, lay.indent);
		row->Add(new wxStaticText(box, nullID, _("Server:")));
		row->Add(impl_->site_name_);
		inner->Add(impl_->select_site_, 0, wxLEFT, lay.indent);

		inner->Add(impl_->server_custom_);

		impl_->server_current_->Bind(wxEVT_RADIOBUTTON, &CManualTransfer::OnServerTypeChanged, this);
		impl_->server_custom_->Bind(wxEVT_RADIOBUTTON, &CManualTransfer::OnServerTypeChanged, this);
		impl_->server_site_->Bind(wxEVT_RADIOBUTTON, &CManualTransfer::OnServerTypeChanged, this);
		impl_->select_site_->Bind(wxEVT_BUTTON, &CManualTransfer::OnSelectSite, this);
	}

	auto onChange = [this](ServerProtocol p, LogonType t) {
		impl_->controls_->SetControlVisibility(p, t);
		impl_->controls_->SetControlState(false);
		Layout();
		if (GetSizer()->GetMinSize().y > GetClientSize().y) {
			GetSizer()->Fit(this);
			Refresh();
		}
	};
	{
		auto [box, inner] = lay.createStatBox(server_row, _("Custom server"), 2);

		impl_->controls_ = std::make_unique<GeneralSiteControls>(*box, lay, *inner, options_, onChange);
	}

	{
		auto [box, inner] = lay.createStatBox(main, _("D&ata type"), 2);
		impl_->type_auto_ = new wxRadioButton(box, nullID, _("Auto"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		inner->Add(impl_->type_auto_, lay.valign);
		impl_->type_label_ = new wxStaticText(box, nullID, _("Entered file would transfer as ASCII"));
		inner->Add(impl_->type_label_, lay.valign);
		impl_->type_ascii_ = new wxRadioButton(box, nullID, _("ASCII"));
		inner->Add(impl_->type_ascii_);
		inner->AddSpacer(0);
		impl_->type_binary_ = new wxRadioButton(box, nullID, _("Binary"));
		inner->Add(impl_->type_binary_);
		inner->AddSpacer(0);
	}

	impl_->immediately_ = new wxCheckBox(this, nullID, _("Start transfer &immediately"));
	main->Add(impl_->immediately_);

	auto buttons = lay.createButtonSizer(this, main, true);

	auto ok = new wxButton(this, wxID_OK, _("&OK"));
	ok->SetDefault();
	buttons->AddButton(ok);
	ok->Bind(wxEVT_BUTTON, &CManualTransfer::OnOK, this);

	auto cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
	buttons->AddButton(cancel);

	buttons->Realize();

	site_ = state_->GetSite();
	impl_->controls_->SetSite(site_, false);
	auto p = site_.server.GetProtocol();
	onChange(p == UNKNOWN ? FTP : p, site_.credentials.logonType_);
	GetSizer()->Fit(this);

	// Eliminate some jumping when switching protocols
	impl_->server_current_->GetContainingSizer()->SetMinSize(impl_->server_current_->GetContainingSizer()->GetSize());

	SetMinClientSize(GetSizer()->GetMinSize());

	if (site_) {
		impl_->server_current_->SetValue(true);
	}
	else {
		impl_->server_current_->Disable();
		impl_->server_custom_->SetValue(true);
	}

	wxString localPath = state_->GetLocalDir().GetPath();
	impl_->local_file_->ChangeValue(localPath);

	impl_->remote_path_->ChangeValue(state_->GetRemotePath().GetPath());

	SetControlState();

	switch(options_.get_int(OPTION_ASCIIBINARY))
	{
	case 1:
		impl_->type_ascii_->SetValue(true);
		break;
	case 2:
		impl_->type_binary_->SetValue(true);
		break;
	default:
		impl_->type_auto_->SetValue(true);
		break;
	}
	Layout();

	ShowModal();
}

void CManualTransfer::DisplayServer()
{
	impl_->controls_->SetSite(site_, false);
	auto p = site_.server.GetProtocol();
	impl_->controls_->SetControlVisibility(p == UNKNOWN ? FTP : p, site_.credentials.logonType_);
	impl_->controls_->SetControlState(false);
	Layout();
	if (GetSizer()->GetMinSize().y > GetClientSize().y) {
		GetSizer()->Fit(this);
		Refresh();
	}
}

void CManualTransfer::SetControlState()
{
	SetAutoAsciiState();

	impl_->select_site_->Enable(impl_->server_site_->GetValue());
}

void CManualTransfer::SetAutoAsciiState()
{
	Site s;
	impl_->controls_->UpdateSite(s, true);

	std::wstring file;
	if (impl_->download_->GetValue()) {
		file = impl_->remote_file_->GetValue().ToStdWstring();
	}
	else {
		file = impl_->local_file_->GetValue().ToStdWstring();
	}
	if (!s || file.empty()) {
		impl_->type_label_->Hide();
	}
	else {
		if (impl_->download_->GetValue() ? CAutoAsciiFiles::TransferRemoteAsAscii(*COptions::Get(), file, s.server.GetType()) : CAutoAsciiFiles::TransferLocalAsAscii(*COptions::Get(), file, s.server.GetType())) {
			impl_->type_label_->SetLabel(_("Entered file would transfer as ASCII"));
		}
		else {
			impl_->type_label_->SetLabel(_("Entered file would transfer as binary"));
		}
		impl_->type_label_->Show();
	}
}

void CManualTransfer::OnLocalChanged(wxCommandEvent&)
{
	if (!impl_->download_->GetValue()) {
		return;
	}

	wxString file = impl_->local_file_->GetValue();
	local_file_exists_ = fz::local_filesys::get_file_type(fz::to_native(file)) == fz::local_filesys::file;

	SetAutoAsciiState();
}

void CManualTransfer::OnRemoteChanged(wxCommandEvent&)
{
	SetAutoAsciiState();
}

void CManualTransfer::OnLocalBrowse(wxCommandEvent&)
{
	int flags;
	wxString title;
	if (impl_->download_->GetValue()) {
		flags = wxFD_SAVE | wxFD_OVERWRITE_PROMPT;
		title = _("Select target filename");
	}
	else {
		flags = wxFD_OPEN | wxFD_FILE_MUST_EXIST;
		title = _("Select file to upload");
	}

#if FZ_WINDOWS
	wchar_t const* all = L"*.*";
#else
	wchar_t const* all = L"*";
#endif
	wxFileDialog dlg(this, title, _T(""), _T(""), all, flags);
	int res = dlg.ShowModal();

	if (res != wxID_OK) {
		return;
	}

	// SetValue on purpose
	impl_->local_file_->SetValue(dlg.GetPath()); // Intentionally not calling ChangeValue
}

void CManualTransfer::OnDirection(wxCommandEvent& event)
{
	if (impl_->download_->GetValue()) {
		SetAutoAsciiState();
	}
	else {
		// Need to check for file existence
		OnLocalChanged(event);
	}
}

void CManualTransfer::OnServerTypeChanged(wxCommandEvent& event)
{
	if (event.GetEventObject() == impl_->server_current_) {
		site_ = state_->GetSite();
	}
	else if (event.GetEventObject() == impl_->server_site_) {
		site_ = lastSite_;
	}
	impl_->select_site_->Enable(event.GetEventObject() == impl_->server_site_);
	DisplayServer();
}

void CManualTransfer::OnOK(wxCommandEvent&)
{
	if (!impl_->controls_->UpdateSite(site_, false)) {
		return;
	}

	bool const download = impl_->download_->GetValue();

	bool const start = impl_->immediately_->GetValue();

	if (!site_) {
		wxMessageBoxEx(_("You need to specify a server."), _("Manual transfer"), wxICON_EXCLAMATION);
		return;
	}

	std::wstring local_file = impl_->local_file_->GetValue().ToStdWstring();
	if (local_file.empty()) {
		wxMessageBoxEx(_("You need to specify a local file."), _("Manual transfer"), wxICON_EXCLAMATION);
		return;
	}

	fz::local_filesys::type type = fz::local_filesys::get_file_type(fz::to_native(local_file));
	if (type == fz::local_filesys::dir) {
		wxMessageBoxEx(_("Local file is a directory instead of a regular file."), _("Manual transfer"), wxICON_EXCLAMATION);
		return;
	}
	if (!download && type != fz::local_filesys::file && start) {
		wxMessageBoxEx(_("Local file does not exist."), _("Manual transfer"), wxICON_EXCLAMATION);
		return;
	}

	std::wstring remote_file = impl_->remote_file_->GetValue().ToStdWstring();

	if (remote_file.empty()) {
		wxMessageBoxEx(_("You need to specify a remote file."), _("Manual transfer"), wxICON_EXCLAMATION);
		return;
	}

	std::wstring remote_path_str = impl_->remote_path_->GetValue().ToStdWstring();
	if (remote_path_str.empty()) {
		wxMessageBoxEx(_("You need to specify a remote path."), _("Manual transfer"), wxICON_EXCLAMATION);
		return;
	}

	CServerPath path(remote_path_str, site_.server.GetType());
	if (path.empty()) {
		wxMessageBoxEx(_("Remote path could not be parsed."), _("Manual transfer"), wxICON_EXCLAMATION);
		return;
	}

	std::wstring name;
	CLocalPath localPath(local_file, &name);

	if (name.empty()) {
		wxMessageBoxEx(_("Local file is not a valid filename."), _("Manual transfer"), wxICON_EXCLAMATION);
		return;
	}

	int const old_data_type = options_.get_int(OPTION_ASCIIBINARY);

	// Set data type for the file to add
	if (impl_->type_ascii_->GetValue()) {
		options_.set(OPTION_ASCIIBINARY, 1);
	}
	else if (impl_->type_binary_->GetValue()) {
		options_.set(OPTION_ASCIIBINARY, 2);
	}
	else {
		options_.set(OPTION_ASCIIBINARY, 0);
	}

	queue_->QueueFile(!start, download,
		download ? remote_file : name,
		(remote_file != name) ? (download ? name : remote_file) : std::wstring(),
		localPath, path, site_, -1);

	// Restore old data type
	options_.set(OPTION_ASCIIBINARY, old_data_type);

	queue_->QueueFile_Finish(start);

	EndModal(wxID_OK);
}

void CManualTransfer::OnSelectSite(wxCommandEvent&)
{
	std::unique_ptr<wxMenu> pMenu = CSiteManager::GetSitesMenu();
	if (pMenu) {
		pMenu->Bind(wxEVT_MENU, &CManualTransfer::OnSelectedSite, this);
		PopupMenu(pMenu.get());
	}
}

void CManualTransfer::OnSelectedSite(wxCommandEvent& event)
{
	std::unique_ptr<Site> pData = CSiteManager::GetSiteById(event.GetId());
	if (!pData) {
		return;
	}

	site_ = *pData;
	lastSite_ = *pData;

	impl_->site_name_->SetLabel(LabelEscape(site_.GetName()));

	impl_->controls_->SetSite(site_, false);
	impl_->controls_->SetControlVisibility(site_.server.GetProtocol(), site_.credentials.logonType_);
	impl_->controls_->SetControlState(false);
}
