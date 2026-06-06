#ifndef FILEZILLA_INTERFACE_FILELISTCTRL_HEADER
#define FILEZILLA_INTERFACE_FILELISTCTRL_HEADER

#include "listctrlex.h"
#include "systemimagelist.h"
#include "listingcomparison.h"

#include <cstring>
#include <deque>
#include <memory>

class CQueueView;
class CFileListCtrl_SortComparisonObject;
class CFilelistStatusBar;
#if defined(__WXGTK__) && !defined(__WXGTK3__)
class CGtkEventCallbackProxyBase;
#endif

class CGenericFileData
{
public:
	std::wstring fileType;
	int icon{-2};

	// t_fileEntryFlags is defined in listingcomparison.h as it will be used for
	// both local and remote listings
	CComparableListing::t_fileEntryFlags comparison_flags{CComparableListing::normal};
};

class CFileListCtrlSortBase
{
public:
	CFileListCtrlSortBase() = default;
	CFileListCtrlSortBase(CFileListCtrlSortBase const&) = delete;
	CFileListCtrlSortBase& operator=(CFileListCtrlSortBase const&) = delete;

	enum DirSortMode
	{
		dirsort_ontop,
		dirsort_onbottom,
		dirsort_inline
	};

	virtual bool operator()(int a, int b) const = 0;
	virtual ~CFileListCtrlSortBase() {} // Without this empty destructor GCC complains

	#define CMP(f, data1, data2) \
		{\
			int res = this->f(data1, data2);\
			if (res < 0)\
				return true;\
			else if (res > 0)\
				return false;\
		}

	#define CMP_LESS(f, data1, data2) \
		{\
			int res = this->f(data1, data2);\
			if (res < 0)\
				return true;\
			else\
				return false;\
		}

	static int CmpCase(std::wstring_view const& str1, std::wstring_view const& str2)
	{
		return str1.compare(str2);
	}

	static int CmpNoCase(std::wstring_view const& str1, std::wstring_view const& str2)
	{
		int cmp = fz::stricmp(str1, str2);
		if (cmp) {
			return cmp;
		}
		return str1.compare(str2);
	}

	static size_t first_digit(std::wstring_view const& s)
	{
		for (size_t i = 0; i < s.size(); ++i) {
			if (s[i] >= '0' && s[i] <= '9') {
				return i;
			}
		}
		return size_t(-1);
	}

	static size_t first_nondigit(std::wstring_view const& s)
	{
		for (size_t i = 0; i < s.size(); ++i) {
			if (s[i] < '0' || s[i] > '9') {
				return i;
			}
		}
		return size_t(-1);
	}

	static int CmpNatural(std::wstring_view const& s1, std::wstring_view const& s2)
	{
		std::wstring_view str1 = s1;
		std::wstring_view str2 = s2;
		while (true) {
			size_t p1 = first_digit(str1);
			size_t p2 = first_digit(str2);
			if (p1 == size_t(-1) || p2 == size_t(-1)) {
				return fz::stricmp(str1, str2);
			}
			auto cmp = fz::stricmp(str1.substr(0, p1), str2.substr(0, p2));
			if (cmp) {
				return cmp;
			}

			str1.remove_prefix(p1);
			str2.remove_prefix(p1);

			// At this point both strings start with a number and any prior prefix was the same

			p1 = first_nondigit(str1);
			p2 = first_nondigit(str2);

			auto n1 = str1.substr(0, p1);
			auto n2 = str2.substr(0, p2);
			str1.remove_prefix(n1.size());
			str2.remove_prefix(n2.size());

			int leading_zeros{};
			size_t i{}, j{};
			for (; i < n1.size() - 1; ++i) {
				if (n1[i] != '0') {
					break;
				}
				++leading_zeros;
			}
			n1.remove_prefix(i);
			for (; j < n2.size() - 1; ++j) {
				if (n2[j] != '0') {
					break;
				}
				--leading_zeros;
			}
			n2.remove_prefix(j);
			if (n1.size() > n2.size()) {
				return 1;
			}
			else if (n1.size() < n2.size()) {
				return -1;
			}

			for (i = 0; i < n1.size(); ++i) {
				auto diff = n1[i] - n2[i];
				if (diff) {
					return diff;
				}
			}
			if (leading_zeros) {
				return leading_zeros;
			}
		}

		return 0;
	}

