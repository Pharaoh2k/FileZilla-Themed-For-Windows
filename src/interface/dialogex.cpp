#include "filezilla.h"
#include "dialogex.h"
#include "msgbox.h"
#include "xrc_helper.h"

#include <wx/combobox.h>
#include <wx/statline.h>
#include <wx/gbsizer.h>

#ifdef __WXMAC__
#include "filezillaapp.h"
#include "dropsource.h"
#endif


BEGIN_EVENT_TABLE(wxDialogEx, wxDialog)
EVT_CHAR_HOOK(wxDialogEx::OnChar)
END_EVENT_TABLE()

std::vector<wxDialogEx*> wxDialogEx::shown_dialogs_;

#ifdef __WXMAC__
wxTextEntry* GetSpecialTextEntry(wxWindow* w, wxChar cmd)
{
	if (cmd == 'A' || cmd == 'V') {
		wxTextCtrl* text = dynamic_cast<wxTextCtrl*>(w);
		if (text) {
			return text;
		}
	}
	return dynamic_cast<wxComboBox*>(w);
}
#else
wxTextEntry* GetSpecialTextEntry(wxWindow*, wxChar)
{
	return 0;
}
#endif

#ifdef __WXMAC__
static int const pasteId = wxNewId();
static int const copyId = wxNewId();
static int const cutId = wxNewId();
static int const selectAllId = wxNewId();

bool wxDialogEx::ProcessEvent(wxEvent& event)
{
	if (event.GetEventType() != wxEVT_MENU) {
		return wxDialog::ProcessEvent(event);
	}

	wxTextEntry* e = GetSpecialTextEntry(FindFocus(), 'V');
	if (e && event.GetId() == pasteId) {
		e->Paste();
		return true;
	}
	else if (e && event.GetId() == copyId) {
		e->Copy();
		return true;
	}
	else if (e && event.GetId() == cutId) {
		e->Cut();
		return true;
	}
	else if (e && event.GetId() == selectAllId) {
		e->SelectAll();
		return true;
	}
	else {
		return wxDialog::ProcessEvent(event);
	}
}
#endif

void wxDialogEx::OnChar(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_ESCAPE) {
		wxCommandEvent cmdEvent(wxEVT_COMMAND_BUTTON_CLICKED, wxID_CANCEL);
		ProcessEvent(cmdEvent);
	}
	else {
		event.Skip();
	}
}

bool wxDialogEx::SetChildLabel(int id, const wxString& label, unsigned long maxLength)
{
	wxWindow* pText = FindWindow(id);
	if (!pText) {
		return false;
	}

	if (!maxLength) {
		pText->SetLabel(label);
	}
	else {
		wxString wrapped = label;
		WrapText(this, wrapped, maxLength);
		pText->SetLabel(wrapped);
	}

	return true;
}

bool wxDialogEx::SetChildLabel(char const* id, const wxString& label, unsigned long maxLength)
{
	return SetChildLabel(XRCID(id), label, maxLength);
}

wxString wxDialogEx::GetChildLabel(int id)
{
	auto pText = dynamic_cast<wxStaticText*>(FindWindow(id));
	if (!pText) {
		return wxString();
	}

	return pText->GetLabel();
}

int wxDialogEx::ShowModal()
{
	CenterOnParent();

#ifdef __WXMSW__
	// All open menus need to be closed or app will become unresponsive.
	::EndMenu();

	// For same reason release mouse capture.
	// Could happen during drag&drop with notification dialogs.
	::ReleaseCapture();
#endif

	shown_dialogs_.push_back(this);

	if (acceleratorTable_.empty()) {
		SetAcceleratorTable(wxNullAcceleratorTable);
	}
	else {
		SetAcceleratorTable(wxAcceleratorTable(acceleratorTable_.size(), acceleratorTable_.data()));
	}

	int ret = wxDialog::ShowModal();

	shown_dialogs_.pop_back();

	return ret;
}

