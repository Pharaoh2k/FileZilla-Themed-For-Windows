#include "filezilla.h"
#include "led.h"
#include "Options.h"
#if ENABLE_SFTP
#include "sftp_crypt_info_dlg.h"
#endif
#include "speedlimits_dialog.h"
#include "statusbar.h"
#include "themeprovider.h"
#include "verifycertdialog.h"

#include "../include/activity_logger.h"
#include "../include/sizeformatting.h"

#include <libfilezilla/glue/wxinvoker.hpp>

#include <wx/dcclient.h>
#include <wx/menu.h>

#include <algorithm>

static const int statbarWidths[3] = {
	-1, 0, 0
};
#define FIELD_QUEUESIZE 1

BEGIN_EVENT_TABLE(wxStatusBarEx, wxStatusBar)
EVT_SIZE(wxStatusBarEx::OnSize)
END_EVENT_TABLE()

wxStatusBarEx::wxStatusBarEx(wxTopLevelWindow* pParent)
{
	m_pParent = pParent;
	m_columnWidths = 0;

	SetBackgroundStyle(wxBG_STYLE_SYSTEM);

	Create(pParent, wxID_ANY);

#ifdef __WXMSW__
	m_parentWasMaximized = false;

	if (GetLayoutDirection() != wxLayout_RightToLeft) {
		SetDoubleBuffered(true);
	}
#endif
}

wxStatusBarEx::~wxStatusBarEx()
{
	delete [] m_columnWidths;
}

void wxStatusBarEx::SetFieldsCount(int number, const int* widths)
{
	wxASSERT(number > 0);

	int oldCount = GetFieldsCount();
	int* oldWidths = m_columnWidths;

	m_columnWidths = new int[number];
	if (!widths) {
		if (oldWidths) {
			const int min = std::min(oldCount, number);
			for (int i = 0; i < min; ++i) {
				m_columnWidths[i] = oldWidths[i];
			}
			for (int i = min; i < number; ++i) {
				m_columnWidths[i] = -1;
			}
			delete [] oldWidths;
		}
		else {
			for (int i = 0; i < number; ++i) {
				m_columnWidths[i] = -1;
			}
		}
	}
	else {
		delete [] oldWidths;
		for (int i = 0; i < number; ++i) {
			m_columnWidths[i] = widths[i];
		}

		FixupFieldWidth(number - 1);
	}

	wxStatusBar::SetFieldsCount(number, m_columnWidths);
}

void wxStatusBarEx::SetStatusWidths(int n, const int *widths)
{
	wxASSERT(n == GetFieldsCount());
	wxASSERT(widths);
	for (int i = 0; i < n; ++i) {
		m_columnWidths[i] = widths[i];
	}
	m_columnWidths[n - 1] += CThemeProvider::GetIconSize(iconSizeSmall).GetWidth();
#ifdef __WXMSW__
	m_columnWidths[n - 1] -= 18; // Internal magic constant of wx, it doesn't UI scale :(
#endif

	FixupFieldWidth(n - 1);

	wxStatusBar::SetStatusWidths(n, m_columnWidths);
}

void wxStatusBarEx::FixupFieldWidth(int field)
{
	if (field != GetFieldsCount() - 1) {
		return;
	}

#if __WXGTK20__
	// Gripper overlaps last all the time
	if (m_columnWidths[field] > 0) {
		m_columnWidths[field] += 15;
	}
#endif
}

void wxStatusBarEx::SetFieldWidth(int field, int width)
{
	field = GetFieldIndex(field);
	if (field < 0) {
		return;
	}

	m_columnWidths[field] = width;
	FixupFieldWidth(field);
	wxStatusBar::SetStatusWidths(GetFieldsCount(), m_columnWidths);
}

int wxStatusBarEx::GetFieldIndex(int field)
{
	if (field >= 0) {
		wxCHECK(field <= GetFieldsCount(), -1);
	}
	else {
		field = GetFieldsCount() + field;
		wxCHECK(field >= 0, -1);
	}

	return field;
}

