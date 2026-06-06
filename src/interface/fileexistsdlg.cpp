#include "filezilla.h"
#include "fileexistsdlg.h"
#include "file_utils.h"
#include "Options.h"
#include "timeformatting.h"
#include "themeprovider.h"
#include "xrc_helper.h"

#include "../include/sizeformatting.h"

#include <wx/display.h>
#include <wx/statbox.h>

#ifndef __WXMSW__
#include <wx/mimetype.h>
#endif

BEGIN_EVENT_TABLE(CFileExistsDlg, wxDialogEx)
EVT_BUTTON(XRCID("wxID_OK"), CFileExistsDlg::OnOK)
EVT_BUTTON(XRCID("wxID_CANCEL"), CFileExistsDlg::OnCancel)
EVT_CHECKBOX(wxID_ANY, CFileExistsDlg::OnCheck)
END_EVENT_TABLE()

CFileExistsDlg::CFileExistsDlg(CFileExistsNotification *pNotification, COptionsBase & options, TimeFormatter & time_formatter)
	: options_(options)
	, time_formatter_(time_formatter)
{
	m_pNotification = pNotification;
	m_action = CFileExistsNotification::overwrite;
}

bool CFileExistsDlg::Create(wxWindow* parent)
{
	wxASSERT(parent);

	SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
	SetParent(parent);

	if (!wxDialogEx::Create(parent, -1, _("Target file already exists"))) {
		return false;
	}

	auto& lay = layout();
	auto* main = lay.createMain(this, 1);

	auto* inner = lay.createFlex(3);
	main->Add(inner);

	auto* left = lay.createFlex(1);
	inner->Add(left);

	left->Add(new wxStaticText(this, -1, _("The target file already exists.\nPlease choose an action.")));

	left->AddSpacer(lay.dlgUnits(1));

	left->Add(new wxStaticText(this, -1, _("Source file:")));
	left->Add(new wxStaticText(this, XRCID("ID_FILE2_NAME"), wxString()));
	auto* source = lay.createFlex(2);
	left->Add(source);
	source->Add(new wxStaticBitmap(this, XRCID("ID_FILE2_ICON"), wxBitmap()), lay.valign);
	auto* sourceDetails = lay.createFlex(1);
	source->Add(sourceDetails, lay.valign);
	sourceDetails->Add(new wxStaticText(this, XRCID("ID_FILE2_SIZE"), wxString()));
	sourceDetails->Add(new wxStaticText(this, XRCID("ID_FILE2_TIME"), wxString()));

	left->AddSpacer(lay.dlgUnits(1));

	left->Add(new wxStaticText(this, -1, _("Target file:")));
	left->Add(new wxStaticText(this, XRCID("ID_FILE1_NAME"), wxString()));
	auto* target = lay.createFlex(2);
	left->Add(target);
	target->Add(new wxStaticBitmap(this, XRCID("ID_FILE1_ICON"), wxBitmap()), lay.valign);
	auto* targetDetails = lay.createFlex(1);
	target->Add(targetDetails, lay.valign);
	targetDetails->Add(new wxStaticText(this, XRCID("ID_FILE1_SIZE"), wxString()));
	targetDetails->Add(new wxStaticText(this, XRCID("ID_FILE1_TIME"), wxString()));

	inner->AddSpacer(lay.indent);

	auto* right = lay.createFlex(1);
	inner->Add(right);

	auto [box, actions] = lay.createStatBox(right, _("Action:"), 1);

	auto* first = new wxRadioButton(box, XRCID("ID_ACTION1"), _("&Overwrite"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	first->SetValue(true);
	actions->Add(first);
	actions->Add(new wxRadioButton(box, XRCID("ID_ACTION2"), _("Overwrite &if source newer")));
	actions->Add(new wxRadioButton(box, XRCID("ID_ACTION7"), _("Overwrite if &different size")));
	actions->Add(new wxRadioButton(box, XRCID("ID_ACTION6"), _("Overwrite if different si&ze or source newer")));
	actions->Add(new wxRadioButton(box, XRCID("ID_ACTION3"), _("&Resume")));
	actions->Add(new wxRadioButton(box, XRCID("ID_ACTION4"), _("Re&name")));
	actions->Add(new wxRadioButton(box, XRCID("ID_ACTION5"), _("&Skip")));


	right->Add(new wxCheckBox(this, XRCID("ID_ALWAYS"), _("&Always use this action")));
	right->Add(new wxCheckBox(this, XRCID("ID_QUEUEONLY"), _("Apply to &current queue only")), 0, wxLEFT, lay.indent);
	right->Add(new wxCheckBox(this, XRCID("ID_UPDOWNONLY"), wxString()), 0, wxLEFT, lay.indent);

	auto* buttons = lay.createButtonSizer(this, main, true);

	auto ok = new wxButton(this, wxID_OK, _("&OK"));
	ok->SetDefault();
	buttons->AddButton(ok);

	auto cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
	buttons->AddButton(cancel);

	buttons->Realize();




	if (!SetupControls()) {
		return false;
	}
	GetSizer()->Fit(this);
	GetSizer()->SetSizeHints(this);

	return true;
}

void CFileExistsDlg::DisplayFile(bool left, std::wstring const& name, int64_t size, fz::datetime const& time, std::wstring const& iconFile)
{
	std::wstring labelName = LabelEscape(GetPathEllipsis(name, FindWindow(left ? XRCID("ID_FILE1_NAME") : XRCID("ID_FILE2_NAME"))));

	wxString sizeStr = _("Size unknown");
	if (size >= 0) {
		sizeStr = SizeFormatter(options_).Format(size, UnitPrefix::byte);
	}

	wxString timeStr = _("Date/time unknown");
	if (!time.empty()) {
		timeStr = time_formatter_.Format(time);
	}

	xrc_call(*this, left ? "ID_FILE1_NAME" : "ID_FILE2_NAME", &wxStaticText::SetLabel, labelName);
	xrc_call(*this, left ? "ID_FILE1_SIZE" : "ID_FILE2_SIZE", &wxStaticText::SetLabel, sizeStr);
	xrc_call(*this, left ? "ID_FILE1_TIME" : "ID_FILE2_TIME", &wxStaticText::SetLabel, timeStr);

	LoadIcon(left ? XRCID("ID_FILE1_ICON") : XRCID("ID_FILE2_ICON"), iconFile);
}

bool CFileExistsDlg::SetupControls()
{
	std::wstring const& localFile = m_pNotification->localFile;
	std::wstring remoteFile = m_pNotification->remotePath.FormatFilename(m_pNotification->remoteFile);

	DisplayFile(m_pNotification->download, localFile, m_pNotification->localSize, m_pNotification->localTime, m_pNotification->localFile);
	DisplayFile(!m_pNotification->download, remoteFile, m_pNotification->remoteSize, m_pNotification->remoteTime, m_pNotification->remoteFile);

	xrc_call(*this, "ID_UPDOWNONLY", &wxCheckBox::SetLabel, m_pNotification->download ? _("A&pply only to downloads") : _("A&pply only to uploads"));

	return true;
}

void CFileExistsDlg::LoadIcon(int id, std::wstring const& file)
{
	wxStaticBitmap *pStatBmp = static_cast<wxStaticBitmap *>(FindWindow(id));
	if (!pStatBmp) {
		return;
	}

	wxSize size = CThemeProvider::GetIconSize(iconSizeNormal);
	pStatBmp->SetInitialSize(size);
	pStatBmp->InvalidateBestSize();

#ifdef __WXMSW__
	SHFILEINFO fileinfo;
	memset(&fileinfo, 0, sizeof(fileinfo));
	if (SHGetFileInfo(file.c_str(), FILE_ATTRIBUTE_NORMAL, &fileinfo, sizeof(fileinfo), SHGFI_ICON | SHGFI_USEFILEATTRIBUTES)) {
		wxBitmap bmp;
		bmp.Create(size.x, size.y);

		wxMemoryDC *dc = new wxMemoryDC;

		wxPen pen(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
		wxBrush brush(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));

		dc->SelectObject(bmp);

		dc->SetPen(pen);
		dc->SetBrush(brush);
		dc->DrawRectangle(0, 0, size.x, size.y);

		wxIcon icon;
		icon.SetHandle(fileinfo.hIcon);
		icon.SetSize(size.x, size.y);

		dc->DrawIcon(icon, 0, 0);
		delete dc;

		pStatBmp->SetBitmap(MakeBmpBundle(bmp));

		return;
	}
#else
	std::wstring ext = GetExtension(file);
	if (!ext.empty() && ext != L".") {
		std::unique_ptr<wxFileType> pType(wxTheMimeTypesManager->GetFileTypeFromExtension(ext));
		if (pType) {
			wxIconLocation loc;
			if (pType->GetIcon(&loc) && loc.IsOk()) {
				wxLogNull* tmp = new wxLogNull;
				wxIcon icon(loc);
				delete tmp;
				if (icon.Ok()) {
					int width = icon.GetWidth();
					int height = icon.GetHeight();
					if (width && height) {
						wxBitmap bmp;
						bmp.Create(icon.GetWidth(), icon.GetHeight());

						wxMemoryDC* dc = new wxMemoryDC;

						wxPen pen(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
						wxBrush brush(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));

						dc->SelectObject(bmp);

						dc->SetPen(pen);
						dc->SetBrush(brush);
						dc->DrawRectangle(0, 0, width, height);

						dc->DrawIcon(icon, 0, 0);
						delete dc;

						pStatBmp->SetBitmap(bmp);

						return;
					}
				}
			}
		}
	}
#endif

	pStatBmp->SetBitmap(MakeBmpBundle(CThemeProvider::Get()->CreateBitmap(_T("ART_FILE"), wxART_OTHER, size)));
}

void CFileExistsDlg::OnOK(wxCommandEvent&)
{
	if (xrc_call(*this, "ID_ACTION1", &wxRadioButton::GetValue)) {
		m_action = CFileExistsNotification::overwrite;
	}
	else if (xrc_call(*this, "ID_ACTION2", &wxRadioButton::GetValue)) {
		m_action = CFileExistsNotification::overwriteNewer;
	}
	else if (xrc_call(*this, "ID_ACTION3", &wxRadioButton::GetValue)) {
		m_action = CFileExistsNotification::resume;
	}
	else if (xrc_call(*this, "ID_ACTION4", &wxRadioButton::GetValue)) {
		m_action = CFileExistsNotification::rename;
	}
	else if (xrc_call(*this, "ID_ACTION5", &wxRadioButton::GetValue)) {
		m_action = CFileExistsNotification::skip;
	}
	else if (xrc_call(*this, "ID_ACTION6", &wxRadioButton::GetValue)) {
		m_action = CFileExistsNotification::overwriteSizeOrNewer;
	}
	else if (xrc_call(*this, "ID_ACTION7", &wxRadioButton::GetValue)) {
		m_action = CFileExistsNotification::overwriteSize;
	}
	else {
		m_action = CFileExistsNotification::overwrite;
	}

	m_always = xrc_call(*this, "ID_ALWAYS", &wxCheckBox::GetValue);
	m_directionOnly = xrc_call(*this, "ID_UPDOWNONLY", &wxCheckBox::GetValue);
	m_queueOnly = xrc_call(*this, "ID_QUEUEONLY", &wxCheckBox::GetValue);
	EndModal(wxID_OK);
}

CFileExistsNotification::OverwriteAction CFileExistsDlg::GetAction() const
{
	return m_action;
}

void CFileExistsDlg::OnCancel(wxCommandEvent&)
{
	m_action = CFileExistsNotification::skip;
	EndModal(wxID_CANCEL);
}

bool CFileExistsDlg::Always(bool &directionOnly, bool &queueOnly) const
{
	directionOnly = m_directionOnly;
	queueOnly = m_queueOnly;
	return m_always;
}

std::wstring CFileExistsDlg::GetPathEllipsis(std::wstring const& path, wxWindow *window)
{
	int dn = wxDisplay::GetFromWindow(GetParent()); // Use parent window as the dialog isn't realized yet.
	if (dn < 0) {
		return path;
	}

	int string_width; // width of the path string in pixels
	int y;			// dummy variable
	window->GetTextExtent(path, &string_width, &y);

	wxDisplay display(dn);
	wxRect rect = display.GetClientArea();
	const int DESKTOP_WIDTH = rect.GetWidth(); // width of the desktop in pixels
	const int maxWidth = (int)(DESKTOP_WIDTH * 0.75);

	// If the path is already short enough, don't change it
	if (string_width <= maxWidth || path.size() < 20) {
		return path;
	}

	wxString fill = _T(" ");
	fill += 0x2026; //unicode ellipsis character
	fill += _T(" ");

	int fillWidth;
	window->GetTextExtent(fill, &fillWidth, &y);

	// Do initial split roughly in the middle of the string
	size_t middle = path.size() / 2;
	wxString left = path.substr(0, middle);
	wxString right = path.substr(middle);

	int leftWidth, rightWidth;
	window->GetTextExtent(left, &leftWidth, &y);
	window->GetTextExtent(right, &rightWidth, &y);

	// continue removing one character at a time around the fill until path string is small enough
	while ((leftWidth + fillWidth + rightWidth) > maxWidth) {
		if (leftWidth > rightWidth && left.Len() > 10) {
			left.RemoveLast();
			window->GetTextExtent(left, &leftWidth, &y);
		}
		else {
			if (right.Len() <= 10) {
				break;
			}

			right = right.Mid(1);
			window->GetTextExtent(right, &rightWidth, &y);
		}
	}

	return (left + fill + right).ToStdWstring();
}

void CFileExistsDlg::OnCheck(wxCommandEvent& event)
{
	if (event.GetId() != XRCID("ID_UPDOWNONLY") && event.GetId() != XRCID("ID_QUEUEONLY")) {
		return;
	}

	if (event.IsChecked()) {
		XRCCTRL(*this, "ID_ALWAYS", wxCheckBox)->SetValue(true);
	}
}
