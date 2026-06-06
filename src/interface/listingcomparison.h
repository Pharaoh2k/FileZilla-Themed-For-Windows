#ifndef FILEZILLA_INTERFACE_LISTINGCOMPARISON_HEADER
#define FILEZILLA_INTERFACE_LISTINGCOMPARISON_HEADER

#include <wx/listctrl.h>

class COptionsBase;

enum class NameSortMode
{
	case_insensitive,
	case_sensitive,
	natural
};

class CComparisonManager;
class CComparableListing
{
	friend class CComparisonManager;
public:
	CComparableListing(wxWindow* pParent);
	virtual ~CComparableListing() = default;

	enum t_fileEntryFlags
	{
		normal = 1,
		fill = 2,
		different = 4,
		newer = 8,
		lonely = 16
	};

	virtual bool CanStartComparison() = 0;
	virtual void StartComparison() = 0;
	virtual bool get_next_file(std::wstring_view & name, std::wstring & path, bool &dir, int64_t &size, fz::datetime& date) = 0;
	virtual void CompareAddFile(t_fileEntryFlags flags) = 0;
	virtual void FinishComparison() = 0;
	virtual void ScrollTopItem(int item) = 0;
	virtual void OnExitComparisonMode() = 0;

	void RefreshComparison();
	void ExitComparisonMode();

	bool IsComparing() const;

	void SetOther(CComparableListing* pOther) { m_pOther = pOther; }
	CComparableListing* GetOther() { return m_pOther; }

	void InitColors();
protected:

	wxListItemAttr m_comparisonBackgrounds[3];

private:
	wxWindow* m_pParent{};

	CComparableListing* m_pOther{};
	CComparisonManager* m_pComparisonManager{};
};

class CState;
class CComparisonManager final
{
public:
	CComparisonManager(CState& state, COptionsBase & options);

	bool CompareListings();
	bool IsComparing() const { return m_isComparing; }

	void ExitComparisonMode();

	void SetListings(CComparableListing* pLeft, CComparableListing* pRight);

	CComparableListing* LeftListing() const { return m_pLeft; }
	CComparableListing* RightListing() const { return m_pRight; }

	void SetComparisonMode(int mode) { m_comparisonMode = mode; }
	void SetHideIdentical(bool hideIdentical) { m_hideIdentical = hideIdentical; }

protected:
	int CompareFiles(int const dirSortMode, NameSortMode const nameSortMode, std::wstring_view const& local_path, std::wstring_view const& local, std::wstring_view const& remote_path, std::wstring_view const& remote, bool localDir, bool remoteDir);

	CState& m_state;
	COptionsBase & options_;

	// Left/right, first/second, a/b, doesn't matter
	CComparableListing* m_pLeft{};
	CComparableListing* m_pRight{};

	bool m_isComparing{};
	int m_comparisonMode{};
	bool m_hideIdentical{};
};

#endif