void wxStatusBarEx::OnSize(wxSizeEvent&)
{
#ifdef __WXMSW__
	const int count = GetFieldsCount();
	if (count && m_columnWidths && m_columnWidths[count - 1] > 0) {
		// No sizegrip on maximized windows
		bool isMaximized = m_pParent->IsMaximized();
		if (isMaximized != m_parentWasMaximized) {
			m_parentWasMaximized = isMaximized;

			if (isMaximized) {
				m_columnWidths[count - 1] -= CThemeProvider::GetIconSize(iconSizeSmall).GetWidth();
			}
			else {
				m_columnWidths[count - 1] += CThemeProvider::GetIconSize(iconSizeSmall).GetWidth();
			}

			wxStatusBar::SetStatusWidths(count, m_columnWidths);
			Refresh();
		}
	}
#endif
}

#ifdef __WXGTK__
void wxStatusBarEx::SetStatusText(const wxString& text, int number)
{
	wxString oldText = GetStatusText(number);
	if (oldText != text) {
		wxStatusBar::SetStatusText(text, number);
	}
}
#endif

int wxStatusBarEx::GetGripperWidth()
{
#if defined(__WXMSW__)
	return m_pParent->IsMaximized() ? 0 : 6;
#elif defined(__WXGTK__)
	return 15;
#else
	return 0;
#endif
}


BEGIN_EVENT_TABLE(CWidgetsStatusBar, wxStatusBarEx)
EVT_SIZE(CWidgetsStatusBar::OnSize)
END_EVENT_TABLE()

CWidgetsStatusBar::CWidgetsStatusBar(wxTopLevelWindow* parent)
	: wxStatusBarEx(parent)
{
}

CWidgetsStatusBar::~CWidgetsStatusBar()
{
}

void CWidgetsStatusBar::OnSize(wxSizeEvent& event)
{
	wxStatusBarEx::OnSize(event);

	for (int i = 0; i < GetFieldsCount(); ++i) {
		PositionChildren(i);
	}

#ifdef __WXMSW__
	if (GetLayoutDirection() != wxLayout_RightToLeft) {
		Update();
	}
#endif
}

bool CWidgetsStatusBar::AddField(int field, int idx, wxWindow* pChild)
{
	field = GetFieldIndex(field);
	if (field < 0) {
		return false;
	}

	t_statbar_child data;
	data.field = field;
	data.pChild = pChild;
	m_children[idx] = data;

	if (statbarWidths[field] >= 0) {
		SetFieldWidth(field, GetStatusWidth(field) + pChild->GetSize().GetWidth() + 3);
	}

	PositionChildren(field);

	return true;
}

void CWidgetsStatusBar::RemoveField(int idx)
{
	auto iter = m_children.find(idx);
	if (iter != m_children.end()) {
		int field = iter->second.field;
		m_children.erase(iter);
		PositionChildren(field);
	}
}

void CWidgetsStatusBar::PositionChildren(int field)
{
	wxRect rect;
	GetFieldRect(field, rect);
	int offset = 3;

#ifndef __WXMSW__
	if (field + 1 == GetFieldsCount()) {
		rect.SetWidth(m_columnWidths[field]);
		offset += 5 + GetGripperWidth();
	}
#endif

	for (auto iter = m_children.begin(); iter != m_children.end(); ++iter) {
		if (iter->second.field != field) {
			continue;
		}

		const wxSize size = iter->second.pChild->GetSize();
		int position = rect.GetRight() - size.x - offset;

		iter->second.pChild->SetPosition(wxPoint(position, rect.GetTop() + (rect.GetHeight() - size.y + 1) / 2));

		offset += size.x + 3;
	}
}

