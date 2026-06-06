#ifndef FILEZILLA_INTERFACE_THEMEPROVIDER_HEADER
#define FILEZILLA_INTERFACE_THEMEPROVIDER_HEADER

#include "option_change_event_handler.h"

#include <memory>

#include <libfilezilla/time.hpp>

#include <wx/animate.h>
#include <wx/artprov.h>

enum iconSize
{
	iconSizeTiny,
	iconSizeSmall,
	iconSize24,
	iconSizeNormal,
	iconSizeLarge,
	iconSizeHuge
};

struct wxSize_cmp final
{
	bool operator()(wxSize const& a, wxSize const& b) const {
		return a.x < b.x || (a.x == b.x && a.y < b.y);
	}
};

class CTheme final
{
public:
	bool Load(std::wstring const& theme);
	bool Load(std::wstring const& theme, std::vector<wxSize> sizes);

	wxBitmap const& LoadBitmap(CLocalPath const& cacheDir, std::wstring const& name, wxSize const& size, bool allowContentScale = true);
	wxAnimation LoadAnimation(std::wstring const& name, wxSize const& size);

	static wxSize StringToSize(std::wstring const&);

	std::wstring get_theme() const { return theme_; }
	std::wstring get_name() const { return name_; }
	std::wstring get_author() const { return author_; }
	std::wstring get_mail() const { return mail_; }

	std::vector<wxBitmap> GetAllImages(CLocalPath const& cacheDir, wxSize const& size);
private:
	typedef std::map<wxSize, wxBitmap, wxSize_cmp> BmpCache;
	struct cacheEntry
	{
		bool empty() const;

		// Converting from wxImage to wxBitmap to wxImage is quite slow, so cache the images as well.
		BmpCache bitmaps_;
		std::map<wxSize, wxImage, wxSize_cmp> images_;
#ifdef __WXMAC__
		BmpCache contentScaledBitmaps_;
#endif
		BmpCache& getBmpCache(bool allowContentScale);
	};

	wxBitmap const& DoLoadBitmap(CLocalPath const& cacheDir, std::wstring const& name, wxSize const& size, cacheEntry & cache, bool allowContentScale);

	wxBitmap const& LoadBitmapWithSpecificSizeAndScale(CLocalPath const& cacheDir, std::wstring const& name, wxSize const& size, wxSize const& scale, cacheEntry & cache, bool allowContentScale);

	wxImage const& LoadImageWithSpecificSize(std::wstring const& file, wxSize const& size, cacheEntry & cache);

	std::wstring theme_;
	std::wstring path_;

	std::wstring name_;
	std::wstring author_;
	std::wstring mail_;

	fz::datetime timestamp_;

	std::map<wxSize, bool, wxSize_cmp> sizes_;

	std::map<std::wstring, cacheEntry> cache_;
};

class COptions;
class CThemeProvider final : public wxArtProvider, public wxEvtHandler, public COptionChangeEventHandler
{
public:
	CThemeProvider(COptions& options);
	virtual ~CThemeProvider();

	static std::vector<std::wstring> GetThemes();
	std::vector<wxBitmap> GetAllImages(std::wstring const& theme, wxSize const& size);
	bool GetThemeData(std::wstring const& theme, std::wstring& name, std::wstring& author, std::wstring& email);
	static wxIconBundle GetIconBundle(const wxArtID& id, const wxArtClient& client = wxART_OTHER);

	static wxSize GetIconSize(iconSize size, bool userScaled = false);

	// Note: Always 1 on OS X
	static double GetUIScaleFactor();

	static CThemeProvider* Get();

	wxAnimation CreateAnimation(wxArtID const& id, wxSize const& size);

	virtual wxBitmap CreateBitmap(wxArtID const& id, wxArtClient const& client, wxSize const& size) override {
		return CreateBitmap(id, client, size, false);
	}
	wxBitmap CreateBitmap(wxArtID const& id, wxArtClient const& client, wxSize const& size, bool allowDummy);

	wxStaticBitmap* createStaticBitmap(wxWindow* parent, std::wstring const& name, iconSize s);

private:
	wxBitmap const& GetEmpty(wxSize const& size);

	virtual void OnOptionsChanged(watched_options const& options) override;

	COptions& options_;
	CLocalPath cacheDir_;
	std::map<std::wstring, CTheme> themes_;
	std::map<wxSize, wxBitmap, wxSize_cmp> emptyBitmaps_;
};

#ifdef __WXMSW__
class ProperlyScaledBitmapBundle final : public wxBitmapBundleImpl
{
public:
	ProperlyScaledBitmapBundle(wxBitmap const& bmp, double scale)
		: bmp_(bmp)
		, scale_(scale)
	{
	}

	wxSize GetDefaultSize() const override { return bmp_.GetSize() / scale_; }
	wxSize GetPreferredBitmapSizeAtScale(double scale) const override {
		return GetDefaultSize() * scale;
	}

	wxBitmap GetBitmap(const wxSize& size) override {
		if (size != bmp_.GetSize()) {
			wxImage img = bmp_.ConvertToImage();
			img.Rescale(size.x, size.y, wxIMAGE_QUALITY_HIGH);
			return wxBitmap(img);
		}
		return bmp_;
	}

private:
	wxBitmap bmp_;
	double scale_;
};

inline wxBitmapBundle MakeBmpBundle(wxBitmap const& bmp) {
	return wxBitmapBundle::FromImpl(new ProperlyScaledBitmapBundle(bmp, CThemeProvider::GetUIScaleFactor()));
}
#else
#define MakeBmpBundle(x) x
#endif

#endif
