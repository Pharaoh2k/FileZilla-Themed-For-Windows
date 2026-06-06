#include "filezilla.h"
#include "listctrlex.h"
#include "filezillaapp.h"

#include <wx/renderer.h>
#include <wx/statbox.h>

#include "Options.h"
#include "dialogex.h"
#ifdef __WXMSW__
#include "commctrl.h"

#ifndef HDF_SORTUP
#define HDF_SORTUP				0x0400
#endif
#ifndef HDF_SORTDOWN
#define HDF_SORTDOWN			0x0200
#endif
#else
#include "themeprovider.h"
#endif

wxDECLARE_EVENT(fzEVT_POSTSCROLL, wxCommandEvent);
wxDEFINE_EVENT(fzEVT_POSTSCROLL, wxCommandEvent);

BEGIN_EVENT_TABLE(wxListCtrlEx, wxListCtrlExBase)
EVT_COMMAND(wxID_ANY, fzEVT_POSTSCROLL, wxListCtrlEx::OnPostScrollEvent)
EVT_SCROLLWIN(wxListCtrlEx::OnScrollEvent)
EVT_MOUSEWHEEL(wxListCtrlEx::OnMouseWheel)
EVT_LIST_ITEM_FOCUSED(wxID_ANY, wxListCtrlEx::OnSelectionChanged)
EVT_LIST_ITEM_SELECTED(wxID_ANY, wxListCtrlEx::OnSelectionChanged)
EVT_KEY_DOWN(wxListCtrlEx::OnKeyDown)
EVT_LIST_BEGIN_LABEL_EDIT(wxID_ANY, wxListCtrlEx::OnBeginLabelEdit)
EVT_LIST_END_LABEL_EDIT(wxID_ANY, wxListCtrlEx::OnEndLabelEdit)
#ifdef __WXMSW__
EVT_SYS_COLOUR_CHANGED(wxListCtrlEx::SystemColorChange)
#else
EVT_LIST_COL_DRAGGING(wxID_ANY, wxListCtrlEx::OnColumnDragging)
#endif
END_EVENT_TABLE()

#define MIN_COLUMN_WIDTH 12

wxListCtrlEx::wxListCtrlEx(wxWindow *parent,
						   wxWindowID id,
						   const wxPoint& pos,
						   const wxSize& size,
						   long style,
						   const wxValidator& validator,
						   const wxString& name)
{
#if defined(__WXMSW__)
	// This gets rid of vertical lines between columns
	EnableSystemTheme(false);
#endif

	Create(parent, id, pos, size, style, validator, name);

#ifndef __WXMSW__
	m_editing = false;
#else
	m_columnDragging = false;

	::SendMessage(GetHandle(), LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_HEADERDRAGDROP, 0);
#endif

	m_blockedLabelEditing = false;
}

wxListCtrlEx::~wxListCtrlEx()
{
#ifdef __WXMSW__
	delete m_pHeaderImageList;
#endif
	delete [] m_pVisibleColumnMapping;
}

wxWindow* wxListCtrlEx::GetMainWindow()
{
#ifdef __WXMSW__
	return this;
#else
	return reinterpret_cast<wxWindow*>(m_mainWin);
#endif
}

wxWindow const* wxListCtrlEx::GetMainWindow() const
{
#ifdef __WXMSW__
	return this;
#else
	return reinterpret_cast<wxWindow const*>(m_mainWin);
#endif
}

void wxListCtrlEx::OnPostScroll()
{
}

void wxListCtrlEx::OnPostScrollEvent(wxCommandEvent&)
{
	OnPostScroll();
}

void wxListCtrlEx::OnPreEmitPostScrollEvent()
{
	EmitPostScrollEvent();
}

void wxListCtrlEx::EmitPostScrollEvent()
{
	QueueEvent(new wxCommandEvent(fzEVT_POSTSCROLL, wxID_ANY));
}

void wxListCtrlEx::OnScrollEvent(wxScrollWinEvent& event)
{
	event.Skip();
	OnPreEmitPostScrollEvent();
}

void wxListCtrlEx::OnMouseWheel(wxMouseEvent& event)
{
	event.Skip();
	OnPreEmitPostScrollEvent();
}

void wxListCtrlEx::OnSelectionChanged(wxListEvent& event)
{
	event.Skip();
	OnPreEmitPostScrollEvent();
}