void CWidgetsStatusBar::SetFieldWidth(int field, int width)
{
	wxStatusBarEx::SetFieldWidth(field, width);
	for (int i = 0; i < GetFieldsCount(); ++i) {
		PositionChildren(i);
	}
}

class CIndicator final : public wxStaticBitmap
{
public:
	CIndicator(CStatusBar* pStatusBar, const wxBitmap& bmp, wxSize const& size)
		: wxStaticBitmap(pStatusBar, wxID_ANY, MakeBmpBundle(bmp), wxDefaultPosition, size)
	{
		m_pStatusBar = pStatusBar;
	}

protected:
	CStatusBar* m_pStatusBar;

	DECLARE_EVENT_TABLE()
	void OnLeftMouseUp(wxMouseEvent&)
	{
		m_pStatusBar->OnHandleLeftClick(this);
	}
	void OnRightMouseUp(wxMouseEvent&)
	{
		m_pStatusBar->OnHandleRightClick(this);
	}
};

BEGIN_EVENT_TABLE(CIndicator, wxStaticBitmap)
EVT_LEFT_UP(CIndicator::OnLeftMouseUp)
EVT_RIGHT_UP(CIndicator::OnRightMouseUp)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(CStatusBar, CWidgetsStatusBar)
EVT_MENU(XRCID("ID_SPEEDLIMITCONTEXT_ENABLE"), CStatusBar::OnSpeedLimitsEnable)
EVT_MENU(XRCID("ID_SPEEDLIMITCONTEXT_CONFIGURE"), CStatusBar::OnSpeedLimitsConfigure)
EVT_TIMER(wxID_ANY, CStatusBar::OnTimer)
END_EVENT_TABLE()

CStatusBar::CStatusBar(wxTopLevelWindow* pParent, activity_logger& al, COptionsBase& options, TimeFormatter & time_formatter)
	: CWidgetsStatusBar(pParent)
	, COptionChangeEventHandler(this)
	, options_(options)
	, time_formatter_(time_formatter)
	, activity_logger_(al)
{
	// Speedlimits
	options_.watch(OPTION_SPEEDLIMIT_ENABLE, this);
	options_.watch(OPTION_SPEEDLIMIT_INBOUND, this);
	options_.watch(OPTION_SPEEDLIMIT_OUTBOUND, this);

	// Size format
	options_.watch(OPTION_SIZE_FORMAT, this);
	options_.watch(OPTION_SIZE_USETHOUSANDSEP, this);
	options_.watch(OPTION_SIZE_DECIMALPLACES, this);

	options_.watch(OPTION_ASCIIBINARY, this);

	// Reload icons
	options_.watch(OPTION_ICONS_THEME, this);

	CContextManager::Get()->RegisterHandler(this, STATECHANGE_SERVER, true);
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_CHANGEDCONTEXT, false);
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_ENCRYPTION, true);

	const int count = 3;
	SetFieldsCount(count);
	int array[count];
	array[0] = wxSB_FLAT;
	array[1] = wxSB_NORMAL;
	array[2] = wxSB_FLAT;
	SetStatusStyles(count, array);

	SetStatusWidths(count, statbarWidths);

	UpdateSizeFormat();

	activityLeds_[0] = new CLed(this, 0);
	activityLeds_[1] = new CLed(this, 1);
	activityLeds_[0]->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { UpdateActivityTooltip(); });
	activityLeds_[1]->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { UpdateActivityTooltip(); });

	AddField(-1, widget_led_recv, activityLeds_[1]);
	AddField(-1, widget_led_send, activityLeds_[0]);

	UpdateSpeedLimitsIcon();
	DisplayDataType();
	DisplayEncrypted();

	m_queue_size_timer.SetOwner(this);

	activity_logger_.set_notifier(fz::make_invoker(*this, [this]() { OnActivity(); }));

	activityTimer_.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { OnActivity(); });
}

CStatusBar::~CStatusBar()
{
	options_.unwatch_all(this);
}

