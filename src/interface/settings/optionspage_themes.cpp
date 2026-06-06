#include "../filezilla.h"

#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_themes.h"
#include "../themeprovider.h"
#include "../wxext/spinctrlex.h"

#include <wx/dcclient.h>
#include <wx/scrolwin.h>
#include <wx/statbox.h>


const int BORDER = 5;

class CIconPreview final : public wxScrolledWindow
{
public:
	CIconPreview() = default;

	CIconPreview(wxWindow* pParent)
		: wxScrolledWindow(pParent, nullID, wxDefaultPosition, wxDefaultSize, wxVSCROLL)
	{
		Bind(wxEVT_PAINT, &CIconPreview::OnPaint, this);
	}

	void LoadIcons(std::wstring const& theme, wxSize const& size)
	{
		m_iconSize = size;

		m_icons = CThemeProvider::Get()->GetAllImages(theme, size);

		m_sizeInitialized = false;
		Refresh();
	}

	void CalcSize()
	{
		if (m_sizeInitialized) {
			return;
		}
		m_sizeInitialized = true;

		wxSize size = GetClientSize();

		if (!m_icons.empty()) {
			int icons_per_line = wxMax(1, (size.GetWidth() - BORDER) / (m_iconSize.GetWidth() + BORDER));

			// Number of lines and line height
			int lines = (m_icons.size() - 1) / icons_per_line + 1;
			int vheight = lines * (m_iconSize.GetHeight() + BORDER) + BORDER;
			if (vheight > size.GetHeight()) {
				// Scroll bar would appear, need to adjust width
				size.SetHeight(vheight);
				SetVirtualSize(size);
				SetScrollRate(0, m_iconSize.GetHeight() + BORDER);

				wxSize size2 = GetClientSize();
				size.SetWidth(size2.GetWidth());

				icons_per_line = wxMax(1, (size.GetWidth() - BORDER) / (m_iconSize.GetWidth() + BORDER));
				lines = (m_icons.size() - 1) / icons_per_line + 1;
				vheight = lines * (m_iconSize.GetHeight() + BORDER) + BORDER;
				if (vheight > size.GetHeight())
					size.SetHeight(vheight);
			}

			// Calculate extra padding
			if (icons_per_line > 1) {
				int extra = size.GetWidth() - BORDER - icons_per_line * (m_iconSize.GetWidth() + BORDER);
				m_extra_padding = extra / (icons_per_line - 1);
			}
		}
		SetVirtualSize(size);
		SetScrollRate(0, m_iconSize.GetHeight() + BORDER);
	}

protected:
	virtual void OnPaint(wxPaintEvent&)
	{
		CalcSize();

		wxPaintDC dc(this);
		PrepareDC(dc);

		wxSize size = GetClientSize();

		if (m_icons.empty()) {
			dc.SetFont(GetFont());
			wxString text = _("No images available");
			wxCoord w, h;
			dc.GetTextExtent(text, &w, &h);
			dc.DrawText(text, (size.GetWidth() - w) / 2, (size.GetHeight() - h) / 2);
			return;
		}

		int x = BORDER;
		int y = BORDER;

		for (auto const& bmp : m_icons) {
			dc.DrawBitmap(bmp, x, y, true);
			x += m_iconSize.GetWidth() + BORDER + m_extra_padding;
			if ((x + m_iconSize.GetWidth() + BORDER) > size.GetWidth()) {
				x = BORDER;
				y += m_iconSize.GetHeight() + BORDER;
			}
		}
	}

	std::vector<wxBitmap> m_icons;
	wxSize m_iconSize;
	bool m_sizeInitialized{};
	int m_extra_padding{};
};

struct COptionsPageThemes::impl final
{
	wxChoice* theme_{};
	wxStaticText* author_{};
	wxStaticText* email_{};
	wxSpinCtrlDoubleEx* scale_{};
	CIconPreview* preview_{};
};

COptionsPageThemes::COptionsPageThemes()
	: impl_(std::make_unique<impl>())
{}

COptionsPageThemes::~COptionsPageThemes()
{
}

