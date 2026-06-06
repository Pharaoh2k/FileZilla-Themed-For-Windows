#ifndef FILEZILLA_INTERFACE_EDITHANDLER_HEADER
#define FILEZILLA_INTERFACE_EDITHANDLER_HEADER

#include "dialogex.h"

#include "../commonui/site.h"

#include <wx/timer.h>

#include <list>
#include <map>

// Handles all aspects about remote file viewing/editing

typedef std::map<std::wstring, std::vector<std::wstring>, std::less<>> Associations;

Associations LoadAssociations();
void SaveAssociations(Associations const& assocs);


namespace edit_choices {
enum type
{
	edit_existing_action = 0x1,
	edit_existing_always = 0x2
};
}

class COptionsBase;
class CQueueView;
class CEditHandler final : protected wxEvtHandler
{
public:
	CEditHandler(COptionsBase & options);

	// This tries to deletes all temporary files.
	// If files are locked, they won't be removed though
	~CEditHandler();

	enum fileState
	{
		unknown = -1,
		edit,
		download,
		upload,
		upload_and_remove,
		upload_and_remove_failed,
		removing
	};

	enum fileType : signed char
	{
		none = -1,
		local,
		remote
	};

	std::wstring GetLocalDirectory();

	fileState GetFileState(std::wstring const& fileName) const; // Local files
	fileState GetFileState(std::wstring const& fileName, CServerPath const& remotePath, Site const& site) const; // Remote files

	// Returns the number of files in given state
	// pServer may be set only if state isn't unknown
	int GetFileCount(fileType type, fileState state, Site const& site = Site()) const;

	// Starts editing the given file, queues it if needed. For local files, fileName must include local path.
	// Can be used to edit files already being added, user is prompted for action.
	bool Edit(CEditHandler::fileType type, std::wstring const& fileName, CServerPath const& path, Site const& site, int64_t size, wxWindow* parent);

	class FileData final {
	public:
		FileData() = default;
		FileData(std::wstring const& n, int64_t s)
			: name(n), size(s) {}

		std::wstring name;
		int64_t size{};
	};
	bool Edit(CEditHandler::fileType type, std::vector<FileData> const& data, CServerPath const& path, Site const& site, wxWindow* parent);

	// Adds the file that doesn't exist yet. (Has to be in unknown state)
	// The initial state will be download for remote files.
	bool AddFile(CEditHandler::fileType type, std::wstring const& localFile, std::wstring const& remoteFile, CServerPath const& remotePath, Site const& site, int64_t size);

	// Tries to unedit and remove file
	bool Remove(std::wstring const& fileName); // Local files
	bool Remove(std::wstring const& fileName, CServerPath const& remotePath, Site const& site); // Remote files
	bool RemoveAll(bool force);
	bool RemoveAll(fileState state, Site const& site = Site());

	void FinishTransfer(bool successful, std::wstring const& fileName);
	void FinishTransfer(bool successful, std::wstring const& fileName, CServerPath const& remotePath, Site const& site);

	void CheckForModifications();

	bool LaunchEditor(std::wstring const& file);
	bool LaunchEditor(std::wstring const& file, CServerPath const& remotePath, Site const& site);

	struct t_fileData
	{
		std::wstring remoteFile; // The name of the remote file
		std::wstring localFile; // The full path to the local file
		fileState state;
		fz::datetime modificationTime;
		CServerPath remotePath;
		Site site;
	};

	const std::list<t_fileData>& GetFiles(fileType type) const { wxASSERT(type != none); return m_fileDataList[(type == local) ? 0 : 1]; }

	bool UploadFile(std::wstring const& file, bool unedit);
	bool UploadFile(std::wstring const& file, CServerPath const& remotePath, Site const& site, bool unedit);

	// Returns command to open the file.
	std::vector<std::wstring> GetAssociation(std::wstring const& file);

	void SetQueue(CQueueView* pQueue) { m_pQueue = pQueue; }

protected:
	void DoCheckForModifications();

	/* Checks if file can be opened. One of these conditions has to be true:
	 * - Filetype association of system has to exist
	 * - Custom association for that filetype
	 * - Default editor set
	 */
	std::vector<std::wstring> CanOpen(std::wstring const& fileName, bool& program_exists);

	bool DoEdit(CEditHandler::fileType type, FileData const& file, CServerPath const& path, Site const& site, wxWindow* parent, size_t fileCount, int & already_editing_action);

	COptionsBase & options_;

	std::wstring m_localDir;

	bool LaunchEditor(fileType type, t_fileData &data);

	std::vector<std::wstring> GetCustomAssociation(std::wstring_view const& file);

	void SetTimerState();

	bool UploadFile(fileType type, std::list<t_fileData>::iterator iter, bool unedit);

	std::list<t_fileData> m_fileDataList[2];

	std::list<t_fileData>::iterator GetFile(std::wstring const& fileName);
	std::list<t_fileData>::const_iterator GetFile(std::wstring const& fileName) const;
	std::list<t_fileData>::iterator GetFile(std::wstring const& fileName, CServerPath const& remotePath, Site const& site);
	std::list<t_fileData>::const_iterator GetFile(std::wstring const& fileName, CServerPath const& remotePath, Site const& site) const;

	CQueueView* m_pQueue{};

	wxTimer m_timer;
	wxTimer m_busyTimer;

	void RemoveTemporaryFiles(std::wstring const& temp);
	void RemoveTemporaryFilesInSpecificDir(std::wstring const& temp);

	std::wstring GetTemporaryFile(std::wstring name);
	std::wstring TruncateFilename(std::wstring const& path, std::wstring const& name, size_t max);
	bool FilenameExists(std::wstring const& file);

	int DisplayChangeNotification(fileType type, t_fileData const& data, bool& remove);

#ifdef __WXMSW__
	HANDLE m_lockfile_handle;
#else
	int m_lockfile_descriptor;
#endif
};

class CWindowStateManager;
class CEditHandlerStatusDialog final : protected wxDialogEx
{
public:
	CEditHandlerStatusDialog(wxWindow* parent, COptionsBase& options, CEditHandler* edit_handler);
	virtual ~CEditHandlerStatusDialog();

	virtual int ShowModal();

protected:
	void SetCtrlState();

	CEditHandler::t_fileData* GetDataFromItem(int item, CEditHandler::fileType &type);

	struct impl;
	std::unique_ptr<impl> impl_;

	void OnUnedit();
	void OnUpload(bool uneditAfter);
	void OnEdit();
};

#endif
