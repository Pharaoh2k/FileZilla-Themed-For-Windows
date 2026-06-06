#include "filezilla.h"

#include "file_utils.h"

#include <wx/stdpaths.h>
#ifdef FZ_WINDOWS
#include <knownfolders.h>
#include <shlobj.h>
#include <shlwapi.h>
#else
#include <wx/mimetype.h>
#include <wx/textfile.h>
#endif

#include <wx/evtloop.h>

#include "Options.h"

#include <libfilezilla/local_filesys.hpp>

bool OpenInFileManager(std::wstring const& dir)
{
	bool ret = false;
#ifdef __WXMSW__
	// Unfortunately under Windows, UTF-8 encoded file:// URLs don't work, so use native paths.
	// Unfortunatelier, we cannot use this for UNC paths, have to use file:// here
	// Unfortunateliest, we again have a problem with UTF-8 characters which we cannot fix...
	if (dir.substr(0, 2) != L"\\\\" && dir != L"/") {
		ret = wxLaunchDefaultBrowser(dir);
	}
	else
#endif
	{
		std::wstring url = GetAsURL(dir);
		if (!url.empty()) {
			ret = wxLaunchDefaultBrowser(url);
		}
	}


	if (!ret) {
		wxBell();
	}

	return ret;
}

#ifndef FZ_WINDOWS
namespace {
bool PathExpand(std::wstring& cmd)
{
	if (cmd.empty()) {
		return false;
	}
#ifndef __WXMSW__
	if (!cmd.empty() && cmd[0] == '/') {
		return true;
	}
#else
	if (!cmd.empty() && cmd[0] == '\\') {
		// UNC or root of current working dir, whatever that is
		return true;
	}
	if (cmd.size() > 2 && cmd[1] == ':') {
		// Absolute path
		return true;
	}
#endif

	// Need to search for program in $PATH
	std::wstring path = GetEnv("PATH");
	if (path.empty()) {
		return false;
	}

	wxString full_cmd;
	bool found = wxFindFileInPath(&full_cmd, path, cmd);
#ifdef __WXMSW__
	if (!found && !fz::equal_insensitive_ascii(cmd.substr(cmd.size() - 1), L".exe")) {
		cmd += L".exe";
		found = wxFindFileInPath(&full_cmd, path, cmd);
	}
#endif

	if (!found) {
		return false;
	}

	cmd = full_cmd;
	return true;
}
}
#endif