void CStatusBar::DisplayQueueSize(int64_t totalSize, bool hasUnknown)
{
	m_size = totalSize;
	m_hasUnknownFiles = hasUnknown;

	if (m_queue_size_timer.IsRunning()) {
		m_queue_size_changed = true;
	}
	else {
		DoDisplayQueueSize();
		m_queue_size_timer.Start(200, true);
	}
}

void CStatusBar::DoDisplayQueueSize()
{
	m_queue_size_changed = false;
	if (m_size == 0 && !m_hasUnknownFiles) {
		SetStatusText(_("Queue: empty"), FIELD_QUEUESIZE);
		return;
	}

	wxString queueSize = wxString::Format(_("Queue: %s%s"), m_hasUnknownFiles ? _T(">") : _T(""),
	    SizeFormatter::Format(m_size, true, m_sizeFormat, m_sizeFormatThousandsSep, m_sizeFormatDecimalPlaces));

	SetStatusText(queueSize, FIELD_QUEUESIZE);
}

void CStatusBar::DisplayDataType()
{
	Site site;
	CState const* pState = CContextManager::Get()->GetCurrentContext();
	if (pState) {
		site = pState->GetSite();
	}

	if (!site || !CServer::ProtocolHasFeature(site.server.GetProtocol(), ProtocolFeature::DataTypeConcept)) {
		if (m_pDataTypeIndicator) {
			RemoveField(widget_datatype);
			m_pDataTypeIndicator->Destroy();
			m_pDataTypeIndicator = 0;
		}
	}
	else {
		wxString name;
		wxString desc;

		const int type = options_.get_int(OPTION_ASCIIBINARY);
		if (type == 1) {
			name = _T("ART_ASCII");
			desc = _("Current transfer type is set to ASCII.");
		}
		else if (type == 2) {
			name = _T("ART_BINARY");
			desc = _("Current transfer type is set to binary.");
		}
		else {
			name = _T("ART_AUTO");
			desc = _("Current transfer type is set to automatic detection.");
		}

		wxSize const s = CThemeProvider::GetIconSize(iconSizeSmall);
		wxBitmap bmp = CThemeProvider::Get()->CreateBitmap(name, wxART_OTHER, s);
		SetFieldBitmap(widget_datatype, m_pDataTypeIndicator, bmp, s);
		m_pDataTypeIndicator->SetToolTip(desc);
	}
}

void CStatusBar::MeasureQueueSizeWidth()
{
	wxClientDC dc(this);
	dc.SetFont(GetFont());

	wxSize s = dc.GetTextExtent(_("Queue: empty"));

	wxString tmp = _T(">8888");
	if (m_sizeFormatDecimalPlaces) {
		tmp += _T(".");
		for (int i = 0; i < m_sizeFormatDecimalPlaces; ++i) {
			tmp += _T("8");
		}
	}
	s.IncTo(dc.GetTextExtent(wxString::Format(_("Queue: %s MiB"), tmp)));

	SetFieldWidth(FIELD_QUEUESIZE, s.x + 10);
}

void CStatusBar::DisplayEncrypted()
{
	Site site;
	CState *const pState = CContextManager::Get()->GetCurrentContext();
	if (pState) {
		site = pState->GetSite();
	}

	bool encrypted = false;
	if (site) {
		auto const protocol = site.server.GetProtocol();
		if (protocol == FTPS || protocol == FTPES || protocol == SFTP || protocol == S3 ||
				protocol == WEBDAV || protocol == AZURE_BLOB || protocol == AZURE_FILE ||
				protocol == SWIFT || protocol == GOOGLE_CLOUD || protocol == GOOGLE_DRIVE ||
				protocol == DROPBOX || protocol == ONEDRIVE || protocol == B2 || protocol == BOX ||
				protocol == RACKSPACE) {
			encrypted = true;
		}
		else if (protocol == FTP && pState->GetTlsSessionInfo()) {
			encrypted = true;
		}
	}

	if (!encrypted) {
		if (m_pEncryptionIndicator) {
			RemoveField(widget_encryption);
			m_pEncryptionIndicator->Destroy();
			m_pEncryptionIndicator = 0;
		}
	}
	else {
		wxSize const s = CThemeProvider::GetIconSize(iconSizeSmall);
		wxBitmap bmp = CThemeProvider::Get()->CreateBitmap(L"ART_LOCK", wxART_OTHER, s);
		SetFieldBitmap(widget_encryption, m_pEncryptionIndicator, bmp, s);
		m_pEncryptionIndicator->SetToolTip(_("The connection is encrypted. Click icon for details."));
	}
}

