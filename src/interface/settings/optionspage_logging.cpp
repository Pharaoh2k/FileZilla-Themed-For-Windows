#include "../filezilla.h"
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_logging.h"
#include "../textctrlex.h"

#include <wx/filedlg.h>
#include <wx/statbox.h>

struct COptionsPageLogging::impl final
{
	wxCheckBox* timestamps_{};
	wxCheckBox* log_{};
	wxTextCtrlEx* file_{};
	wxButton* browse_{};
	wxCheckBox* do_limit_{};
	wxTextCtrlEx* limit_{};
};

COptionsPageLogging::COptionsPageLogging()
	: impl_(std::make_unique<impl>())
{}

COptionsPageLogging::~COptionsPageLogging()
{
}

bool COptionsPageLogging::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	main->AddGrowableRow(0);
	SetSizer(main);

	{
		auto [box, inner] = lay.createStatBox(main, _("Logging"), 1);

		impl_->timestamps_ = new wxCheckBox(box, nullID, _("&Show timestamps in message log"));
		inner->Add(impl_->timestamps_);

		impl_->log_ = new wxCheckBox(box, nullID, _("&Log to file"));
		inner->Add(impl_->log_);
		auto row = lay.createFlex(3);
		row->AddGrowableCol(1);
		inner->Add(row, 0, wxLEFT|wxGROW, lay.indent);
		row->Add(new wxStaticText(box, nullID, _("Filename:")), lay.valign);
		impl_->file_ = new wxTextCtrlEx(box, nullID, wxString());
		row->Add(impl_->file_, lay.valigng);
		impl_->browse_ = new wxButton(box, nullID, _("&Browse"));
		row->Add(impl_->browse_, lay.valign);
		impl_->browse_->Bind(wxEVT_BUTTON, &COptionsPageLogging::OnBrowse, this);

		impl_->do_limit_ = new wxCheckBox(box, nullID, _("Limit size of logfile"));
		inner->Add(impl_->do_limit_, 0, wxLEFT, lay.indent);
		row = lay.createFlex(3);
		inner->Add(row, 0, wxLEFT, lay.indent * 2);
		row->Add(new wxStaticText(box, nullID, _("Limit:")), lay.valign);
		impl_->limit_ = new wxTextCtrlEx(box, nullID, wxString());
		row->Add(impl_->limit_, lay.valign)->SetMinSize(lay.dlgUnits(20), -1);
		impl_->limit_->SetMaxLength(4);
		row->Add(new wxStaticText(box, nullID, _("MiB")), lay.valign);

		inner->Add(new wxStaticText(box, nullID, _("If the size of the logfile reaches the limit, it gets renamed by adding \".1\" to the end of the filename (possibly overwriting older logfiles) and a new file gets created.")), 0, wxLEFT, lay.indent * 2);

		impl_->do_limit_->Bind(wxEVT_CHECKBOX, &COptionsPageLogging::OnCheck, this);
		impl_->log_->Bind(wxEVT_CHECKBOX, &COptionsPageLogging::OnCheck, this);
	}

	return true;
}
bool COptionsPageLogging::LoadPage()
{
	impl_->timestamps_->SetValue(m_pOptions->get_bool(OPTION_MESSAGELOG_TIMESTAMP));

	std::wstring const filename = m_pOptions->get_string(OPTION_LOGGING_FILE);
	impl_->log_->SetValue(!filename.empty());
	impl_->file_->ChangeValue(filename);

	int const limit = m_pOptions->get_int(OPTION_LOGGING_FILE_SIZELIMIT);
	impl_->do_limit_->SetValue(limit > 0);
	impl_->limit_->ChangeValue(fz::to_wstring(limit));

	SetCtrlState();

	return true;
}

bool COptionsPageLogging::SavePage()
{
	m_pOptions->set(OPTION_MESSAGELOG_TIMESTAMP, impl_->timestamps_->GetValue());

	std::wstring file;
	if (impl_->log_->GetValue()) {
		file = impl_->file_->GetValue().ToStdWstring();
	}
	m_pOptions->set(OPTION_LOGGING_FILE, file);

	if (impl_->do_limit_->GetValue()) {
		m_pOptions->set(OPTION_LOGGING_FILE_SIZELIMIT, fz::to_integral<int>(impl_->limit_->GetValue().ToStdWstring()));
	}
	else {
		m_pOptions->set(OPTION_LOGGING_FILE_SIZELIMIT, 0);
	}

	return true;
}

bool COptionsPageLogging::Validate()
{
	if (impl_->log_->GetValue()) {
		wxString file = impl_->file_->GetValue();
		if (file.empty()) {
			return DisplayError(impl_->file_, _("You need to enter a name for the log file."));
		}

		wxFileName fn(file);
		if (!fn.IsOk() || !fn.DirExists()) {
			return DisplayError(impl_->file_, _("Directory containing the logfile does not exist or filename is invalid."));
		}

		if (impl_->do_limit_->GetValue()) {
			auto v = fz::to_integral<int>(impl_->limit_->GetValue().ToStdWstring());
			if (v < 1 || v > 2000) {
				return DisplayError(impl_->limit_, _("The limit needs to be between 1 and 2000 MiB"));
			}
		}
	}
	return true;
}

void COptionsPageLogging::SetCtrlState()
{
	bool const log_to_file = impl_->log_->GetValue();
	bool const limit = impl_->do_limit_->GetValue();

	impl_->file_->Enable(log_to_file);
	impl_->browse_->Enable(log_to_file);
	impl_->do_limit_->Enable(log_to_file);
	impl_->limit_->Enable(log_to_file && limit);
}

void COptionsPageLogging::OnBrowse(wxCommandEvent&)
{
	CLocalPath p;
	std::wstring f;
	if (!p.SetPath(impl_->file_->GetValue().ToStdWstring(), &f) || f.empty() || p.empty() || !p.Exists()) {
		p.clear();
		f = L"filezilla.log";
	}
	wxFileDialog dlg(this, _("Log file"), p.GetPath(), f, L"Log files (*.log)|*.log", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	impl_->file_->ChangeValue(dlg.GetPath());
}

void COptionsPageLogging::OnCheck(wxCommandEvent&)
{
	SetCtrlState();
}