std::vector<std::wstring> GetSystemAssociation(std::wstring const& file)
{
	std::vector<std::wstring> ret;

	auto const ext = GetExtension(file);
	if (ext.empty() || ext == L".") {
		return ret;
	}

#if FZ_WINDOWS
	auto query = [&](wchar_t const* verb) {
		DWORD len{};
		int res = AssocQueryString(0, ASSOCSTR_COMMAND, (L"." + ext).c_str(), verb, nullptr, &len);
		if (res == S_FALSE && len > 1) {
			std::wstring raw;

			// len as returned by AssocQueryString includes terminating null
			raw.resize(static_cast<size_t>(len - 1));

			res = AssocQueryString(0, ASSOCSTR_COMMAND, (L"." + ext).c_str(), verb, raw.data(), &len);
			if (res == S_OK && len > 1) {
				raw.resize(len - 1);
				return UnquoteCommand(raw);
			}
		}

		return std::vector<std::wstring>();
	};

	ret = query(L"edit");
	if (ret.empty()) {
		ret = query(nullptr);
	}

	std::vector<std::wstring> raw;
	std::swap(ret, raw);
	if (!raw.empty()) {
		ret.emplace_back(std::move(raw.front()));
	}

	// Process placeholders.

	// Phase 1: Look for %1
	bool got_first{};
	for (size_t i = 1; i < raw.size(); ++i) {
		auto const& arg = raw[i];

		std::wstring out;
		out.reserve(arg.size());
		bool percent{};
		for (auto const& c : arg) {
			if (percent) {
				if (!got_first) {
					if (c == '1') {
						out += L"%f";
						got_first = true;
					}
					else if (c == '*') {
						out += '%';
						out += c;
					}
					// Ignore others.
				}

				percent = false;
			}
			else if (c == '%') {
				percent = true;
			}
			else {
				out += c;
			}
		}

		if (!out.empty() || arg.empty()) {
			ret.emplace_back(std::move(out));
		}
	}

	std::swap(ret, raw);
	ret.clear();
	if (!raw.empty()) {
		ret.emplace_back(std::move(raw.front()));
	}

	// Phase 2: Filter %*
	for (size_t i = 1; i < raw.size(); ++i) {
		auto& arg = raw[i];

		std::wstring out;
		out.reserve(arg.size());
		bool percent{};
		for (auto const& c : arg) {
			if (percent) {
				if (c == '*') {
					if (!got_first) {
						out += L"%f";
					}
				}
				else if (c == 'f') {
					out += L"%f";
				}
				// Ignore others.

				percent = false;
			}
			else if (c == '%') {
				percent = true;
			}
			else {
				out += c;
			}
		}

		if (!out.empty() || arg.empty()) {
			ret.emplace_back(std::move(out));
		}
	}
#else
	// wxWidgets doens't escape properly.
	// For now don't support these files until we can replace wx with something sane.
	if (ext.find_first_of(L"\"\\") != std::wstring::npos) {
		return ret;
	}

	std::unique_ptr<wxFileType> pType(wxTheMimeTypesManager->GetFileTypeFromExtension(ext));
	if (!pType) {
		return ret;
	}

	std::wstring const placeholder = L"placeholderJkpZ0eet6lWsI8glm3uSJYZyvn7WZn5S";
	ret = UnquoteCommand(pType->GetOpenCommand(placeholder).ToStdWstring());

	bool replaced{};
	for (size_t i = 0; i < ret.size(); ++i) {
		auto& arg = ret[i];

		fz::replace_substrings(arg, L"%", L"%%");
		if (fz::replace_substrings(arg, placeholder, L"%f")) {
			if (!i) {
				// Placeholder found in the command itself? Something went wrong.
				ret.clear();
				return ret;
			}
			replaced = true;
		}
	}

	if (!replaced) {
		ret.clear();
	}

	if (!ret.empty()) {
		if (!PathExpand(ret[0])) {
			ret.clear();
		}
	}

#endif

	if (!ret.empty() && !ProgramExists(ret[0])) {
		ret.clear();
	}

	return ret;
}

std::vector<fz::native_string> AssociationToCommand(std::vector<std::wstring> const& association, std::wstring_view const& file)
{
	std::vector<fz::native_string> ret;
	ret.reserve(association.size());

	if (!association.empty()) {
		ret.push_back(fz::to_native(association.front()));
	}

	bool replaced{};

	for (size_t i = 1; i < association.size(); ++i) {
		bool percent{};
		std::wstring const& arg = association[i];

		std::wstring out;
		out.reserve(arg.size());
		for (auto const& c : arg) {
			if (percent) {
				percent = false;
				if (c == 'f') {
					replaced = true;
					out += file;
				}
				else {
					out += c;
				}
			}
			else if (c == '%') {
				percent = true;
			}
			else {
				out += c;
			}
		}
		ret.emplace_back(fz::to_native(out));
	}

	if (!replaced) {
		ret.emplace_back(fz::to_native(file));
	}

	return ret;
}

bool ProgramExists(std::wstring const& editor)
{
	if (wxFileName::FileExists(editor)) {
		return true;
	}

#ifdef __WXMAC__
	std::wstring_view e = editor;
	if (!e.empty() && e.back() == '/') {
		e = e.substr(0, e.size() - 1);
	}
	if (fz::ends_with(e, std::wstring_view(L".app")) && wxFileName::DirExists(editor)) {
		return true;
	}
#endif

	return false;
}