void wxListCtrlEx::ScrollTopItem(int item)
{
	if (!GetItemCount()) {
		return;
	}

	if (item < 0) {
		item = 0;
	}
	else if (item >= GetItemCount()) {
		item = GetItemCount() - 1;
	}

	const int current = GetTopItem();

	int delta = item - current;

	if (!delta) {
		return;
	}

	wxRect rect;
	GetItemRect(current, rect, wxLIST_RECT_BOUNDS);

	delta *= rect.GetHeight();
	ScrollList(0, delta);
}


void wxListCtrlEx::HandlePrefixSearch(wxChar character)
{
	wxASSERT(character);

	// Keyboard navigation within items
	fz::datetime now = fz::datetime::now();
	if (!m_prefixSearch_lastKeyPress.empty()) {
		fz::duration span = now - m_prefixSearch_lastKeyPress;
		if (span.get_seconds() >= 1) {
			m_prefixSearch_prefix.clear();
		}
	}
	m_prefixSearch_lastKeyPress = now;

	wxString newPrefix = m_prefixSearch_prefix + character;

	int item;
#ifndef __WXMSW__
	// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
	if (!GetSelectedItemCount()) {
		item = -1;
	}
	else
#endif
	{
		item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	}

	bool beep = false;
	if (item != -1) {
		wxString text = GetItemText(item, 0);
		if (text.Length() >= m_prefixSearch_prefix.Length() && !m_prefixSearch_prefix.CmpNoCase(text.Left(m_prefixSearch_prefix.Length()))) {
			beep = true;
		}
	}
	else if (m_prefixSearch_prefix.empty()) {
		beep = true;
	}

	int start = item;
	if (start < 0) {
		start = 0;
	}

	int newPos = FindItemWithPrefix(newPrefix, start);

	if (newPos == -1 && (m_prefixSearch_prefix.Len() == 1 && m_prefixSearch_prefix[0] == character) && item != -1 && beep) {
		// Search the next item that starts with the same letter
		newPrefix = m_prefixSearch_prefix;
		newPos = FindItemWithPrefix(newPrefix, item + 1);
	}

	m_prefixSearch_prefix = newPrefix;
	if (newPos == -1) {
		if (beep) {
			wxBell();
		}
		return;
	}

	while (item != -1) {
		SetItemState(item, 0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
		item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	}
	SetItemState(newPos, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);

#ifdef __WXMSW__
	// SetItemState does not move the selection mark, that is the item from
	// which a multiple selection starts (e.g. shift+up/down)
	HWND hWnd = (HWND)GetHandle();
	::SendMessage(hWnd, LVM_SETSELECTIONMARK, 0, newPos);
#endif

	EnsureVisible(newPos);
}

void wxListCtrlEx::OnKeyDown(wxKeyEvent& event)
{
	if (!m_prefixSearch_enabled) {
		event.Skip();
		return;
	}

	int code = event.GetKeyCode();
	if (code == WXK_LEFT ||
		code == WXK_RIGHT ||
		code == WXK_UP ||
		code == WXK_DOWN ||
		code == WXK_HOME ||
		code == WXK_END)
	{
		ResetSearchPrefix();
		event.Skip();
		return;
	}

	if (event.AltDown() && !event.ControlDown()) {
		// Alt but not AltGr
		event.Skip();
		return;
	}

	wxChar key;

	switch (code)
	{
	case WXK_NUMPAD0:
	case WXK_NUMPAD1:
	case WXK_NUMPAD2:
	case WXK_NUMPAD3:
	case WXK_NUMPAD4:
	case WXK_NUMPAD5:
	case WXK_NUMPAD6:
	case WXK_NUMPAD7:
	case WXK_NUMPAD8:
	case WXK_NUMPAD9:
		key = '0' + code - WXK_NUMPAD0;
		break;
	case WXK_NUMPAD_ADD:
		key = '+';
		break;
	case WXK_NUMPAD_SUBTRACT:
		key = '-';
		break;
	case WXK_NUMPAD_MULTIPLY:
		key = '*';
		break;
	case WXK_NUMPAD_DIVIDE:
		key = '/';
		break;
	default:
		key = 0;
		break;
	}
	if (key) {
		if (event.GetModifiers()) {
			// Numpad keys can not have modifiers
			event.Skip();
		}
		HandlePrefixSearch(key);
		return;
	}

#if defined(__WXMSW__)

	if (code >= 300 && code != WXK_NUMPAD_DECIMAL) {
		event.Skip();
		return;
	}

	// Get the actual key
	BYTE state[256];
	if (!GetKeyboardState(state)) {
		event.Skip();
		return;
	}
	wxChar buffer[1];
	int res = ToUnicode(event.GetRawKeyCode(), 0, state, buffer, 1, 0);
	if (res != 1) {
		event.Skip();
		return;
	}

	key = buffer[0];

	if (key < 32) {
		event.Skip();
		return;
	}
	if (key == 32 && event.HasModifiers()) {
		event.Skip();
		return;
	}
	HandlePrefixSearch(key);
	return;
#else
	if (code > 32 && code < 300 && !event.HasModifiers()) {
		int unicodeKey = event.GetUnicodeKey();
		if (unicodeKey) {
			code = unicodeKey;
		}
		HandlePrefixSearch(code);
	}
	else {
		event.Skip();
	}
#endif //defined(__WXMSW__)
}

// Declared const due to design error in wxWidgets.
// Won't be fixed since a fix would break backwards compatibility
// Both functions use a const_cast<CLocalListView *>(this) and modify
// the instance.
wxString wxListCtrlEx::OnGetItemText(long item, long column) const
{
	wxListCtrlEx *pThis = const_cast<wxListCtrlEx *>(this);
	return pThis->GetItemText(item, GetColumnActualIndex(column));
}

int wxListCtrlEx::FindItemWithPrefix(const wxString& searchPrefix, int start)
{
	const int count = GetItemCount();
	for (int i = start; i < (count + start); ++i) {
		int item = i % count;
		wxString namePrefix = GetItemText(item, 0).Left(searchPrefix.Length());
		if (!namePrefix.CmpNoCase(searchPrefix)) {
			return i % count;
		}
	}
	return -1;
}

void wxListCtrlEx::SaveSetItemCount(long count)
{
#ifndef __WXMSW__
	if (count < GetItemCount()) {
		int focused = GetNextItem(count - 1, wxLIST_NEXT_ALL, wxLIST_STATE_FOCUSED);
		if (focused != -1) {
			SetItemState(focused, 0, wxLIST_STATE_FOCUSED);
		}
	}
#endif //__WXMSW__
	SetItemCount(count);
}

void wxListCtrlEx::ResetSearchPrefix()
{
	m_prefixSearch_prefix.clear();
}

void wxListCtrlEx::ShowColumn(unsigned int col, bool show)
{
	if (col >= m_columnInfo.size()) {
		return;
	}

	if (m_columnInfo[col].shown == show) {
		return;
	}

	m_columnInfo[col].shown = show;

	if (show) {
		// Insert new column
		int pos = 0;
		for (unsigned int i = 0; i < m_columnInfo.size(); ++i) {
			if (i == col) {
				continue;
			}
			t_columnInfo& info = m_columnInfo[i];
			if (info.shown && info.order < m_columnInfo[col].order) {
				++pos;
			}
		}
		for (int i = GetColumnCount() - 1; i >= pos; --i) {
			m_pVisibleColumnMapping[i + 1] = m_pVisibleColumnMapping[i];
		}
		m_pVisibleColumnMapping[pos] = col;

		t_columnInfo& info = m_columnInfo[col];
		InsertColumn(pos, info.name, info.align, info.width);
	}
	else {
		int i;
		for (i = 0; i < GetColumnCount(); ++i) {
			if (m_pVisibleColumnMapping[i] == col) {
				break;
			}
		}
		wxASSERT(m_columnInfo[col].order >= (unsigned int)i);
		for (int j = i + 1; j < GetColumnCount(); ++j) {
			m_pVisibleColumnMapping[j - 1] = m_pVisibleColumnMapping[j];
		}

		wxASSERT(i < GetColumnCount());

		m_columnInfo[col].width = GetColumnWidth(i);
		DeleteColumn(i);
	}
}

void wxListCtrlEx::LoadColumnSettings(COptionsBase & options, interfaceOptions widthsOptionId, interfaceOptions visibilityOptionId, interfaceOptions sortOptionId)
{
	wxASSERT(!GetColumnCount());

	if (widthsOptionId != OPTIONS_NUM) {
		ReadColumnWidths(options, widthsOptionId);
	}

	delete [] m_pVisibleColumnMapping;
	m_pVisibleColumnMapping = new unsigned int[m_columnInfo.size()];

	if (visibilityOptionId != OPTIONS_NUM) {
		wxString visibleColumns = options.get_string(visibilityOptionId);
		if (visibleColumns.Len() >= m_columnInfo.size()) {
			for (unsigned int i = 0; i < m_columnInfo.size(); ++i) {
				if (!m_columnInfo[i].fixed) {
					m_columnInfo[i].shown = visibleColumns[i] == '1';
				}
			}
		}
	}

	if (sortOptionId != OPTIONS_NUM) {
		auto tokens = fz::strtok(options.get_string(sortOptionId), L",");

		if (tokens.size() >= m_columnInfo.size()) {
			unsigned int *order = new unsigned int[m_columnInfo.size()];
			bool *order_set = new bool[m_columnInfo.size()];
			memset(order_set, 0, sizeof(bool) * m_columnInfo.size());

			size_t i{};
			for (; i < m_columnInfo.size(); ++i) {
				order[i] = fz::to_integral(tokens[i], std::numeric_limits<unsigned int>::max());
				if (order[i] == std::numeric_limits<unsigned int>::max()) {
					break;
				}
				if (order[i] >= m_columnInfo.size() || order_set[order[i]]) {
					break;
				}
				order_set[order[i]] = true;
			}

			if (i == m_columnInfo.size()) {
				bool valid = true;
				for (size_t j = 0; j < m_columnInfo.size(); ++j) {
					if (!m_columnInfo[j].fixed) {
						continue;
					}

					if (j != order[j]) {
						valid = false;
						break;
					}
				}

				if (valid) {
					for (size_t j = 0; j < m_columnInfo.size(); ++j) {
						m_columnInfo[j].order = order[j];
					}
				}
			}

			delete [] order;
			delete [] order_set;
		}
	}

	CreateVisibleColumnMapping();
}

void wxListCtrlEx::SaveColumnSettings(COptionsBase& options, interfaceOptions widthsOptionId, interfaceOptions visibilityOptionId, interfaceOptions sortOptionId)
{
	if (widthsOptionId != OPTIONS_NUM) {
		SaveColumnWidths(options, widthsOptionId);
	}

	if (visibilityOptionId != OPTIONS_NUM) {
		std::wstring visibleColumns;
		for (unsigned int i = 0; i < m_columnInfo.size(); ++i) {
			if (m_columnInfo[i].shown) {
				visibleColumns += L"1";
			}
			else {
				visibleColumns += L"0";
			}
		}
		options.set(visibilityOptionId, visibleColumns);
	}

	if (sortOptionId != OPTIONS_NUM) {
		std::wstring order;
		for (unsigned int i = 0; i < m_columnInfo.size(); ++i) {
			if (i) {
				order += L",";
			}
			order += fz::to_wstring(m_columnInfo[i].order);
		}
		options.set(sortOptionId, order);
	}
}

bool wxListCtrlEx::ReadColumnWidths(COptionsBase & options, interfaceOptions optionId)
{
	wxASSERT(!GetColumnCount());

	if (wxGetKeyState(WXK_SHIFT) &&
		wxGetKeyState(WXK_ALT) &&
		wxGetKeyState(WXK_CONTROL))
	{
		return true;
	}


	auto tokens = fz::strtok(options.get_string(optionId), L" ");

	auto const count = std::min(tokens.size(), m_columnInfo.size());
	for (size_t i = 0; i < count; ++i) {
		int width = fz::to_integral(tokens[i], -1);
		if (width >= 0 && width < 10000) {
			m_columnInfo[i].width = width;
		}
	}

	return true;
}

void wxListCtrlEx::SaveColumnWidths(COptionsBase & options, interfaceOptions optionId)
{
	const unsigned int count = m_columnInfo.size();

	wxString widths;
	for (unsigned int i = 0; i < count; ++i) {
		int width = 0;

		bool found = false;
		for (int j = 0; j < GetColumnCount(); ++j) {
			if (m_pVisibleColumnMapping[j] != i) {
				continue;
			}

			found = true;
			width = GetColumnWidth(j);
		}
		if (!found) {
			width = m_columnInfo[i].width;
		}
		widths += wxString::Format(_T("%d "), width);
	}
	widths.RemoveLast();

	options.set(optionId, widths.ToStdWstring());
}


void wxListCtrlEx::AddColumn(const wxString& name, int align, int initialWidth, bool fixed)
{
	wxASSERT(!GetColumnCount());

	t_columnInfo info;
	info.name = name;
	info.align = align;
	info.width = initialWidth;
	info.shown = true;
	info.order = m_columnInfo.size();
	info.fixed = fixed;

	m_columnInfo.push_back(info);
}

// Moves column. Target position includes both hidden
// as well as shown columns
void wxListCtrlEx::MoveColumn(unsigned int col, unsigned int before)
{
	if (m_columnInfo[col].order == before) {
		return;
	}

	for (unsigned int i = 0; i < m_columnInfo.size(); ++i) {
		if (i == col) {
			continue;
		}

		t_columnInfo& info = m_columnInfo[i];
		if (info.order > col) {
			--info.order;
		}
		if (info.order >= before) {
			++info.order;
		}
	}

	t_columnInfo& info = m_columnInfo[col];

	if (info.shown) {
		int icon = -1;
		// Remove old column
		for (unsigned int i = 0; i < (unsigned int)GetColumnCount(); ++i) {
			if (m_pVisibleColumnMapping[i] != col) {
				continue;
			}

			for (unsigned int j = i + 1; j < (unsigned int)GetColumnCount(); ++j) {
				m_pVisibleColumnMapping[j - 1] = m_pVisibleColumnMapping[j];
			}
			info.width = GetColumnWidth(i);

			icon = GetHeaderSortIconIndex(i);
			DeleteColumn(i);

			break;
		}

		// Insert new column
		unsigned int pos = 0;
		for (unsigned int i = 0; i < m_columnInfo.size(); ++i) {
			if (i == col) {
				continue;
			}
			t_columnInfo& info2 = m_columnInfo[i];
			if (info2.shown && info2.order < before) {
				++pos;
			}
		}
		for (unsigned int i = (int)GetColumnCount(); i > pos; --i) {
			m_pVisibleColumnMapping[i] = m_pVisibleColumnMapping[i - 1];
		}
		m_pVisibleColumnMapping[pos] = col;

		InsertColumn(pos, info.name, info.align, info.width);

		SetHeaderSortIconIndex(pos, icon);
	}
	m_columnInfo[col].order = before;
}

void wxListCtrlEx::CreateVisibleColumnMapping()
{
	int pos = 0;
	for (unsigned int j = 0; j < m_columnInfo.size(); ++j) {
		for (unsigned int i = 0; i < m_columnInfo.size(); ++i) {
			const t_columnInfo &column = m_columnInfo[i];

			if (!column.shown) {
				continue;
			}

			if (column.order != j) {
				continue;
			}

			m_pVisibleColumnMapping[pos] = i;
			InsertColumn(pos++, column.name, column.align, column.width);
		}
	}
}

class CColumnEditDialog final : public wxDialogEx
{
public:
	std::vector<int> order_;
	wxCheckListBox * list_box_{};

	bool Create(wxWindow* parent)
	{
		if (!wxDialogEx::Create(parent, -1, _("Column setup"))) {
			return false;
		}

		auto& lay = layout();
		auto main = lay.createMain(this, 1);

		{
			auto [box, inner] = lay.createStatBox(main, _("Visible columns"), 1);
			inner->AddGrowableCol(0);
			inner->Add(new wxStaticText(box, -1, _("&Select the columns that should be displayed:")));

			auto row = lay.createFlex(2);
			row->AddGrowableCol(0);
			inner->Add(row, lay.grow);
			list_box_ = new wxCheckListBox(box, -1);
			list_box_->Bind(wxEVT_LISTBOX, &CColumnEditDialog::OnSelChanged, this);
			list_box_->Bind(wxEVT_CHECKLISTBOX, &CColumnEditDialog::OnCheck, this);
			row->Add(list_box_, lay.grow);

			auto col = lay.createFlex(1);
			col->AddGrowableCol(0);
			row->Add(col);
			up_ = new wxButton(box, -1, _("Move &up"));
			up_->Bind(wxEVT_BUTTON, &CColumnEditDialog::OnUp, this);
			up_->Disable();
			col->Add(up_, lay.grow);
			down_ = new wxButton(box, -1, _("Move &down"));
			down_->Bind(wxEVT_BUTTON, &CColumnEditDialog::OnDown, this);
			down_->Disable();
			col->Add(down_, lay.grow);
		}

		auto buttons = lay.createButtonSizer(this, main, true);
		auto ok = new wxButton(this, wxID_OK, _("OK"));
		ok->SetDefault();
		buttons->AddButton(ok);
		auto cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
		buttons->AddButton(cancel);
		buttons->Realize();

		Layout();
		GetSizer()->Fit(this);

		return true;
	}

protected:

	wxButton* up_{};
	wxButton* down_{};

	void OnUp(wxCommandEvent&)
	{
		int sel = list_box_->GetSelection();
		if (sel < 2) {
			return;
		}

		std::swap(order_[sel], order_[sel - 1]);

		wxString name = list_box_->GetString(sel);
		bool checked = list_box_->IsChecked(sel);
		list_box_->Delete(sel);
		list_box_->Insert(name, sel - 1);
		list_box_->Check(sel - 1, checked);
		list_box_->SetSelection(sel - 1);

		wxCommandEvent evt;
		OnSelChanged(evt);
	}

	void OnDown(wxCommandEvent&)
	{
		int sel = list_box_->GetSelection();
		if (sel < 1) {
			return;
		}
		if (sel >= (int)list_box_->GetCount() - 1) {
			return;
		}

		std::swap(order_[sel], order_[sel + 1]);

		wxString name = list_box_->GetString(sel);
		bool checked = list_box_->IsChecked(sel);
		list_box_->Delete(sel);
		list_box_->Insert(name, sel + 1);
		list_box_->Check(sel + 1, checked);
		list_box_->SetSelection(sel + 1);

		wxCommandEvent evt;
		OnSelChanged(evt);
	}

	void OnSelChanged(wxCommandEvent&)
	{
		int sel = list_box_->GetSelection();
		up_->Enable(sel > 1);
		down_->Enable(sel > 0 && sel < (int)list_box_->GetCount() - 1);
	}

	void OnCheck(wxCommandEvent& event)
	{
		if (!event.GetSelection() && !event.IsChecked()) {
			list_box_->Check(0);
			wxMessageBoxEx(_("The filename column can neither be hidden nor moved."), _("Column properties"));
		}
	}
};

void wxListCtrlEx::ShowColumnEditor()
{
	CColumnEditDialog dlg;

	if (!dlg.Create(this)) {
		return;
	}

	dlg.order_.resize(m_columnInfo.size());
	for (size_t j = 0; j < m_columnInfo.size(); ++j) {
		for (size_t i = 0; i < m_columnInfo.size(); ++i) {
			if (m_columnInfo[i].order != j) {
				continue;
			}
			dlg.order_[j] = i;
			dlg.list_box_->Append(m_columnInfo[i].name);
			if (m_columnInfo[i].shown) {
				dlg.list_box_->Check(j);
			}
		}
	}
	wxASSERT(dlg.list_box_->GetCount() == m_columnInfo.size());

	dlg.GetSizer()->Fit(&dlg);

	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	for (size_t i = 0; i < m_columnInfo.size(); ++i) {
		int col = dlg.order_[i];
		bool isChecked = dlg.list_box_->IsChecked(i);
		if (!isChecked && !col) {
			isChecked = true;
			wxMessageBoxEx(_("The filename column cannot be hidden."));
		}
		MoveColumn(col, i);
		if (m_columnInfo[col].shown != isChecked) {
			ShowColumn(col, isChecked);
		}
	}

	// Generic wxListCtrl needs manual refresh
	Refresh();
}

int wxListCtrlEx::GetColumnVisibleIndex(int col) const
{
	if (!m_pVisibleColumnMapping) {
		return -1;
	}

	for (int i = 0; i < GetColumnCount(); ++i) {
		if (m_pVisibleColumnMapping[i] == (unsigned int)col) {
			return i;
		}
	}

	return -1;
}

int wxListCtrlEx::GetColumnActualIndex(int col) const
{
	if (!m_pVisibleColumnMapping) {
		return -1;
	}
	if (col < 0 || static_cast<size_t>(col) >= m_columnInfo.size()) {
		return -1;
	}
	return m_pVisibleColumnMapping[col];
}

int wxListCtrlEx::GetHeaderSortIconIndex(int col)
{
	if (col < 0 || col >= GetColumnCount()) {
		return -1;
	}

#ifdef __WXMSW__
	HWND hWnd = (HWND)GetHandle();
	HWND header = (HWND)SendMessage(hWnd, LVM_GETHEADER, 0, 0);

	HDITEM item;
	item.mask = HDI_IMAGE | HDI_FORMAT;
	SendMessage(header, HDM_GETITEM, col, (LPARAM)&item);

	if (!(item.fmt & HDF_IMAGE)) {
		return -1;
	}

	return item.iImage;
#else
	wxListItem item;
	if (!GetColumn(col, item)) {
		return -1;
	}

	return item.GetImage();
#endif
}

void wxListCtrlEx::InitHeaderSortImageList()
{
#ifndef __WXMSW__
	wxColour colour = wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE);

	wxString lightness;
	if (colour.Red() + colour.Green() + colour.Blue() > 3 * 128) {
		lightness = _T("DARK");
	}
	else {
		lightness = _T("LIGHT");
	}

	auto * imageList = GetSystemImageList();
	if (imageList) {
		wxBitmap bmp;

		bmp = CThemeProvider::Get()->CreateBitmap(_T("ART_SORT_UP_") + lightness, wxART_OTHER, CThemeProvider::GetIconSize(iconSizeSmall));
		m_header_icon_index.up = imageList->Add(bmp);
		bmp = CThemeProvider::Get()->CreateBitmap(_T("ART_SORT_DOWN_") + lightness, wxART_OTHER, CThemeProvider::GetIconSize(iconSizeSmall));
		m_header_icon_index.down = imageList->Add(bmp);
	}
#endif
}

