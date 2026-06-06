#ifndef FILEZILLA_INTERFACE_TREECTRLEX_HEADER
#define FILEZILLA_INTERFACE_TREECTRLEX_HEADER

#include <wx/dnd.h>
#include "filelistctrl.h"

#ifndef __WXMAC__
	#define DEFAULT_TREE_STYLE wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT
#else
	#define DEFAULT_TREE_STYLE wxTR_HAS_BUTTONS | wxTR_NO_LINES
#endif

class wxTreeCtrlEx : public wxNavigationEnabled<wxTreeCtrl>
{
	wxDECLARE_CLASS(wxTreeCtrlEx); // Needed for OnCompareItems to work on Windows. Bad library design, why not use normal RTTI?
public:
	typedef wxTreeItemId Item;

	wxTreeCtrlEx();
	wxTreeCtrlEx(wxWindow *parent, wxWindowID id = wxID_ANY,
			   const wxPoint& pos = wxDefaultPosition,
			   const wxSize& size = wxDefaultSize,
			   long style = DEFAULT_TREE_STYLE);
	void SafeSelectItem(wxTreeItemId const& item, bool clearSelection = true);

	// Small wrappers to make wxTreeCtrl(Ex) API more similar to wxListCtrl(ex).
	int GetItemCount() const { return GetCount(); }
	wxTreeItemId GetTopItem() const { return GetFirstVisibleItem(); }
	bool GetItemRect(wxTreeItemId const& item, wxRect &rect) const { return GetBoundingRect(item, rect); }

	wxRect GetActualClientRect() const { return GetClientRect(); }

	bool Valid(wxTreeItemId const& i) const { return i.IsOk(); }

	wxWindow* GetMainWindow() { return this; }

	virtual wxTreeItemId GetSelection() const override;

	// wxTreeCtrl::GetSelections has an atrocious interface
	std::vector<wxTreeItemId> GetAllSelections() const;

	// Items with a collapsed ancestor are not included
	wxTreeItemId GetFirstItem() const;
	wxTreeItemId GetLastItem() const;
	wxTreeItemId GetBottomItem() const;

	wxTreeItemId GetNextItemSimple(wxTreeItemId const& item, bool includeCollapsed = false) const;
	wxTreeItemId GetPrevItemSimple(wxTreeItemId const& item) const;

	bool InPrefixSearch() const { return inPrefixSearch_; }

	void Resort();

	bool ShouldIgnoreChangeEvent() const { return ignore_change_event_ != 0; }

	virtual void Delete(wxTreeItemId const& item) override;
	virtual void DeleteAllItems() override;

	bool IsRelated(wxTreeItemId const& ancestor, wxTreeItemId child) const;

	wxTreeItemId GetHit(wxPoint const& point);

	wxTreeItemId DisplayDropHighlight(wxPoint const& p);
	wxTreeItemId DisplayDropHighlight(wxTreeItemId const& item);
	void ClearDropHighlight();

protected:

	bool inPrefixSearch_{};

	int m_setSelection{};
	int ignore_change_event_{};

#ifdef __WXMAC__
	wxDECLARE_EVENT_TABLE();
	void OnChar(wxKeyEvent& event);
#endif

	virtual int OnCompareItems(wxTreeItemId const& item1, wxTreeItemId const& item2) override;

	typedef int (*CompareFunction)(std::wstring_view const&, std::wstring_view const&);
	CompareFunction sortFunction_{};

	wxTreeItemId m_dropHighlight;
};

#endif
