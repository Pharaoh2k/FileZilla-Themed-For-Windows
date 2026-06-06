#ifndef FILEZILLA_INTERFACE_SYSTEMIMAGELIST_HEADER
#define FILEZILLA_INTERFACE_SYSTEMIMAGELIST_HEADER

#include "filezilla.h"

#ifdef __WXMSW__
#include <shellapi.h>
#include <commctrl.h>
#else
#include <map>
#endif

enum class iconType
{
	file,
	dir,
	opened_dir
};

// Required wxImageList extension
class wxImageListEx final : public wxImageList
{
public:
	wxImageListEx();
	wxImageListEx(int width, int height, const bool mask = true, int initialCount = 1);

#ifdef __WXMSW__
	wxImageListEx(WXHIMAGELIST hList) { m_hImageList = hList; }
	HIMAGELIST GetHandle() const { return reinterpret_cast<HIMAGELIST>(m_hImageList); }
	HIMAGELIST Detach();
#endif
};

class CSystemImageList
{
public:
	CSystemImageList(int size = -1);
	virtual ~CSystemImageList();

	CSystemImageList(CSystemImageList const&) = delete;
	CSystemImageList& operator=(CSystemImageList const&) = delete;

	bool CreateSystemImageList(int size);

	wxImageList* GetSystemImageList() { return m_pImageList; }

	int GetIconIndex(iconType type, std::wstring const& fileName = std::wstring(), bool physical = true, bool symlink = false);

#ifdef __WXMSW__
	int GetLinkOverlayIndex();
#endif

private:
	wxImageListEx *m_pImageList{};

#ifndef __WXMSW__
	std::map<std::wstring, int> m_iconCache;
	std::map<std::wstring, int> m_iconSymlinkCache;
#endif
};

#endif