void CStatusBar::UpdateSizeFormat()
{
	// 0 equals bytes, however just use IEC binary prefixes instead,
	// exact byte counts for queue make no sense.
	m_sizeFormat = SizeFormat(options_.get_int(OPTION_SIZE_FORMAT));
	if (m_sizeFormat == SizeFormat::bytes) {
		m_sizeFormat = SizeFormat::iec;
	}

	m_sizeFormatThousandsSep = options_.get_bool(OPTION_SIZE_USETHOUSANDSEP);
	m_sizeFormatDecimalPlaces = options_.get_int(OPTION_SIZE_DECIMALPLACES);

	MeasureQueueSizeWidth();

	DisplayQueueSize(m_size, m_hasUnknownFiles);
}

void CStatusBar::OnHandleLeftClick(wxWindow* pWnd)
{
	if (pWnd == m_pEncryptionIndicator) {
		CState* pState = CContextManager::Get()->GetCurrentContext();
		auto const& tlsInfo = pState->GetTlsSessionInfo();
		if (tlsInfo) {
			CVerifyCertDialog::DisplayCertificate(*tlsInfo, options_, time_formatter_);
			return;
		}
#if ENABLE_SFTP
		auto const& sshInfo = pState->GetSshSessionInfo();
		if (sshInfo) {
			CSftpEncryptioInfoDialog dlg;
			dlg.ShowDialog(sshInfo->first, sshInfo->second);
			return;
		}
#endif
		wxMessageBoxEx(_("Certificate and session data are not available yet."), _("Security information"));
	}
	else if (pWnd == m_pSpeedLimitsIndicator) {
		CSpeedLimitsDialog dlg(options_);
		dlg.Run(m_pParent);
	}
	else if (pWnd == m_pDataTypeIndicator) {
		ShowDataTypeMenu();
	}
}

void CStatusBar::OnHandleRightClick(wxWindow* pWnd)
{
	if (pWnd == m_pDataTypeIndicator) {
		ShowDataTypeMenu();
	}
	else if (pWnd == m_pSpeedLimitsIndicator) {

		int downloadlimit = options_.get_int(OPTION_SPEEDLIMIT_INBOUND);
		int uploadlimit = options_.get_int(OPTION_SPEEDLIMIT_OUTBOUND);
		bool enable = options_.get_int(OPTION_SPEEDLIMIT_ENABLE) != 0;
		if (!downloadlimit && !uploadlimit) {
			enable = false;
		}

		wxMenu menu;
		menu.Append(XRCID("ID_SPEEDLIMITCONTEXT_ENABLE"), _("&Enable"), wxString(), wxITEM_CHECK)->Check(enable);
		menu.Append(XRCID("ID_SPEEDLIMITCONTEXT_CONFIGURE"), _("&Configure speed limits..."));

		PopupMenu(&menu);
	}
}