	typedef int (* CompareFunction)(std::wstring_view const&, std::wstring_view const&);
	static CompareFunction GetCmpFunction(NameSortMode mode)
	{
		switch (mode)
		{
		default:
		case NameSortMode::case_insensitive:
			return &CFileListCtrlSortBase::CmpNoCase;
		case NameSortMode::case_sensitive:
			return &CFileListCtrlSortBase::CmpCase;
		case NameSortMode::natural:
			return &CFileListCtrlSortBase::CmpNatural;
		}
	}
};

// Helper classes for fast sorting using std::sort
// -----------------------------------------------

template<typename value_type>
inline int DoCmpName(value_type const& data1, value_type const& data2, NameSortMode const nameSortMode)
{
	switch (nameSortMode)
	{
	case NameSortMode::case_sensitive:
		return CFileListCtrlSortBase::CmpCase(data1.name, data2.name);

	default:
	case NameSortMode::case_insensitive:
		return CFileListCtrlSortBase::CmpNoCase(data1.name, data2.name);

	case NameSortMode::natural:
		return CFileListCtrlSortBase::CmpNatural(data1.name, data2.name);
	}
}

template<typename Listing>
class CFileListCtrlSort : public CFileListCtrlSortBase
{
public:
	typedef Listing List;
	typedef typename Listing::value_type value_type;

	CFileListCtrlSort(Listing const& listing, DirSortMode dirSortMode, NameSortMode nameSortMode)
		: m_listing(listing), m_dirSortMode(dirSortMode), m_nameSortMode(nameSortMode)
	{
	}

	inline int CmpDir(value_type const& data1, value_type const& data2) const
	{
		switch (m_dirSortMode)
		{
		default:
		case dirsort_ontop:
			if (data1.is_dir()) {
				if (!data2.is_dir()) {
					return -1;
				}
				else {
					return 0;
				}
			}
			else {
				if (data2.is_dir()) {
					return 1;
				}
				else {
					return 0;
				}
			}
		case dirsort_onbottom:
			if (data1.is_dir()) {
				if (!data2.is_dir()) {
					return 1;
				}
				else {
					return 0;
				}
			}
			else {
				if (data2.is_dir()) {
					return -1;
				}
				else {
					return 0;
				}
			}
		case dirsort_inline:
			return 0;
		}
	}

	template<typename value_type>
	inline int CmpName(value_type const& data1, value_type const& data2) const
	{
		return DoCmpName(data1, data2, m_nameSortMode);
	}

	inline int CmpSize(const value_type &data1, const value_type &data2) const
	{
		int64_t const diff = data1.size - data2.size;
		if (diff < 0) {
			return -1;
		}
		else if (diff > 0) {
			return 1;
		}
		else {
			return 0;
		}
	}

	inline int CmpStringNoCase(std::wstring_view const& data1, std::wstring_view const& data2) const
	{
		return fz::stricmp(data1, data2);
	}

	inline int CmpTime(const value_type &data1, const value_type &data2) const
	{
		if (data1.time < data2.time) {
			return -1;
		}
		else if (data1.time > data2.time) {
			return 1;
		}
		else {
			return 0;
		}
	}

protected:
	Listing const& m_listing;

	DirSortMode const m_dirSortMode;
	NameSortMode const m_nameSortMode;
};

template<class CFileData> class CFileListCtrl;

template<class T, typename DataEntry> class CReverseSort final : public T
{
public:
	CReverseSort(typename T::List const& listing, std::vector<DataEntry>& fileData, CFileListCtrlSortBase::DirSortMode dirSortMode, NameSortMode nameSortMode, CFileListCtrl<DataEntry>* const pListView)
		: T(listing, fileData, dirSortMode, nameSortMode, pListView)
	{
	}

	inline bool operator()(int a, int b) const
	{
		return T::operator()(b, a);
	}
};