void wxListCtrlEx::SetHeaderSortIconIndex(int col, int icon)
{
	if (col < 0 || col >= GetColumnCount()) {
		return;
	}

#ifdef __WXMSW__
	HWND hWnd = (HWND)GetHandle();
	HWND header = (HWND)SendMessage(hWnd, LVM_GETHEADER, 0, 0);

	HDITEM item = {};
	item.mask = HDI_FORMAT;
	SendMessage(header, HDM_GETITEM, col, (LPARAM)&item);
	if (icon != -1) {
		item.fmt &= ~(HDF_IMAGE | HDF_BITMAP_ON_RIGHT | HDF_SORTUP | HDF_SORTDOWN);
		item.iImage = -1;
		if (icon) {
			item.fmt |= HDF_SORTDOWN;
		}
		else {
			item.fmt |= HDF_SORTUP;
		}
	}
	else {
		item.fmt &= ~(HDF_IMAGE | HDF_BITMAP_ON_RIGHT | HDF_SORTUP | HDF_SORTDOWN);
		item.iImage = -1;
	}
	SendMessage(header, HDM_SETITEM, col, (LPARAM)&item);
#else
	wxListItem item;
	if (!GetColumn(col, item)) {
		return;
	}

	if (icon != -1) {
		icon = icon ? m_header_icon_index.down : m_header_icon_index.up;
	}

	item.SetImage(icon);
	SetColumn(col, item);
	reinterpret_cast<wxWindow*>(m_headerWin)->Refresh();
#endif
}

