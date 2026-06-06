#include "filezilla.h"
#include "aui_notebook_ex.h"
#include "themeprovider.h"

#include "filezillaapp.h"

#include <wx/aui/aui.h>
#include <wx/dcmirror.h>
#include <wx/graphics.h>

#include <memory>

#ifdef __WXMSW__
#define TABCOLOUR wxSYS_COLOUR_3DFACE
#else
#define TABCOLOUR wxSYS_COLOUR_WINDOWFRAME
#endif

struct wxAuiTabArtExData
{
	std::map<wxString, int> maxSizes;
};


#if defined(__WXMSW__) || defined (__WXMAC__)
#define USE_PREPARED_ICONS 1
#endif

#if USE_PREPARED_ICONS
namespace {
void PrepareTabIcon(wxBitmap & active, wxBitmap & disabled, wxString const& art, wxSize size, wxSize canvasSize, wxSize offset = wxSize(), std::function<void(wxImage&)> const& f = nullptr, unsigned char brightness = 128)
{
#ifdef __WXMAC__
	double const scale = wxGetApp().GetTopWindow()->GetContentScaleFactor();
#else
	double const scale = 1.0;
#endif
	size *= scale;
	canvasSize *= scale;
	offset *= scale;

	wxBitmap loaded = CThemeProvider::Get()->CreateBitmap(art, wxART_TOOLBAR, size);
	if (!loaded.IsOk()) {
		return;
	}

	wxImage img(canvasSize.x, canvasSize.y);
	img.SetMaskColour(0, 0, 0);
	img.InitAlpha();

	int x = (canvasSize.x - loaded.GetSize().x) / 2 + offset.x;
	int y = (canvasSize.x - loaded.GetSize().y) / 2 + offset.y;
	img.Paste(loaded.ConvertToImage(), x, y);
	if (f) {
		f(img);
	}
#ifdef __WXMAC__
	active = wxBitmap(img, -1, scale);
#else
	active = wxBitmap(img);
#endif
	disabled = active.ConvertToDisabled(brightness);
}

void PrepareTabIcon(wxBitmapBundle & active, wxBitmapBundle & disabled, wxString const& art, wxSize const& size, wxSize const& canvasSize, wxSize const& offset = wxSize(), std::function<void(wxImage&)> const& f = nullptr, unsigned char brightness = 128)
{
	wxBitmap a, d;
	PrepareTabIcon(a, d, art, size, canvasSize, offset, f, brightness);
	active = MakeBmpBundle(a);
	disabled = MakeBmpBundle(d);
}
}
#endif

#ifdef __WXGTK__
typedef wxAuiDefaultTabArt TabArtBase;
#else
typedef wxAuiGenericTabArt TabArtBase;
#endif
class wxAuiTabArtEx : public TabArtBase
{
public:
	wxAuiTabArtEx(wxAuiNotebookEx* pNotebook, std::shared_ptr<wxAuiTabArtExData> const& data)
		: m_pNotebook(pNotebook)
		, m_data(data)
	{

#if USE_PREPARED_ICONS
		PrepareIcons();
#endif
	}

	virtual wxAuiTabArt* Clone() override
	{
		wxAuiTabArtEx *art = new wxAuiTabArtEx(m_pNotebook, m_data);
		art->SetNormalFont(m_normalFont);
		art->SetSelectedFont(m_selectedFont);
		art->SetMeasuringFont(m_measuringFont);
		return art;
	}

	virtual wxSize GetTabSize(wxDC& dc, wxWindow* wnd, const wxString& caption, const wxBitmapBundle& bitmap, bool active, int close_button_state, int* x_extent) override
	{
		wxSize size = TabArtBase::GetTabSize(dc, wnd, caption, bitmap, active, close_button_state, x_extent);

		wxString text = caption;
		int pos;
		if ((pos = caption.Find(_T(" ("))) != -1) {
			text = text.Left(pos);
		}
		auto iter = m_data->maxSizes.find(text);
		if (iter == m_data->maxSizes.end()) {
			m_data->maxSizes[text] = size.x;
		}
		else {
			if (iter->second > size.x) {
				size.x = iter->second;
				*x_extent = size.x;
			}
			else {
				iter->second = size.x;
			}
		}

		return size;
	}

