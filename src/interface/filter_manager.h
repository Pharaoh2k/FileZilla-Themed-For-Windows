#ifndef FILEZILLA_INTERFACE_FILTER_MANAGER_HEADER
#define FILEZILLA_INTERFACE_FILTER_MANAGER_HEADER

#include "dialogex.h"
#include "../commonui/filter.h"

class CFilterManager : public filter_manager
{
public:
	CFilterManager();
	virtual ~CFilterManager() = default;

	// Note: Under non-windows, attributes are permissions
	bool FilenameFiltered(std::wstring const& name, std::wstring const& path, bool dir, int64_t size, bool local, int attributes, fz::datetime const& date) const override;
	using filter_manager::FilenameFiltered; //also get the other function with same name to scope
	static bool HasActiveFilters(bool ignore_disabled = false);

	bool HasSameLocalAndRemoteFilters() const;

	static void ToggleFilters();

	ActiveFilters GetActiveFilters();

	bool HasActiveLocalFilters() const;
	bool HasActiveRemoteFilters() const;

	static void Import(pugi::xml_node& element);
	static void LoadFilters(pugi::xml_node& element);

protected:
	static void LoadFilters();
	static void SaveFilters();

	static bool m_loaded;

	static filter_data global_filters_;

	static bool m_filters_disabled;
};

class CMainFrame;
class CFilterDialog final : public wxDialogEx, public CFilterManager
{
public:
	CFilterDialog();

	bool Create(CMainFrame* parent);

protected:
	void DisplayFilters();

	void SetCtrlState();

	DECLARE_EVENT_TABLE()
	void OnOkOrApply(wxCommandEvent& event);
	void OnCancel(wxCommandEvent& event);
	void OnEdit(wxCommandEvent& event);
	void OnFilterSelect(wxCommandEvent& event);
	void OnMouseEvent(wxMouseEvent& event);
	void OnKeyEvent(wxKeyEvent& event);
	void OnSaveAs(wxCommandEvent& event);
	void OnRename(wxCommandEvent& event);
	void OnDeleteSet(wxCommandEvent& event);
	void OnSetSelect(wxCommandEvent& event);

	void OnChangeAll(wxCommandEvent& event);

	bool m_shiftClick{};

	CMainFrame* m_pMainFrame{};

	std::vector<CFilter> m_filters;
	std::vector<CFilterSet> m_filterSets;
	unsigned int m_currentFilterSet;
};

#endif