void wxListCtrlEx::RefreshListOnly(bool eraseBackground /*=true*/)
{
	// See comment in wxGenericListCtrl::Refresh
	GetMainWindow()->Refresh(eraseBackground);
}

void wxListCtrlEx::CancelLabelEdit()
{
#ifdef __WXMSW__
	if (GetEditControl()) {
		ListView_CancelEditLabel((HWND)GetHandle());
	}
#else
	m_editing = false;
	wxTextCtrl* pEdit = GetEditControl();
	if (pEdit) {
		wxKeyEvent evt(wxEVT_CHAR);
		evt.m_keyCode = WXK_ESCAPE;
		pEdit->GetEventHandler()->ProcessEvent(evt);
	}
#endif

}

void wxListCtrlEx::OnBeginLabelEdit(wxListEvent& event)
{
#ifndef __WXMSW__
	if (m_editing) {
		event.Veto();
		return;
	}
#endif
	if (m_blockedLabelEditing) {
		event.Veto();
		return;
	}

	if (!OnBeginRename(event)) {
		event.Veto();
	}
#ifndef __WXMSW__
	else {
		m_editing = true;
	}
#endif
}

void wxListCtrlEx::OnEndLabelEdit(wxListEvent& event)
{
#ifdef __WXMAC__
	int item = event.GetIndex();
	if (item != -1) {
		int to = item + 1;
		if (to < GetItemCount()) {
			int from = item;
			if (from) {
				--from;
			}
			RefreshItems(from, to);
		}
		else {
			RefreshListOnly();
		}
	}
#endif

#ifndef __WXMSW__
	if (!m_editing) {
		event.Veto();
		return;
	}
	m_editing = false;
#endif

	if (event.IsEditCancelled()) {
		return;
	}

	if (!OnAcceptRename(event)) {
		event.Veto();
	}
}