#ifdef FZ_WINDOWS
int CallSHFileOperation(wxWindow* parent, SHFILEOPSTRUCT & op)
{
	int res{};

	wxWindow * focused = wxWindow::FindFocus();

	{
		// SHFileOperation shows non-modal dialogs. Force them to be modal by using a new modal event loop
		wxWindow dummy(parent, -1, parent ? parent->GetPosition() : wxDefaultPosition, parent ? parent->GetSize() : wxDefaultSize, wxTRANSPARENT_WINDOW);
		wxModalEventLoop loop(&dummy);
		dummy.CallAfter([&]() {
			res = SHFileOperation(&op);
			loop.Exit();
		});
		loop.Run();
	}

	if (focused) {
		// Microsoft introduced a bug in Windows 10 1803: Calling SHFileOperation resets focus.
		focused->SetFocus();
	}
	return res;
}

#endif

#ifdef FZ_WINDOWS
void gui_recursive_remove::adjust_shfileop(SHFILEOPSTRUCT & op)
{
	op.hwnd = parent_ ? (HWND)parent_->GetHandle() : 0;
	if (parent_) {
		// Move to trash if shift is not pressed, else delete
		op.fFlags = wxGetKeyState(WXK_SHIFT) ? 0 : FOF_ALLOWUNDO;
	}
	else {
		recursive_remove::adjust_shfileop(op);
	}
}

bool gui_recursive_remove::call_shfileop(SHFILEOPSTRUCT & op)
{
	return CallSHFileOperation(parent_, op) == 0;
}
#endif

bool gui_recursive_remove::confirm() const
{
	if (parent_) {
		if (wxMessageBoxEx(_("Really delete all selected files and/or directories from your computer?"), _("Confirmation needed"), wxICON_QUESTION | wxYES_NO, parent_) != wxYES) {
			 return false;
		}
	}
	return true;
}

bool RenameFile(wxWindow* parent, wxString dir, wxString from, wxString to)
{
	if (dir.Right(1) != _T("\\") && dir.Right(1) != _T("/")) {
		dir += wxFileName::GetPathSeparator();
	}

#ifdef __WXMSW__
	to = to.Left(255);

	if ((to.Find('/') != -1) ||
		(to.Find('\\') != -1) ||
		(to.Find(':') != -1) ||
		(to.Find('*') != -1) ||
		(to.Find('?') != -1) ||
		(to.Find('"') != -1) ||
		(to.Find('<') != -1) ||
		(to.Find('>') != -1) ||
		(to.Find('|') != -1))
	{
		wxMessageBoxEx(_("Filenames may not contain any of the following characters: / \\ : * ? \" < > |"), _("Invalid filename"), wxICON_EXCLAMATION, parent);
		return false;
	}

	auto t = fz::local_filesys::get_file_type((dir + to).ToStdWstring());
	if (t != fz::local_filesys::unknown) {
		wxMessageBoxEx(_("Target filename already exists!"), _("Invalid filename"), wxICON_EXCLAMATION, parent);
		return false;
	}

	SHFILEOPSTRUCT op;
	memset(&op, 0, sizeof(op));

	from = dir + from + _T(" ");
	from.SetChar(from.Length() - 1, '\0');
	op.pFrom = from.wc_str();
	to = dir + to + _T(" ");
	to.SetChar(to.Length()-1, '\0');
	op.pTo = to.wc_str();
	op.hwnd = parent ? (HWND)parent->GetHandle() : nullptr;
	op.wFunc = FO_RENAME;
	op.fFlags = FOF_ALLOWUNDO;

	return CallSHFileOperation(parent, op) == 0;
#else
	if ((to.Find('/') != -1) ||
		(to.Find('*') != -1) ||
		(to.Find('?') != -1) ||
		(to.Find('<') != -1) ||
		(to.Find('>') != -1) ||
		(to.Find('|') != -1))
	{
		wxMessageBoxEx(_("Filenames may not contain any of the following characters: / * ? < > |"), _("Invalid filename"), wxICON_EXCLAMATION, parent);
		return false;
	}

	return wxRename(dir + from, dir + to) == 0;
#endif
}