void CStatusBar::ShowDataTypeMenu()
{
	wxMenu menu;
	menu.Append(XRCID("ID_MENU_TRANSFER_TYPE_AUTO"), _("&Auto"), wxString(), wxITEM_RADIO);
	menu.Append(XRCID("ID_MENU_TRANSFER_TYPE_ASCII"), _("A&SCII"), wxString(), wxITEM_RADIO);
	menu.Append(XRCID("ID_MENU_TRANSFER_TYPE_BINARY"), _("&Binary"), wxString(), wxITEM_RADIO);

	const int type = options_.get_int(OPTION_ASCIIBINARY);
	switch (type)
	{
	case 1:
		menu.Check(XRCID("ID_MENU_TRANSFER_TYPE_ASCII"), true);
		break;
	case 2:
		menu.Check(XRCID("ID_MENU_TRANSFER_TYPE_BINARY"), true);
		break;
	default:
		menu.Check(XRCID("ID_MENU_TRANSFER_TYPE_AUTO"), true);
		break;
	}

	PopupMenu(&menu);
}

void CStatusBar::UpdateSpeedLimitsIcon()
{
	bool enable = options_.get_int(OPTION_SPEEDLIMIT_ENABLE) != 0;

	auto const s = CThemeProvider::GetIconSize(iconSizeSmall);
	wxBitmap bmp = CThemeProvider::Get()->CreateBitmap(L"ART_SPEEDLIMITS", wxART_OTHER, s);
	if (!bmp.Ok()) {
		return;
	}
	wxString tooltip;
	int downloadLimit = options_.get_int(OPTION_SPEEDLIMIT_INBOUND);
	int uploadLimit = options_.get_int(OPTION_SPEEDLIMIT_OUTBOUND);
	if (!enable || (!downloadLimit && !uploadLimit)) {
		wxImage img = bmp.ConvertToImage();
		img = img.ConvertToGreyscale();
#ifdef __WXMAC__
		bmp = wxBitmap(img, -1, bmp.GetScaleFactor());
#else
		bmp = wxBitmap(img);
#endif
		tooltip = _("Speed limits are disabled, click to change.");
	}
	else {
		tooltip = _("Speed limits are enabled, click to change.");
		tooltip += _T("\n");
		if (downloadLimit) {
			tooltip += wxString::Format(_("Download limit: %s/s"), SizeFormatter(options_).Format(downloadLimit, UnitPrefix::kilo));
		}
		else {
			tooltip += _("Download limit: none");
		}
		tooltip += _T("\n");
		if (uploadLimit) {
			tooltip += wxString::Format(_("Upload limit: %s/s"), SizeFormatter(options_).Format(uploadLimit, UnitPrefix::kilo));
		}
		else {
			tooltip += _("Upload limit: none");
		}
	}

	SetFieldBitmap(widget_speedlimit, m_pSpeedLimitsIndicator, bmp, s);
	m_pSpeedLimitsIndicator->SetToolTip(tooltip);
}

void CStatusBar::OnOptionsChanged(watched_options const& options)
{
	if (options.test(OPTION_SPEEDLIMIT_ENABLE) || options.test(OPTION_SPEEDLIMIT_INBOUND) || options.test(OPTION_SPEEDLIMIT_OUTBOUND)) {
		UpdateSpeedLimitsIcon();
	}
	if (options.test(OPTION_SIZE_FORMAT) || options.test(OPTION_SIZE_USETHOUSANDSEP) || options.test(OPTION_SIZE_DECIMALPLACES)) {
		UpdateSizeFormat();
	}
	if (options.test(OPTION_ASCIIBINARY)) {
		DisplayDataType();
	}
	if (options.test(OPTION_ICONS_THEME)) {
		DisplayDataType();
		UpdateSpeedLimitsIcon();
		DisplayEncrypted();
	}
}

void CStatusBar::OnStateChange(CState*, t_statechange_notifications notification, std::wstring const&, const void*)
{
	if (notification == STATECHANGE_SERVER || notification == STATECHANGE_CHANGEDCONTEXT) {
		DisplayDataType();
		DisplayEncrypted();
	}
	else if (notification == STATECHANGE_ENCRYPTION) {
		DisplayEncrypted();
	}
}