	virtual void DrawTab(wxDC &dc, wxWindow *wnd, const wxAuiNotebookPage &page, const wxRect &rect, int close_button_state, wxRect *out_tab_rect, wxRect *out_button_rect, int *x_extent) override
	{
		wxColour const tint = m_pNotebook->GetTabColour(page.window);

		if (tint.IsOk()) {

#if !defined(__WXGTK__) || !defined(wxHAS_NATIVE_TABART)
			wxColour const baseOrig = m_baseColour;
			wxColour const activeOrig = m_activeColour;


			wxColour const base(
				wxColour::AlphaBlend(tint.Red(),   baseOrig.Red(),   tint.Alpha() / 255.0f),
				wxColour::AlphaBlend(tint.Green(), baseOrig.Green(), tint.Alpha() / 255.0f),
				wxColour::AlphaBlend(tint.Blue(),  baseOrig.Blue(),  tint.Alpha() / 255.0f));

			wxColour const active(
				wxColour::AlphaBlend(tint.Red(),   activeOrig.Red(),   tint.Alpha() / 255.0f),
				wxColour::AlphaBlend(tint.Green(), activeOrig.Green(), tint.Alpha() / 255.0f),
				wxColour::AlphaBlend(tint.Blue(),  activeOrig.Blue(),  tint.Alpha() / 255.0f));

			m_baseColour = base;
			m_activeColour = active;

			TabArtBase::DrawTab(dc, wnd, page, rect, close_button_state, out_tab_rect, out_button_rect, x_extent);

			m_baseColour = baseOrig;
			m_activeColour = baseOrig;
#else
			wxRect tab_rect;
			if (!out_tab_rect) {
				out_tab_rect = &tab_rect;
			}

			TabArtBase::DrawTab(dc, wnd, page, rect, close_button_state, out_tab_rect, out_button_rect, x_extent);

			wxMemoryDC *mdc = dynamic_cast<wxMemoryDC*>(&dc);
			if (mdc) {
				wxGraphicsContext *gc = wxGraphicsContext::Create(*mdc);
				if (gc) {
					gc->SetBrush(wxBrush(tint));
					gc->DrawRectangle(out_tab_rect->x, out_tab_rect->y, out_tab_rect->width, out_tab_rect->height);
					delete gc;
				}
			}
#endif
		}
		else {
			TabArtBase::DrawTab(dc, wnd, page, rect, close_button_state, out_tab_rect, out_button_rect, x_extent);
		}
	}

protected:

#if USE_PREPARED_ICONS
	virtual void UpdateColoursFromSystem() override
	{
		TabArtBase::UpdateColoursFromSystem();
		PrepareIcons();
	}

	void PrepareIcons()
	{
		wxSize canvas(CThemeProvider::Get()->GetIconSize(iconSizeSmall));
		wxSize size = canvas;
		size.Scale(0.75, 0.75);

		wxSize closeOffset(-3, 0);
		PrepareTabIcon(m_activeCloseBmp, m_disabledCloseBmp, L"ART_CLOSE", size, canvas, closeOffset);

		wxSize offset(0, (canvas.y - size.y) / -4);
		PrepareTabIcon(m_activeWindowListBmp, m_disabledWindowListBmp, L"ART_DROPDOWN", size, canvas, offset);

		// Up arrow mirrored along top-left to bottom-right diagonal gets a left button with correct drop shadow
		auto mirror = [](wxImage& img) {
			img = img = img.Mirror().Rotate90(false);
		};
		PrepareTabIcon(m_activeLeftBmp, m_disabledLeftBmp, L"ART_SORT_UP_DARK", size, canvas, offset, mirror, 192);
		PrepareTabIcon(m_activeRightBmp, m_disabledRightBmp, L"ART_SORT_DOWN_DARK", size, canvas, offset, mirror, 192);
	}
#endif

	wxAuiNotebookEx* m_pNotebook;

	std::shared_ptr<wxAuiTabArtExData> m_data;
};

BEGIN_EVENT_TABLE(wxAuiNotebookEx, wxAuiNotebook)
EVT_AUINOTEBOOK_PAGE_CHANGED(wxID_ANY, wxAuiNotebookEx::OnPageChanged)
EVT_AUINOTEBOOK_DRAG_MOTION(wxID_ANY, wxAuiNotebookEx::OnTabDragMotion)
END_EVENT_TABLE()

