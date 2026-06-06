#ifndef FILEZILLA_INTERFACE_DNDOBJECTS_HEADER
#define FILEZILLA_INTERFACE_DNDOBJECTS_HEADER

#ifdef __WXMSW__
#define FZ3_USESHELLEXT 1
#else
#define FZ3_USESHELLEXT 0
#endif

#include "drop_target_ex.h"
#include "dropsource.h"
#include "xmlfunctions.h"

#include <wx/dnd.h>

#include <memory>

wxDataFormat LocalDataObjectFormat();
wxDataFormat RemoteDataObjectFormat();

class CLocalDataObject final : public wxDataObjectSimple
{
public:
	CLocalDataObject();

	virtual size_t GetDataSize() const;
	virtual bool GetDataHere(void *buf) const;

	virtual bool SetData(size_t len, const void* buf);

	std::vector<std::string> const& GetFiles() const { return files_; }
	std::vector<std::wstring> GetFilesW() const;

	void Reserve(size_t count);
	void AddFile(std::wstring const& file);

protected:
	std::vector<std::string> files_;
};

class login_manager;
class COptionsBase;

class CRemoteDataObject final : public wxDataObjectSimple
{
public:
	CRemoteDataObject(login_manager & lim, COptionsBase & options, Site const& site, const CServerPath& path);
	CRemoteDataObject(login_manager & lim, COptionsBase & options);

	virtual size_t GetDataSize() const;
	virtual bool GetDataHere(void *buf ) const;

	virtual bool SetData(size_t len, const void* buf);

	// Finalize has to be called prior to calling wxDropSource::DoDragDrop
	void Finalize();

	bool DidSendData() const { return m_didSendData; }

	Site const& GetSite() const { return site_; }
	const CServerPath& GetServerPath() const { return m_path; }
	int GetProcessId() const { return m_processId; }

	struct t_fileInfo
	{
		std::wstring name;
		int64_t size;
		bool dir;
		bool link;
	};

	const std::vector<t_fileInfo>& GetFiles() const { return m_fileList; }

	void Reserve(size_t count);
	void AddFile(std::wstring const& name, bool dir, int64_t size, bool link);

protected:
	login_manager & lim_;
	COptionsBase & options_;

	Site site_;
	CServerPath m_path;

	mutable CXmlFile m_xmlFile;

	bool m_didSendData{};

	int m_processId;

	std::vector<t_fileInfo> m_fileList;

	mutable size_t m_expectedSize{};
};

#if FZ3_USESHELLEXT

// This class checks if the shell extension is installed and
// communicates with it.
class CShellExtensionInterface final
{
public:
	CShellExtensionInterface();
	~CShellExtensionInterface();

	bool IsLoaded() const { return m_shellExtension != 0; }

	wxString InitDrag();

	wxString GetTarget();

	wxString GetDragDirectory() const { return m_dragDirectory; }

	static std::unique_ptr<CShellExtensionInterface> CreateInitialized();

protected:
	bool CreateDragDirectory();

	void* m_shellExtension;
	HANDLE m_hMutex;
	HANDLE m_hMapping;

	std::wstring m_dragDirectory;
};

#endif

class FileDropTargetBase : public wxDropTarget
{
public:
	FileDropTargetBase(COptionsBase& options, login_manager& lim);

	CLocalDataObject* GetLocalDataObject();
	CRemoteDataObject* GetRemoteDataObject();
	wxDataFormat GetReceivedFormat();

protected:
	wxFileDataObject *const m_pFileDataObject{};

private:
	CLocalDataObject *const m_pLocalDataObject{};
	CRemoteDataObject *const m_pRemoteDataObject{};
	wxDataObjectComposite *const m_pDataObject{};
};

template<typename Control>
class CFileDropTarget : public CScrollableDropTarget<Control, FileDropTargetBase, COptionsBase&, login_manager&>
{
public:
	CFileDropTarget(Control* ctrl, COptionsBase& options, login_manager& lim);
};

#endif
