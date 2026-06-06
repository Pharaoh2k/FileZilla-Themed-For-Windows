#include "filezilla.h"
#include "dndobjects.h"
#include "dragdropmanager.h"

#include "listctrlex.h"
#include "treectrlex.h"

wxDataFormat LocalDataObjectFormat()
{
	static wxDataFormat const fmt = wxDataFormat(L"FileZilla3LocalDataObject");
	return fmt;
}

wxDataFormat RemoteDataObjectFormat()
{
	static wxDataFormat const fmt = wxDataFormat(L"FileZilla3RemoteDataObject");
	return fmt;
}

CLocalDataObject::CLocalDataObject()
	: wxDataObjectSimple(LocalDataObjectFormat())
{
}

size_t CLocalDataObject::GetDataSize() const
{
	size_t ret = 1;
	for (auto const& file : files_) {
		ret += file.size() + 1;
	}

	return ret;
}

bool CLocalDataObject::GetDataHere(void *buf) const
{
	auto p = static_cast<char*>(buf);
	*p = 1;
	++p;

	for (auto const& file : files_) {
		strcpy(p, file.c_str());
		p += file.size() + 1;
	}

	return true;
}

bool CLocalDataObject::SetData(size_t len, const void* buf)
{
	files_.clear();

	auto p = static_cast<char const*>(buf);
	auto const end = p + len;

	if (!len || !buf || p[len - 1] != 0) {
		return false;
	}

	if (p[0] != 1) {
		return false;
	}
	++p;

	while (p < end) {
		size_t filelen = strlen(p);

		if (filelen) {
			files_.push_back(std::string(p, p + filelen));
		}

		p += filelen + 1;
	}

	return true;
}

void CLocalDataObject::Reserve(size_t count)
{
	files_.reserve(count);
}

void CLocalDataObject::AddFile(std::wstring const& file)
{
	std::string utf8 = fz::to_utf8(file);
	if (!utf8.empty()) {
		files_.push_back(utf8);
	}
}

std::vector<std::wstring> CLocalDataObject::GetFilesW() const
{
	std::vector<std::wstring> ret;
	ret.reserve(files_.size());

	for (auto const& file : files_) {
		std::wstring wide = fz::to_wstring_from_utf8(file);
		if (!wide.empty()) {
			ret.emplace_back(std::move(wide));
		}
	}

	return ret;
}

#if FZ3_USESHELLEXT

#include <initguid.h>
#include "../fzshellext/shellext.h"
#include <shlobj.h>
#include <wx/stdpaths.h>

CShellExtensionInterface::CShellExtensionInterface()
{
	m_shellExtension = 0;

	int res = CoCreateInstance(CLSID_ShellExtension, NULL,
	  CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, IID_IUnknown,
	  &m_shellExtension);
	if (res != S_OK) {
		m_shellExtension = 0;
	}

	if (m_shellExtension) {
		m_hMutex = CreateMutex(0, false, _T("FileZilla3DragDropExtLogMutex"));
	}
	else {
		m_hMutex = 0;
	}

	m_hMapping = 0;
}

CShellExtensionInterface::~CShellExtensionInterface()
{
	if (m_shellExtension) {
		((IUnknown*)m_shellExtension)->Release();
		CoFreeUnusedLibraries();
	}

	if (m_hMapping) {
		CloseHandle(m_hMapping);
	}

	if (m_hMutex) {
		CloseHandle(m_hMutex);
	}

	if (!m_dragDirectory.empty()) {
		RemoveDirectory(m_dragDirectory.c_str());
	}
}

wxString CShellExtensionInterface::InitDrag()
{
	if (!m_shellExtension) {
		return wxString();
	}

	if (!m_hMutex) {
		return wxString();
	}

	if (!CreateDragDirectory()) {
		return wxString();
	}

	m_hMapping = CreateFileMapping(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0, DRAG_EXT_MAPPING_LENGTH, DRAG_EXT_MAPPING);
	if (!m_hMapping) {
		return wxString();
	}

	char* data = (char*)MapViewOfFile(m_hMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, DRAG_EXT_MAPPING_LENGTH);
	if (!data) {
		CloseHandle(m_hMapping);
		m_hMapping = 0;
		return wxString();
	}

	DWORD result = WaitForSingleObject(m_hMutex, 250);
	if (result != WAIT_OBJECT_0) {
		UnmapViewOfFile(data);
		return wxString();
	}

	*data = DRAG_EXT_VERSION;
	data[1] = 1;
	wcscpy((wchar_t*)(data + 2), m_dragDirectory.c_str());

	ReleaseMutex(m_hMutex);

	UnmapViewOfFile(data);

	return m_dragDirectory;
}

