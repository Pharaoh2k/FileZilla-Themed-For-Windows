#include "../filezilla.h"
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_filelists.h"
#include "../textctrlex.h"

#include <wx/statbox.h>

struct COptionsPageFilelists::impl final
{
	wxChoice* dirSortMode_{};
	wxChoice* nameSortMode_{};

	wxTextCtrlEx* threshold_{};

	wxChoice* doubleClickFileAction_{};
	wxChoice* doubleClickDirAction_{};
};

COptionsPageFilelists::COptionsPageFilelists()
	: impl_(std::make_unique<impl>())
{
}

COptionsPageFilelists::~COptionsPageFilelists()
{
}

bool COptionsPageFilelists::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	SetSizer(main);

	{
		auto [box, inner] = lay.createStatBox(main, _("Sorting"), 2);

		inner->Add(new wxStaticText(box, nullID, _("Sorting &mode:")), lay.valign);
		impl_->dirSortMode_ = new wxChoice(box, nullID);
		impl_->dirSortMode_->AppendString(_("Prioritize directories (default)"));
		impl_->dirSortMode_->AppendString(_("Keep directories on top"));
		impl_->dirSortMode_->AppendString(_("Sort directories inline"));
		inner->Add(impl_->dirSortMode_, lay.valign);

		inner->Add(new wxStaticText(box, nullID, _("&Name sorting mode:")), lay.valign);
		impl_->nameSortMode_ = new wxChoice(box, nullID);
#ifdef __WXMSW__
		impl_->nameSortMode_->AppendString(_("Case insensitive (default)"));
		impl_->nameSortMode_->AppendString(_("Case sensitive"));
#else
		impl_->nameSortMode_->AppendString(_("Case insensitive"));
		impl_->nameSortMode_->AppendString(_("Case sensitive (default)"));
#endif
		impl_->nameSortMode_->AppendString(_("Natural sort"));
		inner->Add(impl_->nameSortMode_, lay.valign);

	}

	{
		auto [box, inner] = lay.createStatBox(main, _("Directory comparison"), 1);
		inner->Add(new wxStaticText(box, nullID, _("If using timestamp based comparison, consider two files equal if their timestamp difference does not exceed this threshold.")));

		auto row = lay.createFlex(2);
		inner->Add(row);
		row->Add(new wxStaticText(box, nullID, _("Comparison &threshold (in minutes):")), lay.valign);
		impl_->threshold_ = new wxTextCtrlEx(box, nullID, wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(22), -1));
		impl_->threshold_->SetMaxLength(4);
		row->Add(impl_->threshold_, lay.valign);
	}

	{
		auto [box, inner] = lay.createStatBox(main, _("Double-click action"), 2);

		inner->Add(new wxStaticText(box, nullID, _("&Double-click action on files:")), lay.valign);
		impl_->doubleClickFileAction_ = new wxChoice(box, nullID);
		inner->Add(impl_->doubleClickFileAction_, lay.valign);
		impl_->doubleClickFileAction_->AppendString(_("Transfer"));
		impl_->doubleClickFileAction_->AppendString(_("Add to queue"));
		impl_->doubleClickFileAction_->AppendString(_("View/Edit"));
		impl_->doubleClickFileAction_->AppendString(_("None"));
		inner->Add(new wxStaticText(box, nullID, _("&Double-click action on directories:")), lay.valign);
		impl_->doubleClickDirAction_ = new wxChoice(box, nullID);
		impl_->doubleClickDirAction_->AppendString(_("Enter directory"));
		impl_->doubleClickDirAction_->AppendString(_("Transfer"));
		impl_->doubleClickDirAction_->AppendString(_("Add to queue"));
		impl_->doubleClickDirAction_->AppendString(_("None"));
		inner->Add(impl_->doubleClickDirAction_, lay.valign);
	}

	return true;
}

bool COptionsPageFilelists::LoadPage()
{
	impl_->dirSortMode_->Select(m_pOptions->get_int(OPTION_FILELIST_DIRSORT));
	impl_->nameSortMode_->Select(m_pOptions->get_int(OPTION_FILELIST_NAMESORT));

	impl_->threshold_->ChangeValue(m_pOptions->get_string(OPTION_COMPARISON_THRESHOLD));

	impl_->doubleClickFileAction_->Select(m_pOptions->get_int(OPTION_DOUBLECLICK_ACTION_FILE));
	impl_->doubleClickDirAction_->Select(m_pOptions->get_int(OPTION_DOUBLECLICK_ACTION_DIRECTORY));

	return true;
}

bool COptionsPageFilelists::SavePage()
{
	m_pOptions->set(OPTION_FILELIST_DIRSORT, impl_->dirSortMode_->GetSelection());
	m_pOptions->set(OPTION_FILELIST_NAMESORT, impl_->nameSortMode_->GetSelection());

	m_pOptions->set(OPTION_COMPARISON_THRESHOLD, impl_->threshold_->GetValue().ToStdWstring());
	
	m_pOptions->set(OPTION_DOUBLECLICK_ACTION_FILE, impl_->doubleClickFileAction_->GetSelection());
	m_pOptions->set(OPTION_DOUBLECLICK_ACTION_DIRECTORY, impl_->doubleClickDirAction_->GetSelection());

	return true;
}

bool COptionsPageFilelists::Validate()
{
	long minutes = 1;
	if (!impl_->threshold_->GetValue().ToLong(&minutes) || minutes < 0 || minutes > 1440) {
		return DisplayError(impl_->threshold_, _("Comparison threshold needs to be between 0 and 1440 minutes."));
	}

	return true;
}
