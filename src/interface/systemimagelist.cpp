#include "filezilla.h"
#include "systemimagelist.h"

#include "file_utils.h"
#include "themeprovider.h"
#ifdef __WXMSW__
#include "shlobj.h"
#else
#include <wx/mimetype.h>
#include "graphics.h"
#endif

wxImageListEx::wxImageListEx()
	: wxImageList()
{
}

wxImageListEx::wxImageListEx(int width, int height, const bool mask, int initialCount)
	: wxImageList(width, height, mask, initialCount)
{
}

#ifdef __WXMSW__
HIMAGELIST wxImageListEx::Detach()
{
	 HIMAGELIST hImageList = (HIMAGELIST)m_hImageList;
	 m_hImageList = 0;
	 return hImageList;
}
#endif

#ifndef __WXMSW__
static void OverlaySymlink(wxBitmap& bmp)
{
	wxBitmap overlay = CThemeProvider::Get()->CreateBitmap(_T("ART_SYMLINK"),  wxART_OTHER, wxSize(bmp.GetScaledWidth(), bmp.GetScaledHeight()));
	Overlay(bmp, overlay);
}
#endif

CSystemImageList::CSystemImageList(int size)
{
	if (size != -1) {
		CreateSystemImageList(size);
	}
}

bool CSystemImageList::CreateSystemImageList(int size)
{
	if (m_pImageList) {
		return true;
	}

#ifdef __WXMSW__
	auto const getImageList = [&size](wchar_t const* buffer) {
		SHFILEINFO shFinfo{};
		return reinterpret_cast<HIMAGELIST>(SHGetFileInfo(buffer,
			0,
			&shFinfo,
			sizeof(shFinfo),
			SHGFI_SYSICONINDEX |
			((size != CThemeProvider::GetIconSize(iconSizeSmall).x) ? SHGFI_ICON : SHGFI_SMALLICON)));
	};

	HIMAGELIST imageList{};

	wchar_t* out{};
	if (SHGetKnownFolderPath(FOLDERID_Windows, 0, 0, &out) == S_OK) {
		imageList = getImageList(out);
		CoTaskMemFree(out);
	}
	if (!imageList && SHGetKnownFolderPath(FOLDERID_Profile, 0, 0, &out) == S_OK) {
		imageList = getImageList(out);
		CoTaskMemFree(out);
	}
	if (!imageList) {
		imageList = getImageList(L"C:\\");
	}

	if (!imageList) {
		return false;
	}

	m_pImageList = new wxImageListEx(reinterpret_cast<WXHIMAGELIST>(imageList));
#else
	m_pImageList = new wxImageListEx(size, size);

	wxBitmap file = CThemeProvider::Get()->CreateBitmap(L"ART_FILE",  wxART_OTHER, wxSize(size, size));
	wxBitmap folderclosed = CThemeProvider::Get()->CreateBitmap(L"ART_FOLDERCLOSED",  wxART_OTHER, wxSize(size, size));
	wxBitmap folder = CThemeProvider::Get()->CreateBitmap(L"ART_FOLDER",  wxART_OTHER, wxSize(size, size));
	m_pImageList->Add(file);
	m_pImageList->Add(folderclosed);
	m_pImageList->Add(folder);
	OverlaySymlink(file);
	OverlaySymlink(folderclosed);
	OverlaySymlink(folder);
	m_pImageList->Add(file);
	m_pImageList->Add(folderclosed);
	m_pImageList->Add(folder);
#endif

	return true;
}

CSystemImageList::~CSystemImageList()
{
	if (!m_pImageList) {
		return;
	}

#ifdef __WXMSW__
	m_pImageList->Detach();
#endif

	delete m_pImageList;

	m_pImageList = 0;
}

#ifndef __WXMSW__
// This function converts to the right size with the given background colour
wxBitmap PrepareIcon(wxIcon icon, wxSize size)
{
	if (icon.GetWidth() == size.GetWidth() && icon.GetHeight() == size.GetHeight()) {
		return icon;
	}
	wxBitmap bmp;
	bmp.CopyFromIcon(icon);
	return bmp.ConvertToImage().Rescale(size.GetWidth(), size.GetHeight());
}
#endif

int CSystemImageList::GetIconIndex(iconType type, std::wstring const& fileName, bool physical, bool symlink)
{
	if (!m_pImageList) {
		return -1;
	}

#ifdef __WXMSW__
	(void)symlink;

	if (fileName.empty()) {
		physical = false;
	}

	SHFILEINFO shFinfo;
	memset(&shFinfo, 0, sizeof(SHFILEINFO));
	if (SHGetFileInfo(!fileName.empty() ? fileName.c_str() : L"{B97D3074-1830-4b4a-9D8A-17A38B074052}",
		(type != iconType::file) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL,
		&shFinfo,
		sizeof(SHFILEINFO),
		SHGFI_ICON | ((type == iconType::opened_dir) ? SHGFI_OPENICON : 0) | ((physical) ? 0 : SHGFI_USEFILEATTRIBUTES) ) )
	{
		int icon = shFinfo.iIcon;
		// we only need the index from the system image list
		DestroyIcon(shFinfo.hIcon);
		return icon;
	}
#else
	(void)physical;

	int icon;
	switch (type)
	{
	case iconType::file:
	default:
		icon = symlink ? 3 : 0;
		break;
	case iconType::dir:
		return symlink ? 4 : 1;
	case iconType::opened_dir:
		return symlink ? 5 : 2;
	}

	std::wstring ext = GetExtension(fileName);
	if (ext.empty() || ext == L".") {
		return icon;
	}

	if (symlink) {
		auto cacheIter = m_iconSymlinkCache.find(ext);
		if (cacheIter != m_iconSymlinkCache.cend()) {
			return cacheIter->second;
		}
	}
	else {
		auto cacheIter = m_iconCache.find(ext);
		if (cacheIter != m_iconCache.cend()) {
			return cacheIter->second;
		}
	}

	wxFileType *pType = wxTheMimeTypesManager->GetFileTypeFromExtension(ext);
	if (!pType) {
		if (symlink) {
			m_iconSymlinkCache[ext] = icon;
		}
		else {
			m_iconCache[ext] = icon;
		}
		return icon;
	}

	wxIconLocation loc;
	if (pType->GetIcon(&loc) && loc.IsOk()) {
		wxLogNull nul;
		wxIcon newIcon(loc);

		if (newIcon.Ok()) {
			wxBitmap bmp = PrepareIcon(newIcon, CThemeProvider::GetIconSize(iconSizeSmall));
			if (symlink) {
				OverlaySymlink(bmp);
			}
			int index = m_pImageList->Add(bmp);
			if (index > 0) {
				icon = index;
			}
		}
	}
	delete pType;

	if (symlink) {
		m_iconSymlinkCache[ext] = icon;
	}
	else {
		m_iconCache[ext] = icon;
	}
	return icon;
#endif
	return -1;
}

#ifdef __WXMSW__
int CSystemImageList::GetLinkOverlayIndex()
{
	static int overlay = -1;
	if (overlay == -1) {
		overlay = SHGetIconOverlayIndex(0, IDO_SHGIOI_LINK);
		if (overlay < 0) {
			overlay = 0;
		}
	}

	return overlay;
}
#endif