wxString CShellExtensionInterface::GetTarget()
{
	if (!m_shellExtension) {
		return wxString();
	}

	if (!m_hMutex) {
		return wxString();
	}

	if (!m_hMapping) {
		return wxString();
	}

	char* data = (char*)MapViewOfFile(m_hMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, DRAG_EXT_MAPPING_LENGTH);
	if (!data) {
		CloseHandle(m_hMapping);
		m_hMapping = 0;
		return wxString();
	}

	DWORD result = WaitForSingleObject(m_hMutex, 250);
	if (result != WAIT_OBJECT_0) {
		UnmapViewOfFile(data);
		return wxString();
	}

	wxString target;
	const char reply = data[1];
	if (reply == 2) {
		data[DRAG_EXT_MAPPING_LENGTH - 1] = 0;
		data[DRAG_EXT_MAPPING_LENGTH - 2] = 0;
		target = (wchar_t*)(data + 2);
	}

	ReleaseMutex(m_hMutex);

	UnmapViewOfFile(data);

	if (target.empty()) {
		return target;
	}

	if (target.Last() == '\\') {
		target.RemoveLast();
	}
	int pos = target.Find('\\', true);
	if (pos < 1) {
		return wxString();
	}
	target = target.Left(pos + 1);

	return target;
}

bool CShellExtensionInterface::CreateDragDirectory()
{
	for (int i = 0; i < 10; ++i) {
		auto const now = fz::datetime::now();
		int64_t value = now.get_time_t();
		value *= 1000;
		value += now.get_milliseconds();
		value *= 10;
		value += i;

		wxFileName dirname(wxStandardPaths::Get().GetTempDir(), L"");
		dirname.Normalize();
		dirname.AppendDir(DRAG_EXT_DUMMY_DIR_PREFIX + std::to_wstring(value));

		std::wstring dir = dirname.GetFullPath().ToStdWstring();
		if (dir.size() > 2 && (dir.back() == '/' || dir.back() == '\\')) {
			dir.pop_back();
		}
		if (dir.size() > DRAG_EXT_MAX_PATH) {
			return false;
		}

		if (CreateDirectory(dir.c_str(), 0)) {
			m_dragDirectory = dir;
			return true;
		}
	}

	return true;
}

std::unique_ptr<CShellExtensionInterface> CShellExtensionInterface::CreateInitialized()
{
	auto ret = std::make_unique<CShellExtensionInterface>();
	if (!ret->IsLoaded() || ret->InitDrag().empty()) {
		ret.reset();
	}

	return ret;
}

#endif

CRemoteDataObject::CRemoteDataObject(login_manager & lim, COptionsBase & options, Site const& site, const CServerPath& path)
	: wxDataObjectSimple(wxDataFormat(_T("FileZilla3RemoteDataObject")))
	, lim_(lim)
	, options_(options)
	, site_(site)
	, m_path(path)
	, m_processId(wxGetProcessId())
{
}

CRemoteDataObject::CRemoteDataObject(login_manager & lim, COptionsBase & options)
	: wxDataObjectSimple(wxDataFormat(_T("FileZilla3RemoteDataObject")))
	, lim_(lim)
	, options_(options)
	, m_processId(wxGetProcessId())
{
}

size_t CRemoteDataObject::GetDataSize() const
{
	wxASSERT(!m_path.empty());

	wxCHECK(m_xmlFile.GetElement(), 0);

	m_expectedSize = m_xmlFile.GetRawDataLength() + 1;

	return m_expectedSize;
}

bool CRemoteDataObject::GetDataHere(void *buf) const
{
	wxASSERT(!m_path.empty());

	wxCHECK(m_xmlFile.GetElement(), false);

	m_xmlFile.GetRawDataHere((char*)buf, m_expectedSize);
	if (m_expectedSize > 0) {
		static_cast<char*>(buf)[m_expectedSize - 1] = 0;
	}

	const_cast<CRemoteDataObject*>(this)->m_didSendData = true;
	return true;
}

void CRemoteDataObject::Finalize()
{
	// Convert data into XML
	auto element = m_xmlFile.CreateEmpty();
	element = element.append_child("RemoteDataObject");

	AddTextElement(element, "ProcessId", m_processId);

	AddTextElement(element, "Count", m_fileList.size());

	auto xServer = element.append_child("Server");
	SetServer(xServer, site_, lim_, options_);

	AddTextElement(element, "Path", m_path.GetSafePath());

	auto files = element.append_child("Files");
	for (auto const& info : m_fileList) {
		auto file = files.append_child("File");
		AddTextElement(file, "Name", info.name);
		AddTextElement(file, "Dir", info.dir ? 1 : 0);
		AddTextElement(file, "Size", info.size);
		AddTextElement(file, "Link", info.link ? 1 : 0);
	}
}