template<typename Listing, typename DataEntry>
class CFileListCtrlSortName : public CFileListCtrlSort<Listing>
{
public:
	CFileListCtrlSortName(Listing const& listing, std::vector<DataEntry>&, CFileListCtrlSortBase::DirSortMode dirSortMode, NameSortMode nameSortMode, CFileListCtrl<DataEntry>* const)
		: CFileListCtrlSort<Listing>(listing, dirSortMode, nameSortMode)
	{
	}

	bool operator()(int a, int b) const
	{
		typename Listing::value_type const& data1 = this->m_listing[a];
		typename Listing::value_type const& data2 = this->m_listing[b];

		CMP(CmpDir, data1, data2);

		CMP_LESS(CmpName, data1, data2);
	}
};

template<typename Listing, typename DataEntry>
class CFileListCtrlSortSize : public CFileListCtrlSort<Listing>
{
public:
	CFileListCtrlSortSize(Listing const& listing, std::vector<DataEntry>&, CFileListCtrlSortBase::DirSortMode dirSortMode, NameSortMode nameSortMode, CFileListCtrl<DataEntry>* const)
		: CFileListCtrlSort<Listing>(listing, dirSortMode, nameSortMode)
	{
	}

	bool operator()(int a, int b) const
	{
		typename Listing::value_type const& data1 = this->m_listing[a];
		typename Listing::value_type const& data2 = this->m_listing[b];

		CMP(CmpDir, data1, data2);

		CMP(CmpSize, data1, data2);

		CMP_LESS(CmpName, data1, data2);
	}
};

template<typename Listing, typename DataEntry>
class CFileListCtrlSortType : public CFileListCtrlSort<Listing>
{
public:
	CFileListCtrlSortType(Listing const& listing, std::vector<DataEntry>& fileData, CFileListCtrlSortBase::DirSortMode dirSortMode, NameSortMode nameSortMode, CFileListCtrl<DataEntry>* const pListView)
		: CFileListCtrlSort<Listing>(listing, dirSortMode, nameSortMode), m_pListView(pListView), m_fileData(fileData)
	{
	}

	bool operator()(int a, int b) const
	{
		typename Listing::value_type const& data1 = this->m_listing[a];
		typename Listing::value_type const& data2 = this->m_listing[b];

		CMP(CmpDir, data1, data2);

		DataEntry &type1 = m_fileData[a];
		DataEntry &type2 = m_fileData[b];
		if (type1.fileType.empty()) {
			type1.fileType = m_pListView->GetType(data1.name, data1.is_dir());
		}
		if (type2.fileType.empty()) {
			type2.fileType = m_pListView->GetType(data2.name, data2.is_dir());
		}

		CMP(CmpStringNoCase, type1.fileType, type2.fileType);

		CMP_LESS(CmpName, data1, data2);
	}

protected:
	CFileListCtrl<DataEntry>* const m_pListView;
	std::vector<DataEntry>& m_fileData;
};

template<typename Listing, typename DataEntry>
class CFileListCtrlSortTime : public CFileListCtrlSort<Listing>
{
public:
	CFileListCtrlSortTime(Listing const& listing, std::vector<DataEntry>&, CFileListCtrlSortBase::DirSortMode dirSortMode, NameSortMode nameSortMode, CFileListCtrl<DataEntry>* const)
		: CFileListCtrlSort<Listing>(listing, dirSortMode, nameSortMode)
	{
	}

	bool operator()(int a, int b) const
	{
		typename Listing::value_type const& data1 = this->m_listing[a];
		typename Listing::value_type const& data2 = this->m_listing[b];

		CMP(CmpDir, data1, data2);

		CMP(CmpTime, data1, data2);

		CMP_LESS(CmpName, data1, data2);
	}
};

template<typename Listing, typename DataEntry>
class CFileListCtrlSortPermissions : public CFileListCtrlSort<Listing>
{
public:
	CFileListCtrlSortPermissions(Listing const& listing, std::vector<DataEntry>&, CFileListCtrlSortBase::DirSortMode dirSortMode, NameSortMode nameSortMode, CFileListCtrl<DataEntry>* const)
		: CFileListCtrlSort<Listing>(listing, dirSortMode, nameSortMode)
	{
	}