void CStatusBar::OnSpeedLimitsEnable(wxCommandEvent&)
{
	int downloadlimit = options_.get_int(OPTION_SPEEDLIMIT_INBOUND);
	int uploadlimit = options_.get_int(OPTION_SPEEDLIMIT_OUTBOUND);
	bool enable = options_.get_int(OPTION_SPEEDLIMIT_ENABLE) == 0;
	if (enable) {
		if (!downloadlimit && !uploadlimit) {
			CSpeedLimitsDialog dlg(options_);
			dlg.Run(m_pParent);
		}
		else {
			options_.set(OPTION_SPEEDLIMIT_ENABLE, 1);
		}
	}
	else {
		options_.set(OPTION_SPEEDLIMIT_ENABLE, 0);
	}
}

void CStatusBar::OnSpeedLimitsConfigure(wxCommandEvent&)
{
	CSpeedLimitsDialog dlg(options_);
	dlg.Run(m_pParent);
}

void CStatusBar::OnTimer(wxTimerEvent&)
{
	if (m_queue_size_changed && !m_queue_size_timer.IsRunning()) {
		DoDisplayQueueSize();
		m_queue_size_timer.Start(200, true);
	}
}

void CStatusBar::OnActivity()
{
	auto activity = activity_logger_.extract_amounts();
	activityLeds_[0]->Set(activity.first != 0);
	activityLeds_[1]->Set(activity.second != 0);
	if (activity.first || activity.second) {
		past_activity_[past_activity_index_++] = std::make_pair(fz::monotonic_clock::now(), activity);
		if (past_activity_index_ >= past_activity_.size()) {
			past_activity_index_ = 0;
		}
		if (!activityTimer_.IsRunning()) {
			activityTimer_.Start(100);
		}
	}
	else {
		activityTimer_.Stop();
	}
}


void CStatusBar::UpdateActivityTooltip()
{
	uint64_t speeds[2]{};

	auto const now = fz::monotonic_clock::now();
	for (size_t i = 0; i < past_activity_.size(); ++i) {
		if ((now - past_activity_[i].first) <= fz::duration::from_seconds(2)) {
			speeds[0] += past_activity_[i].second.first;
			speeds[1] += past_activity_[i].second.second;
		}
	}
	speeds[0] /= 2;
	speeds[1] /= 2;

	auto format = SizeFormat(options_.get_int(OPTION_SIZE_FORMAT));
	if (format == SizeFormat::bytes) {
		format = SizeFormat::iec;
	}


	std::wstring const upSpeed = SizeFormatter::Format(speeds[0], true, format,
	                                                 options_.get_bool(OPTION_SIZE_USETHOUSANDSEP),
													 options_.get_int(OPTION_SIZE_DECIMALPLACES));

	std::wstring const dlSpeed = SizeFormatter::Format(speeds[1], true, format,
	                                                 options_.get_bool(OPTION_SIZE_USETHOUSANDSEP),
													 options_.get_int(OPTION_SIZE_DECIMALPLACES));


	wxString tooltipText;
	tooltipText.Printf(_("Network activity:") + L"\n    " + _("Download: %s/s") + L"\n    " + _("Upload: %s/s"), dlSpeed, upSpeed);

	activityLeds_[0]->SetToolTip(tooltipText);
	activityLeds_[1]->SetToolTip(tooltipText);
}

void CStatusBar::SetFieldBitmap(int field, wxStaticBitmap*& indicator, wxBitmap const& bmp, wxSize const& s)
{
	if (indicator) {
#if defined(__WXMAC__)
		RemoveField(field);
		indicator->Destroy();
		indicator = nullptr;
#else
		indicator->SetBitmap(MakeBmpBundle(bmp));
#endif
	}
	if (!indicator) {
		indicator = new CIndicator(this, bmp, s);
		AddField(0, field, indicator);
	}
}