void wxAuiNotebookEx::OnTabDragMotion(wxAuiNotebookEvent& evt)
{
	wxAuiNotebook::OnTabDragMotion(evt);

	int active = m_tabs.GetActivePage();
	if (active != wxNOT_FOUND) {
		m_curPage = active;
	}
}

void wxAuiNotebookEx::RemoveExtraBorders()
{
	wxAuiPaneInfoArray& panes = m_mgr.GetAllPanes();
	for (size_t i = 0; i < panes.Count(); ++i) {
		panes[i].PaneBorder(false);
	}
	m_mgr.Update();
}

void wxAuiNotebookEx::SetExArtProvider()
{
	SetArtProvider(new wxAuiTabArtEx(this, std::make_shared<wxAuiTabArtExData>()));
}

bool wxAuiNotebookEx::SetPageText(size_t page_idx, const wxString& text)
{
	// Basically identical to the AUI one, but not calling Update
	if (page_idx >= m_tabs.GetPageCount()) {
		return false;
	}

	// update our own tab catalog
	wxAuiNotebookPage& page_info = m_tabs.GetPage(page_idx);
	page_info.caption = text;

	// update what's on screen
	wxAuiTabCtrl* ctrl;
	int ctrl_idx;
	if (FindTab(page_info.window, &ctrl, &ctrl_idx)) {
		wxAuiNotebookPage& info = ctrl->GetPage(ctrl_idx);
		info.caption = text;
		ctrl->Refresh();
	}

	return true;
}

void wxAuiNotebookEx::SetTabColour(size_t page, wxColour const& c)
{
	wxWindow* w = GetPage(page);
	if (w) {
		m_colourMap[w] = c;
	}
}

void wxAuiNotebookEx::Highlight(size_t page, bool highlight)
{
	if (GetSelection() == (int)page) {
		return;
	}

	wxASSERT(page < m_tabs.GetPageCount());
	if (page >= m_tabs.GetPageCount()) {
		return;
	}

	if (page >= m_highlighted.size()) {
		m_highlighted.resize(page + 1, false);
	}

	if (highlight == m_highlighted[page]) {
		return;
	}

	m_highlighted[page] = highlight;

	GetActiveTabCtrl()->Refresh();
}

bool wxAuiNotebookEx::Highlighted(size_t page) const
{
	wxASSERT(page < m_tabs.GetPageCount());
	if (page >= m_highlighted.size()) {
		return false;
	}

	return m_highlighted[page];
}

void wxAuiNotebookEx::OnPageChanged(wxAuiNotebookEvent&)
{
	size_t page = (size_t)GetSelection();
	if (page >= m_highlighted.size())
		return;

	m_highlighted[page] = false;
}

void wxAuiNotebookEx::AdvanceTab(bool forward)
{
	int page = GetSelection();
	if (forward)
		++page;
	else
		--page;
	if (page >= (int)GetPageCount())
		page = 0;
	else if (page < 0)
		page = GetPageCount() - 1;

	SetSelection(page);
}

bool wxAuiNotebookEx::AddPage(wxWindow *page, const wxString &text, bool select, int imageId)
{
	bool const res = wxAuiNotebook::AddPage(page, text, select, imageId);
	size_t const count = GetPageCount();

	if (count > 1) {
		GetPage(count - 1)->MoveAfterInTabOrder(GetPage(count - 2));
	}

	if (GetWindowStyle() & wxAUI_NB_BOTTOM) {
		GetActiveTabCtrl()->MoveAfterInTabOrder(GetPage(count - 1));
	}
	else {
		GetActiveTabCtrl()->MoveBeforeInTabOrder(GetPage(0));
	}

	return res;
}

bool wxAuiNotebookEx::RemovePage(size_t page)
{
	m_colourMap.erase(GetPage(page));
	return wxAuiNotebook::RemovePage(page);
}

wxColour wxAuiNotebookEx::GetTabColour(wxWindow* page)
{
	auto const it = m_colourMap.find(page);
	if (it != m_colourMap.end()) {
		return it->second;
	}
	return wxColour();
}
