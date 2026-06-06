#include "filezilla.h"
#include "aboutdialog.h"
#include "buildinfo.h"
#include "Options.h"
#include "themeprovider.h"

#include "../include/version.h"

#include <wx/hyperlink.h>
#include <wx/clipbrd.h>
#include <wx/statline.h>
#include <wx/statbmp.h>
#include <wx/statbox.h>

#include <libfilezilla/version.hpp>

#include <fzssh/version.hpp>

CAboutDialog::CAboutDialog(COptionsBase& options)
	: options_(options)
{
}

bool CAboutDialog::Create(wxWindow* parent)
{
	wxDialogEx::Create(parent, nullID, _("About FileZilla"));

	auto & lay = layout();
	auto main = lay.createMain(this, 1);

	auto top = lay.createFlex(2);
	main->Add(top);

	top->Add(CThemeProvider::Get()->createStaticBitmap(this, L"ART_FILEZILLA", iconSizeLarge), 0, wxALL, lay.border);

	auto topRight = lay.createFlex(1);
	top->Add(topRight);

	std::wstring version = L"FileZilla " + GetFileZillaVersion();
	if (CBuildInfo::GetBuildType() == L"nightly") {
		version += L"-nightly";
	}
	topRight->Add(new wxStaticText(this, nullID, version));
	topRight->Add(new wxStaticText(this, nullID, L"Copyright (C) 2004-2026  Tim Kosse"));

	auto homepage = lay.createFlex(2);
	homepage->Add(new wxStaticText(this, nullID, _("Homepage:")), lay.valign);
	homepage->Add(new wxHyperlinkCtrl(this, nullID, L"https://filezilla-project.org/", L"https://filezilla-project.org/"), lay.valign);
	topRight->Add(homepage);

	{
		auto [box, inner] = lay.createStatBox(main, _("Build information"), 2);

		std::wstring host = CBuildInfo::GetHostname();
		if (!host.empty()) {
			inner->Add(new wxStaticText(box, nullID, _("Compiled for:")));
			inner->Add(new wxStaticText(box, nullID, host));
		}

		std::wstring build = CBuildInfo::GetBuildSystem();
		if (!build.empty()) {
			inner->Add(new wxStaticText(box, nullID, _("Compiled on:")));
			inner->Add(new wxStaticText(box, nullID, build));
		}
		inner->Add(new wxStaticText(box, nullID, _("Build date:")));
		inner->Add(new wxStaticText(box, nullID, CBuildInfo::GetBuildDateString()));

		inner->Add(new wxStaticText(box, nullID, _("Compiled with:")));
		wxString cc = CBuildInfo::GetCompiler();
		WrapText(this, cc, 300);
		inner->Add(new wxStaticText(box, nullID, cc));

		wxString compilerFlags = CBuildInfo::GetCompilerFlags();
		if (!compilerFlags.empty()) {
			inner->Add(new wxStaticText(box, nullID, _("Compiler flags:")));
			WrapText(this, compilerFlags, 300);
			inner->Add(new wxStaticText(box, nullID, compilerFlags));
		}
	}

	{
		auto [box, inner] = lay.createStatBox(main, _("Linked against"), 1);

		auto deps = lay.createFlex(2);
		inner->Add(deps);
		deps->Add(new wxStaticText(box, nullID, _("libfilezilla:")));
		deps->Add(new wxStaticText(box, nullID, fz::to_wstring(fz::get_version_string())));
		deps->Add(new wxStaticText(box, nullID, _("fzssh:")));
		deps->Add(new wxStaticText(box, nullID, fz::to_wstring(fz::ssh::get_version_string())));
		deps->Add(new wxStaticText(box, nullID, _("wxWidgets:")));
		deps->Add(new wxStaticText(box, nullID, GetDependencyVersion(gui_lib_dependency::wxwidgets)));
		deps->Add(new wxStaticText(box, nullID, _("GnuTLS:")));
		deps->Add(new wxStaticText(box, nullID, GetDependencyVersion(lib_dependency::gnutls)));
		deps->Add(new wxStaticText(box, nullID, _("SQLite:")));
		deps->Add(new wxStaticText(box, nullID, GetDependencyVersion(gui_lib_dependency::sqlite)));

		auto legal = new wxHyperlinkCtrl(box, nullID, _("License and copyright information"), {});
		inner->Add(legal, lay.valign);

		legal->Bind(wxEVT_HYPERLINK, [this](wxHyperlinkEvent const&) {
			wxMessageBoxEx(
				L"FileZilla makes use of the following third-party libraries:\n\n"
				"libfilezilla - Copyright (C) 2015-2026 Tim Kosse - GPLv2 or later\n\n"
				"fzssh - Copyright (C) 2025-2026 Tim Kosse and Business Follows srl. - AGPLv3 or later\n\n"
				"wxWidgets - Copyright (C) wxWidgets - wxWindows Library Licence version 3.1 or later\n\n"
				"GnuTLS - Copyright (C) 2000-2023 Free Software Foundation - LGPLv2.1 or later\n\n"
				"Nettle - Copyright (C) 2001-2025 Niels Möller, and others - GPLv2 or later, LGPLv3 or later\n\n"
				"GMP - Copyright 1991, 1996, 1999, 2000, 2007 Free Software Foundation, Inc. - GPLv2 or later, LGPLv3 or later\n\n"
				"pugixml - Copyright (C) 2006-2026 Arseny Kapoulkine - MIT\n\n",
				L"Third-party library copyright and license information");
		});

	}

	{
		auto [box, inner] = lay.createStatBox(main, _("System details"), 2);
		auto os = wxGetOsDescription();
		if (!os.empty()) {
			inner->Add(new wxStaticText(box, nullID, _("Operating System:")));
			inner->Add(new wxStaticText(box, nullID, os));
		}

		auto sysver = GetSystemVersion();
		if (sysver) {
			inner->Add(new wxStaticText(box, nullID, _("OS version:")));
			wxString osVersion = wxString::Format(_T("%u.%u"), sysver.major, sysver.minor);
			inner->Add(new wxStaticText(box, nullID, osVersion));
		}
#ifdef __WXMSW__
		inner->Add(new wxStaticText(box, nullID, _("Platform:")));
		if (::wxIsPlatform64Bit()) {
			inner->Add(new wxStaticText(box, nullID, _("64-bit system")));
		}
		else {
			inner->Add(new wxStaticText(box, nullID, _("32-bit system")));
		}
#endif

		wxString cpuCaps = CBuildInfo::GetCPUCaps(' ');
		if (!cpuCaps.empty()) {
			inner->Add(new wxStaticText(box, nullID, _("CPU features:")));
			WrapText(this, cpuCaps, 300);
			inner->Add(new wxStaticText(box, nullID, cpuCaps));
		}

		inner->Add(new wxStaticText(box, nullID, _("Settings directory:")));
		inner->Add(new wxStaticText(box, nullID, options_.get_string(OPTION_DEFAULT_SETTINGSDIR)));
	}

	main->Add(new wxStaticLine(this), lay.grow);
	auto buttons = lay.createFlex(3);
	main->Add(buttons, lay.grow);

	auto copy = new wxButton(this, nullID, _("&Copy to clipboard"));
	copy->Bind(wxEVT_BUTTON, [this](wxEvent const&){ OnCopy(); });
	buttons->Add(copy, lay.valign);
	buttons->AddStretchSpacer();
	buttons->AddGrowableCol(1);

	auto ok = new wxButton(this, nullID, _("OK"));
	ok->Bind(wxEVT_BUTTON, [this](wxEvent const&){ EndModal(wxID_OK); });
	ok->SetDefault();
	ok->SetFocus();
	buttons->Add(ok, lay.valign);

	GetSizer()->Fit(this);
	GetSizer()->SetSizeHints(this);

	return true;
}

