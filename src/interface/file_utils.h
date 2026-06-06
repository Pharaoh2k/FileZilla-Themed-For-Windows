#ifndef FILEZILLA_FILE_UTILS_HEADER
#define FILEZILLA_FILE_UTILS_HEADER

#include "../commonui/file_utils.h"
#include <libfilezilla/recursive_remove.hpp>


// Returns the association for a file based on its extension
std::vector<std::wstring> GetSystemAssociation(std::wstring const& file);

std::vector<fz::native_string> AssociationToCommand(std::vector<std::wstring> const& association, std::wstring_view const& file);

bool ProgramExists(std::wstring const& editor);

// Opens specified directory in local file manager, e.g. Explorer on Windows
bool OpenInFileManager(std::wstring const& dir);

bool RenameFile(wxWindow* pWnd, wxString dir, wxString from, wxString to);

#if FZ_WINDOWS
class wxModalEventLoop;
#endif

class gui_recursive_remove final : public fz::recursive_remove
{
public:
	gui_recursive_remove(wxWindow* parent)
		: parent_(parent)
	{}

#ifdef FZ_WINDOWS
	virtual void adjust_shfileop(SHFILEOPSTRUCT & op) override;
	virtual bool call_shfileop(SHFILEOPSTRUCT & op) override;
#endif

	virtual bool confirm() const;

private:
	wxWindow* parent_;
};

#ifdef FZ_WINDOWS
int CallSHFileOperation(wxWindow* parent, SHFILEOPSTRUCT & op);
#endif

#endif