bool COptionsPageThemes::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	main->AddGrowableRow(1);
	SetSizer(main);

	{
		auto [box, inner] = lay.createStatBox(main, _("Select Theme"), 2);
		inner->Add(new wxStaticText(box, nullID, _("&Theme:")), lay.valign);
		impl_->theme_ = new wxChoice(box, nullID);
		inner->Add(impl_->theme_);
		inner->Add(new wxStaticText(box, nullID, _("Author:")), lay.valign);
		impl_->author_ = new wxStaticText(box, nullID, wxString());
		inner->Add(impl_->author_, lay.valign);
		inner->Add(new wxStaticText(box, nullID, _("Email:")), lay.valign);
		impl_->email_ = new wxStaticText(box, nullID, wxString());
		inner->Add(impl_->email_, lay.valign);
		inner->Add(new wxStaticText(box, nullID, _("Scale factor:")), lay.valign);
		impl_->scale_ = new wxSpinCtrlDoubleEx(box, nullID);
		impl_->scale_->SetRange(0.5, 4);
		impl_->scale_->SetIncrement(0.25);
		impl_->scale_->SetValue(1.25);
		impl_->scale_->SetDigits(2);
		impl_->scale_->SetMaxLength(10);
		impl_->scale_->Bind(wxEVT_SPINCTRLDOUBLE, &COptionsPageThemes::OnThemeChange, this);
		inner->Add(impl_->scale_, lay.valign);
	}

	{
		auto [box, inner] = lay.createStatBox(main, _("Preview"), 1);
		inner->AddGrowableCol(0);
		inner->AddGrowableRow(0);
		impl_->preview_ = new CIconPreview(box);
		inner->Add(impl_->preview_, 1, wxGROW);
	}

	GetSizer()->Layout();
	GetSizer()->Fit(this);
	
	impl_->theme_->Bind(wxEVT_CHOICE, &COptionsPageThemes::OnThemeChange, this);

	return true;
}

bool COptionsPageThemes::LoadPage()
{
	return true;
}

bool COptionsPageThemes::SavePage()
{
	if (!m_was_selected) {
		return true;
	}

	const int sel = impl_->theme_->GetSelection();
	const wxString theme = ((wxStringClientData*)impl_->theme_->GetClientObject(sel))->GetData();

	m_pOptions->set(OPTION_ICONS_THEME, theme.ToStdWstring());

	m_pOptions->set(OPTION_ICONS_SCALE, static_cast<int>(100 * impl_->scale_->GetValue()));

	return true;
}

bool COptionsPageThemes::Validate()
{
	return true;
}

bool COptionsPageThemes::DisplayTheme(std::wstring const& theme)
{
	std::wstring name, author, mail;
	if (!CThemeProvider::Get()->GetThemeData(theme, name, author, mail)) {
		return false;
	}
	if (name.empty()) {
		return false;
	}

	if (author.empty()) {
		author = _("N/a").ToStdWstring();
	}
	if (mail.empty()) {
		mail = _("N/a").ToStdWstring();
	}

	impl_->author_->SetLabel(LabelEscape(author));
	impl_->email_->SetLabel(LabelEscape(mail));

	auto scale_factor = impl_->scale_->GetValue();
	wxSize size = CThemeProvider::Get()->GetIconSize(iconSizeSmall);
	size.Scale(scale_factor, scale_factor);

	impl_->preview_->LoadIcons(theme, size);

	return true;
}

void COptionsPageThemes::OnThemeChange(wxCommandEvent&)
{
	const int sel = impl_->theme_->GetSelection();
	std::wstring const theme = ((wxStringClientData*)impl_->theme_->GetClientObject(sel))->GetData().ToStdWstring();
	DisplayTheme(theme);
}

bool COptionsPageThemes::OnDisplayedFirstTime()
{
	bool failure = false;

	auto const themes = CThemeProvider::GetThemes();
	if (themes.empty()) {
		return false;
	}

	impl_->scale_->SetValue(static_cast<double>(m_pOptions->get_int(OPTION_ICONS_SCALE)) / 100.f);
	
	std::wstring activeTheme = m_pOptions->get_string(OPTION_ICONS_THEME);
	std::wstring firstName;
	for (auto const& theme : themes) {
		std::wstring name, author, mail;
		if (!CThemeProvider::Get()->GetThemeData(theme, name, author, mail)) {
			continue;
		}
		if (firstName.empty()) {
			firstName = name;
		}
		int n = impl_->theme_->Append(name, new wxStringClientData(theme));
		if (theme == activeTheme) {
			impl_->theme_->SetSelection(n);
		}
	}
	if (impl_->theme_->GetSelection() == wxNOT_FOUND) {
		impl_->theme_->SetSelection(impl_->theme_->FindString(firstName));
	}
	activeTheme = ((wxStringClientData*)impl_->theme_->GetClientObject(impl_->theme_->GetSelection()))->GetData().ToStdWstring();

	if (!DisplayTheme(activeTheme)) {
		failure = true;
	}

	impl_->theme_->GetContainingSizer()->Layout();

#ifdef __WXMAC__
	if (!failure) {
		CallAfter([this]{
			impl_->preview_->Refresh(true, nullptr);
		});
	}
#endif
	return !failure;
}