bool wxListCtrlEx::OnBeginRename(const wxListEvent&)
{
	return false;
}

bool wxListCtrlEx::OnAcceptRename(const wxListEvent&)
{
	return false;
}

void wxListCtrlEx::SetLabelEditBlock(bool block)
{
	if (block) {
		CancelLabelEdit();
		++m_blockedLabelEditing;
	}
	else {
		wxASSERT(m_blockedLabelEditing);
		if (m_blockedLabelEditing > 0) {
			--m_blockedLabelEditing;
		}
	}
}

CLabelEditBlocker::CLabelEditBlocker(wxListCtrlEx& listCtrl)
	: m_listCtrl(listCtrl)
{
	m_listCtrl.SetLabelEditBlock(true);
}

CLabelEditBlocker::~CLabelEditBlocker()
{
	m_listCtrl.SetLabelEditBlock(false);
}

void wxListCtrlEx::OnColumnDragging(wxListEvent& event)
{
	if (event.GetItem().GetWidth() < MIN_COLUMN_WIDTH) {
		event.Veto();
	}
}

#ifdef __WXMSW__
bool wxListCtrlEx::MSWOnNotify(int idCtrl, WXLPARAM lParam, WXLPARAM *result)
{
	// MSW doesn't generate HDN_TRACK on all header styles, so handle it
	// ourselves using HDN_ITEMCHANGING.
	NMHDR *nmhdr = (NMHDR *)lParam;
	HWND hwndHdr = ListView_GetHeader((HWND)GetHandle());

	if (nmhdr->hwndFrom != hwndHdr) {
		return wxListCtrl::MSWOnNotify(idCtrl, lParam, result);
	}

	HD_NOTIFY *nmHDR = (HD_NOTIFY *)nmhdr;

	switch ( nmhdr->code )
	{
		// See comment in src/msw/listctrl.cpp of wx why both A and W are needed
	case HDN_BEGINTRACKA:
	case HDN_BEGINTRACKW:
		m_columnDragging = true;
		break;
	case HDN_ENDTRACKA:
	case HDN_ENDTRACKW:
		m_columnDragging = true;
		break;
	case HDN_ITEMCHANGINGA:
	case HDN_ITEMCHANGINGW:
		if (m_columnDragging) {
			if (nmHDR->pitem->mask & HDI_WIDTH && nmHDR->pitem->cxy < MIN_COLUMN_WIDTH) {
				*result = 1;
				return true;
			}
			else {
				*result = 0;
				return false;
			}
		}
		else {
			return false;
		}
	case HDN_DIVIDERDBLCLICK:
		{
			auto event = new wxListEvent(wxEVT_LIST_COL_END_DRAG, GetId());
			event->SetEventObject(this);
			QueueEvent(event);
		}
		break;
	}

	return wxListCtrl::MSWOnNotify(idCtrl, lParam, result);
}
#endif

bool wxListCtrlEx::HasSelection() const
{
#ifndef __WXMSW__
	// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
	return GetSelectedItemCount() != 0;
#else
	return GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) != -1;
#endif
}


wxRect wxListCtrlEx::GetActualClientRect() const
{
	wxRect windowRect = GetMainWindow()->GetClientRect();
#ifdef __WXMSW__
	wxRect topRect;
	if (GetItemRect(0, topRect)) {
		windowRect.height -= topRect.y;
		windowRect.y += topRect.y;
	}
#endif

	return windowRect;
}

#ifdef __WXMSW__
void wxListCtrlEx::SystemColorChange(wxSysColourChangedEvent& event)
{
	SetTextColour(GetDefaultAttributes().colFg);
	event.Skip();
}
#endif