	bool operator()(int a, int b) const
	{
		typename Listing::value_type const& data1 = this->m_listing[a];
		typename Listing::value_type const& data2 = this->m_listing[b];

		CMP(CmpDir, data1, data2);

		CMP(CmpStringNoCase, *data1.permissions, *data2.permissions);

		CMP_LESS(CmpName, data1, data2);
	}
};

template<typename Listing, typename DataEntry>
class CFileListCtrlSortOwnerGroup : public CFileListCtrlSort<Listing>
{
public:
	CFileListCtrlSortOwnerGroup(Listing const& listing, std::vector<DataEntry>&, CFileListCtrlSortBase::DirSortMode dirSortMode, NameSortMode nameSortMode, CFileListCtrl<DataEntry>* const)
		: CFileListCtrlSort<Listing>(listing, dirSortMode, nameSortMode)
	{
	}

	bool operator()(int a, int b) const
	{
		typename Listing::value_type const& data1 = this->m_listing[a];
		typename Listing::value_type const& data2 = this->m_listing[b];

		CMP(CmpDir, data1, data2);

		CMP(CmpStringNoCase, *data1.ownerGroup, *data2.ownerGroup);

		CMP_LESS(CmpName, data1, data2);
	}
};

template<typename Listing, typename DataEntry>
class CFileListCtrlSortPath : public CFileListCtrlSort<Listing>
{
public:
	CFileListCtrlSortPath(Listing const& listing, std::vector<DataEntry>& fileData, CFileListCtrlSortBase::DirSortMode dirSortMode, NameSortMode nameSortMode, CFileListCtrl<DataEntry>* const)
		: CFileListCtrlSort<Listing>(listing, dirSortMode, nameSortMode)
		, m_fileData(fileData)
	{
	}

	bool operator()(int a, int b) const
	{
		typename Listing::value_type const& data1 = this->m_listing[a];
		typename Listing::value_type const& data2 = this->m_listing[b];

		int res = data1.path.compare_case(data2.path);
		if (res) {
			return res < 0;
		}

		CMP_LESS(CmpName, data1, data2);
	}
	std::vector<DataEntry>& m_fileData;
};

template<typename Listing, typename DataEntry>
class CFileListCtrlSortNamePath : public CFileListCtrlSort<Listing>
{
public:
	CFileListCtrlSortNamePath(Listing const& listing, std::vector<DataEntry>& fileData, CFileListCtrlSortBase::DirSortMode dirSortMode, NameSortMode nameSortMode, CFileListCtrl<DataEntry>* const)
		: CFileListCtrlSort<Listing>(listing, dirSortMode, nameSortMode)
		, m_fileData(fileData)
	{
	}

	bool operator()(int a, int b) const
	{
		typename Listing::value_type const& data1 = this->m_listing[a];
		typename Listing::value_type const& data2 = this->m_listing[b];

		CMP(CmpDir, data1, data2);
		CMP(CmpName, data1, data2);

		return data1.path.compare_case(data2.path) < 0;
	}
	std::vector<DataEntry>& m_fileData;
};

namespace genericTypes {
	enum type {
		file,
		directory
	};
}