bool CRemoteDataObject::SetData(size_t len, const void* buf)
{
	char* data = (char*)buf;
	if (!len) {
		return false;
	}

	if (!m_xmlFile.ParseData(reinterpret_cast<uint8_t const*>(data), len)) {
		return false;
	}

	auto element = m_xmlFile.GetElement();
	if (!element || !(element = element.child("RemoteDataObject"))) {
		return false;
	}

	m_processId = GetTextElementInt(element, "ProcessId", -1);
	if (m_processId == -1) {
		return false;
	}

	int64_t count = GetTextElementInt(element, "Count", -1);
	if (count > 0) {
		try {
			m_fileList.reserve(static_cast<size_t>(count));
		}
		catch(std::exception const&) {
			return false;
		}
	}

	auto serverElement = element.child("Server");
	if (!serverElement || !::GetServer(serverElement, site_)) {
		return false;
	}

	std::wstring path = GetTextElement(element, "Path");
	if (path.empty() || !m_path.SetSafePath(path)) {
		return false;
	}

	m_fileList.clear();
	auto files = element.child("Files");
	if (!files) {
		return false;
	}

	for (auto file = files.child("File"); file; file = file.next_sibling("File")) {
		t_fileInfo info;
		info.name = GetTextElement(file, "Name");
		if (info.name.empty()) {
			return false;
		}

		const int dir = GetTextElementInt(file, "Dir", -1);
		if (dir == -1) {
			return false;
		}
		info.dir = dir == 1;

		info.size = GetTextElementInt(file, "Size", -2);
		if (info.size <= -2) {
			return false;
		}

		info.link = GetTextElementBool(file, "Link", false);

		m_fileList.push_back(info);
	}

	return true;
}

void CRemoteDataObject::Reserve(size_t count)
{
	m_fileList.reserve(count);
}

void CRemoteDataObject::AddFile(std::wstring const& name, bool dir, int64_t size, bool link)
{
	t_fileInfo info;
	info.name = name;
	info.dir = dir;
	info.size = size;
	info.link = link;

	m_fileList.push_back(info);
}

FileDropTargetBase::FileDropTargetBase(COptionsBase& options, login_manager& lim)
	: m_pFileDataObject(new wxFileDataObject())
	, m_pLocalDataObject(new CLocalDataObject())
	, m_pRemoteDataObject(new CRemoteDataObject(lim, options))
	, m_pDataObject(new wxDataObjectComposite)
{
	m_pDataObject->Add(m_pRemoteDataObject, true);
	m_pDataObject->Add(m_pLocalDataObject, false);
	m_pDataObject->Add(m_pFileDataObject, false);
	SetDataObject(m_pDataObject);
}

wxDataFormat FileDropTargetBase::GetReceivedFormat()
{
	auto format = m_pDataObject->GetReceivedFormat();
#ifdef __WXMAC__
	auto dndmgr = CDragDropManager::Get();
	if (format == wxDF_FILENAME && dndmgr && dndmgr->dragDataObject) {
		format = dndmgr->dragDataObject->GetPreferredFormat();
	}
#endif
        return format;
}

CLocalDataObject* FileDropTargetBase::GetLocalDataObject()
{
#ifdef __WXMAC__
	auto dndmgr = CDragDropManager::Get();
	if (dndmgr && dndmgr->dragDataObject && dndmgr->dragDataObject->GetPreferredFormat() == wxDataFormat(L"FileZilla3LocalDataObject")) {
		return static_cast<CLocalDataObject *>(dndmgr->dragDataObject);
	}
#endif
	return m_pLocalDataObject;
}

CRemoteDataObject* FileDropTargetBase::GetRemoteDataObject()
{
#ifdef __WXMAC__
	auto dndmgr = CDragDropManager::Get();
	if (dndmgr && dndmgr->dragDataObject && dndmgr->dragDataObject->GetPreferredFormat() == wxDataFormat(L"FileZilla3RemoteDataObject")) {
		return static_cast<CRemoteDataObject *>(dndmgr->dragDataObject);
	}
#endif
	return m_pRemoteDataObject;
}

template<class Control>
CFileDropTarget<Control>::CFileDropTarget(Control* ctrl, COptionsBase& options, login_manager& lim)
	: CScrollableDropTarget<Control, FileDropTargetBase, COptionsBase&, login_manager&>(ctrl, options, lim)
{
}

template class CFileDropTarget<wxTreeCtrlEx>;
template class CFileDropTarget<wxListCtrlEx>;