bool wxDialogEx::ReplaceControl(wxWindow* old, wxWindow* wnd)
{
	if (!GetSizer()->Replace(old, wnd, true)) {
		return false;
	}
	old->Destroy();

	return true;
}

bool wxDialogEx::CanShowPopupDialog(wxTopLevelWindow * parent)
{
	if (!IsActiveTLW(parent)) {
		return false;
	}

	wxMouseState mouseState = wxGetMouseState();
	if (mouseState.LeftIsDown() || mouseState.MiddleIsDown() || mouseState.RightIsDown()) {
		// Displaying a dialog while the user is clicking is extremely confusing, don't do it.
		return false;
	}
#ifdef __WXMSW__
	// During a drag & drop we cannot show a dialog. Doing so can render the program unresponsive
	if (GetCapture()) {
		return false;
	}
#endif

#ifdef __WXMAC__
	if (MacIsMouseTracking()) {
		return false;
	}
#endif

	return true;
}

bool wxDialogEx::IsActiveTLW(wxTopLevelWindow * parent)
{
	if (IsShowingMessageBox()) {
		// There already a message box showing
		return false;
	}

	if (parent && !shown_dialogs_.empty() && shown_dialogs_.back() != parent) {
		// There is an open dialog which isn't the expected parent
		return false;
	}

	return true;
}

void wxDialogEx::InitDialog()
{
#ifdef __WXGTK__
	// Some controls only report proper size after the call to Show(), e.g.
	// wxStaticBox::GetBordersForSizer is affected by this.
	// Re-fit window to compensate.
	wxSizer* s = GetSizer();
	if (s) {
		wxSize min = GetMinClientSize();
		wxSize smin = s->GetMinSize();
		if ( min.x < smin.x || min.y < smin.y ) {
			s->Fit(this);
			SetMinSize(GetSize());
		}
	}
#endif

	wxDialog::InitDialog();
}


DialogLayout const& wxDialogEx::layout()
{
	if (!layout_) {
		layout_ = std::make_unique<DialogLayout>(this);
	}

	return *layout_;
}

wxSizerFlags const DialogLayout::grow(wxSizerFlags().Expand());
wxSizerFlags const DialogLayout::valign(wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL));
wxSizerFlags const DialogLayout::halign(wxSizerFlags().Align(wxALIGN_CENTER_HORIZONTAL));
wxSizerFlags const DialogLayout::valigng(wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL).Expand());
wxSizerFlags const DialogLayout::ralign(wxSizerFlags().Align(wxALIGN_RIGHT));

DialogLayout::DialogLayout(wxTopLevelWindow * parent)
	: parent_(parent)
{
	gap = dlgUnits(3);
	border = dlgUnits(3);
	indent = dlgUnits(10);
#if defined(__WXMAC__)
	defTextCtrlSize = parent ? parent->ConvertDialogToPixels(wxSize(50, -1)) : wxDefaultSize;
#else
	defTextCtrlSize = wxDefaultSize;
#endif
}

int DialogLayout::dlgUnits(int num) const
{
	return wxDLG_UNIT(parent_, wxPoint(0, num)).y;
}

wxFlexGridSizer* DialogLayout::createMain(wxWindow* parent, int cols, int rows) const
{
	auto outer = new wxBoxSizer(wxVERTICAL);
	parent->SetSizer(outer);

	auto main = createFlex(cols, rows);
	outer->Add(main, 1, wxALL|wxGROW, border);

	return main;
}

wxFlexGridSizer* DialogLayout::createFlex(int cols, int rows) const
{
	int const g = gap;
	return new wxFlexGridSizer(rows, cols, g, g);
}

wxGridSizer* DialogLayout::createGrid(int cols, int rows) const
{
	int const g = gap;
	return new wxGridSizer(rows, cols, g, g);
}

wxGridBagSizer* DialogLayout::createGridBag(int cols, int rows) const
{
	int const g = gap;
	auto bag = new wxGridBagSizer(g, g);
	bag->SetCols(cols);
	bag->SetRows(rows);
	return bag;
}