void CAboutDialog::OnCopy()
{
	wxString text = _T("FileZilla Client\n");
	text += _T("----------------\n\n");

	text += _T("Version:          ") + GetFileZillaVersion();
	if (CBuildInfo::GetBuildType() == _T("nightly")) {
		text += _T("-nightly");
	}
	text += '\n';

	text += _T("\nBuild information:\n");

	wxString host = CBuildInfo::GetHostname();
	if (!host.empty()) {
		text += _T("  Compiled for:   ") + host + _T("\n");
	}

	wxString build = CBuildInfo::GetBuildSystem();
	if (!build.empty()) {
		text += _T("  Compiled on:    ") + build + _T("\n");
	}

	text += _T("  Build date:     ") + CBuildInfo::GetBuildDateString() + _T("\n");

	text += _T("  Compiled with:  ") + CBuildInfo::GetCompiler() + _T("\n");

	wxString compilerFlags = CBuildInfo::GetCompilerFlags();
	if (!compilerFlags.empty()) {
		text += _T("  Compiler flags: ") + compilerFlags + _T("\n");
	}

	text += _T("\nLinked against:\n");
	for (int i = 0; i < static_cast<int>(gui_lib_dependency::count); ++i) {
		text += wxString::Format(_T("  % -15s %s\n"),
			GetDependencyName(gui_lib_dependency(i)) + _T(":"),
			GetDependencyVersion(gui_lib_dependency(i)));
	}
	for (int i = 0; i < static_cast<int>(lib_dependency::count); ++i) {
		text += wxString::Format(_T("  % -15s %s\n"),
			GetDependencyName(lib_dependency(i)) + _T(":"),
			GetDependencyVersion(lib_dependency(i)));
	}

	text += _T("\nOperating system:\n");
	wxString os = wxGetOsDescription();
	if (!os.empty()) {
		text += _T("  Name:           ") + os + _T("\n");
	}

	auto sysver = GetSystemVersion();
	if (sysver) {
		text += wxString::Format(_T("  Version:        %u.%u\n"), sysver.major, sysver.minor);
	}

#ifdef __WXMSW__
	if (::wxIsPlatform64Bit()) {
		text += _T("  Platform:       64-bit system\n");
	}
	else {
		text += _T("  Platform:       32-bit system\n");
	}
#endif

	wxString cpuCaps = CBuildInfo::GetCPUCaps(' ');
	if (!cpuCaps.empty()) {
		text += _T("  CPU features:   ") + cpuCaps + _T("\n");
	}

	text += _T("  Settings dir:   ") + options_.get_string(OPTION_DEFAULT_SETTINGSDIR) + _T("\n");

#ifdef __WXMSW__
	text.Replace(_T("\n"), _T("\r\n"));
#endif

	if (!wxTheClipboard->Open()) {
		wxMessageBoxEx(_("Could not open clipboard"), _("Could not copy data"), wxICON_EXCLAMATION);
		return;
	}

	wxTheClipboard->SetData(new wxTextDataObject(text));
	wxTheClipboard->Flush();
	wxTheClipboard->Close();
}