class COptionsBase;
class TimeFormatter;
class login_manager;
template<class CFileData> class CFileListCtrl : public wxListCtrlEx, public CComparableListing
{
	template<typename Listing, typename DataEntry> friend class CFileListCtrlSortType;
public:
	CFileListCtrl(wxWindow* pParent, CQueueView *pQueue, COptionsBase & options, TimeFormatter & time_formatter, bool border = false);
	virtual ~CFileListCtrl() = default;

	void SetFilelistStatusBar(CFilelistStatusBar* pFilelistStatusBar) { m_pFilelistStatusBar = pFilelistStatusBar; }
	CFilelistStatusBar* GetFilelistStatusBar() { return m_pFilelistStatusBar; }

	void ClearSelection();

	virtual void OnNavigationEvent(bool) {}

	std::vector<unsigned int> const& indexMapping() const { return m_indexMapping; }

protected:
	CQueueView *m_pQueue{};

	std::vector<CFileData> m_fileData;
	std::vector<unsigned int> m_indexMapping;
	std::vector<unsigned int> m_originalIndexMapping; // m_originalIndexMapping will only be set on comparisons

	virtual bool ItemIsDir(int index) const = 0;
	virtual int64_t ItemGetSize(int index) const = 0;

	std::map<std::wstring, std::wstring> m_fileTypeMap;

	// The .. item
	bool m_hasParent{true};

	int m_sortColumn{-1};
	int m_sortDirection{};

	void InitSort(interfaceOptions optionID); // Has to be called after initializing columns
	void SortList(int column = -1, int direction = -1, bool updateSelections = true);
	CFileListCtrlSortBase::DirSortMode GetDirSortMode();
	NameSortMode GetNameSortMode();
	virtual void UpdateSortComparisonObject() = 0;
	CFileListCtrlSortBase& GetSortComparisonObject();

	// An empty path denotes a virtual file
	std::wstring GetType(std::wstring const& name, bool dir, std::wstring const& path = std::wstring());

	// Comparison related
	virtual void ScrollTopItem(int item);
	virtual void OnPostScroll();
	virtual void OnExitComparisonMode();
	virtual void CompareAddFile(t_fileEntryFlags flags);

	int m_comparisonIndex{-1};

	// Remembers which non-fill items are selected if enabling/disabling comparison.
	// Exploit fact that sort order doesn't change -> O(n)
	void ComparisonRememberSelections();
	void ComparisonRestoreSelections();
	std::deque<int> m_comparisonSelections;

	CFilelistStatusBar* m_pFilelistStatusBar{};

	// Indexes of the items added, sorted ascending.
	void UpdateSelections_ItemsAdded(std::vector<int> const& added_indexes);

#ifndef __WXMSW__
	// Generic wxListCtrl does not support wxLIST_STATE_DROPHILITED, emulate it
	wxListItemAttr m_dropHighlightAttribute;
#endif

	void SetSelection(int item, bool select);
#ifndef __WXMSW__
	// Used by selection tracking
	void SetItemCount(int count);
#endif

#ifdef __WXMSW__
	virtual int GetOverlayIndex(int) { return 0; }
#endif

private:
	void UpdateSelections(int min, int max);

	void SortList_UpdateSelections(bool* selections, int focused_item, unsigned int focused_index);

	// If this is set to true, don't process selection changed events
	bool m_insideSetSelection{};

#ifdef __WXMSW__
	virtual WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam);
	virtual bool MSWOnNotify(int idCtrl, WXLPARAM lParam, WXLPARAM *result);
#else
	int m_focusItem{-1};
	std::vector<bool> m_selections;
	int m_pending_focus_processing{};
	bool updateAllSelections_{};
#endif

#if defined(__WXGTK__) && !defined(__WXGTK3__)
	std::unique_ptr<CGtkEventCallbackProxyBase> m_gtkEventCallbackProxy;
#endif

	std::wstring m_genericTypes[2];

	DECLARE_EVENT_TABLE()
	void OnColumnClicked(wxListEvent &event);
	void OnColumnRightClicked(wxListEvent& event);
	void OnItemSelected(wxListEvent& event);
	void OnItemDeselected(wxListEvent& event);
#ifndef __WXMSW__
	void OnFocusChanged(wxListEvent& event);
	void OnProcessFocusChange(wxCommandEvent& event);
	void OnLeftDown(wxMouseEvent& event);
	void OnProcessMouseEvent(wxCommandEvent& event);
#endif
	void OnKeyDown(wxKeyEvent& event);
	void OnColorChange(wxSysColourChangedEvent & ev);

	COptionsBase& options_;
	TimeFormatter & time_formatter_;

	std::unique_ptr<CFileListCtrlSortBase> sortComparisonObject_;
};

class SortPredicate
{
public:
	SortPredicate() = delete;
	SortPredicate(std::unique_ptr<CFileListCtrlSortBase> const& ref)
		: p_(*ref)
	{}
	SortPredicate(CFileListCtrlSortBase const& ref)
	    : p_(ref)
	{}

	inline bool operator()(int lhs, int rhs) {
		return p_(lhs, rhs);
	}

public:
	CFileListCtrlSortBase const& p_;
};

#ifdef FILELISTCTRL_INCLUDE_TEMPLATE_DEFINITION
#include "filelistctrl.cpp"
#endif

#endif