wxStdDialogButtonSizer * DialogLayout::createButtonSizer(wxWindow* parent, wxSizer * sizer, bool hline) const
{
	if (hline) {
		sizer->Add(new wxStaticLine(parent), grow);
	}
	auto btns = new wxStdDialogButtonSizer();
	sizer->Add(btns, grow);

	return btns;
}

std::tuple<wxStdDialogButtonSizer*, wxButton*, wxButton*> DialogLayout::createButtonSizerButtons(wxWindow* parent, wxSizer * sizer, bool hline, bool okcancel, bool realize) const
{
	auto buttons = createButtonSizer(parent, sizer, hline);

	wxButton* ok{};
	wxButton* cancel{};
	if (okcancel) {
		ok = new wxButton(parent, wxID_OK, _("&OK"));
		ok->SetDefault();
		buttons->AddButton(ok);

		cancel = new wxButton(parent, wxID_CANCEL, _("Cancel"));
		buttons->AddButton(cancel);
	}
	else {
		ok = new wxButton(parent, wxID_YES, _("&Yes"));
		ok->SetDefault();
		buttons->AddButton(ok);

		cancel = new wxButton(parent, wxID_NO, _("&No"));
		buttons->AddButton(cancel);
	}
	ok->SetDefault();

	if (realize) {
		buttons->Realize();
	}

	return {buttons, ok, cancel};
}

wxSizerItem* DialogLayout::gbAddRow(wxGridBagSizer * gb, wxWindow* wnd, wxSizerFlags const& flags) const
{
	int row = gb->GetRows();
	gb->SetRows(row + 1);

	return gb->Add(wnd, wxGBPosition(row, 0), wxGBSpan(1, gb->GetCols()), flags.GetFlags(), flags.GetBorderInPixels());
}

void DialogLayout::gbNewRow(wxGridBagSizer * gb) const
{
	gb->SetRows(gb->GetRows() + 1);
}

wxSizerItem* DialogLayout::gbAdd(wxGridBagSizer* gb, wxWindow* wnd, wxSizerFlags const& flags) const
{
	int const row = gb->GetRows() - 1;
	int col;
	for (col = 0; col < gb->GetCols(); ++col) {
		if (!gb->FindItemAtPosition(wxGBPosition(row, col))) {
			break;
		}
	}

	auto item = gb->Add(wnd, wxGBPosition(row, col), wxGBSpan(), flags.GetFlags(), flags.GetBorderInPixels());
	return item;
}

wxSizerItem* DialogLayout::gbAdd(wxGridBagSizer* gb, wxSizer* sizer, wxSizerFlags const& flags) const
{
	int const row = gb->GetRows() - 1;
	int col;
	for (col = 0; col < gb->GetCols(); ++col) {
		if (!gb->FindItemAtPosition(wxGBPosition(row, col))) {
			break;
		}
	}

	auto item = gb->Add(sizer, wxGBPosition(row, col), wxGBSpan(), flags.GetFlags(), flags.GetBorderInPixels());
	return item;
}

std::tuple<wxStaticBox*, wxFlexGridSizer*> DialogLayout::createStatBox(wxSizer* parent, wxString const& title, int cols, int rows) const
{
	auto* boxSizer = new wxStaticBoxSizer(wxHORIZONTAL, parent->GetContainingWindow(), title);
	auto* box = boxSizer->GetStaticBox();
	parent->Add(boxSizer, nullID, wxGROW);

	auto* flex = createFlex(cols, rows);
#ifdef __WXMSW__
	int style = wxGROW | wxLEFT | wxBOTTOM | wxRIGHT;
	int const b = dlgUnits(2);
#else
	int style = wxGROW | wxALL;
	int const b = border;
#endif
	boxSizer->Add(flex, 1, style, b);

	return std::make_tuple(box, flex);
}

std::wstring LabelEscape(std::wstring_view const& label, size_t maxlen)
{
	std::wstring ret = fz::replaced_substrings(label.substr(0, maxlen), L"&", L"&&");
	if (label.size() > maxlen) {
		ret += 0x2026; //unicode ellipsis character
	}
	return ret;
}
