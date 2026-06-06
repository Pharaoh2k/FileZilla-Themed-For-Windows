#include "filezilla.h"

CConnectCommand::CConnectCommand(CServer const& server, ServerHandle const& handle, Credentials const& credentials, bool retry_connecting)
	: server_(server)
	, handle_(handle)
	, credentials_(credentials)
	, retry_connecting_(retry_connecting)
{
}

bool CConnectCommand::valid() const
{
	if (!server_) {
		return false;
	}
	return true;
}

CListCommand::CListCommand(int flags)
	: m_flags(flags)
{
}

CListCommand::CListCommand(CServerPath path, std::wstring const& subDir, int flags)
	: m_path(path), m_subDir(subDir), m_flags(flags)
{
}

CServerPath CListCommand::GetPath() const
{
	return m_path;
}

std::wstring CListCommand::GetSubDir() const
{
	return m_subDir;
}

bool CListCommand::valid() const
{
	if (GetPath().empty() && !GetSubDir().empty()) {
		return false;
	}

	if (GetFlags() & LIST_FLAG_LINK && GetSubDir().empty()) {
		return false;
	}

	bool const refresh = (m_flags & LIST_FLAG_REFRESH) != 0;
	bool const avoid = (m_flags & LIST_FLAG_AVOID) != 0;
	if (refresh && avoid) {
		return false;
	}

	return true;
}

CFileTransferCommand::CFileTransferCommand(fz::reader_factory_holder const& reader,
	CServerPath const& remotePath, std::wstring const& remoteFile, transfer_flags const& flags, std::wstring const& extraFlags, std::string const& persistentState)
	: reader_(reader), m_remotePath(remotePath), m_remoteFile(remoteFile), extraFlags_(extraFlags), persistentState_(persistentState), flags_(flags)
{
}

CFileTransferCommand::CFileTransferCommand(fz::writer_factory_holder const& writer,
	CServerPath const& remotePath, std::wstring const& remoteFile, transfer_flags const& flags, std::wstring const& extraFlags, std::string const& persistentState)
	: writer_(writer), m_remotePath(remotePath), m_remoteFile(remoteFile), extraFlags_(extraFlags), persistentState_(persistentState), flags_(flags)
{
}

CServerPath CFileTransferCommand::GetRemotePath() const
{
	return m_remotePath;
}

std::wstring CFileTransferCommand::GetRemoteFile() const
{
	return m_remoteFile;
}

bool CFileTransferCommand::valid() const
{
	if (!reader_ && !writer_) {
		return false;
	}

	if (m_remotePath.empty() || m_remoteFile.empty()) {
		return false;
	}

	return true;
}

CRawCommand::CRawCommand(std::wstring const& command)
{
	m_command = command;
}

std::wstring CRawCommand::GetCommand() const
{
	return m_command;
}

CDeleteCommand::CDeleteCommand(const CServerPath& path, std::vector<std::wstring>&& files)
	: m_path(path), files_(files)
{
}

bool CDeleteCommand::valid() const
{
	if (GetPath().empty() || GetFiles().empty()) {
		return false;
	}
	for (auto const& f : files_) {
		if (f.empty()) {
			return false;
		}
	}
	return true;
}

CRemoveDirCommand::CRemoveDirCommand(const CServerPath& path, std::wstring const& subDir)
	: m_path(path), m_subDir(subDir)
{
}

bool CRemoveDirCommand::valid() const
{
	return !GetPath().empty() && !GetSubDir().empty();
}

CMkdirCommand::CMkdirCommand(const CServerPath& path, transfer_flags const& flags)
	: m_path(path)
	, flags_(flags)
{
}

bool CMkdirCommand::valid() const
{
	return !GetPath().empty() && GetPath().HasParent();
}

CRenameCommand::CRenameCommand(CServerPath const& fromPath, std::wstring const& fromFile,
							   CServerPath const& toPath, std::wstring const& toFile)
	: m_fromPath(fromPath)
	, m_toPath(toPath)
	, m_fromFile(fromFile)
	, m_toFile(toFile)
{}

bool CRenameCommand::valid() const
{
	return !GetFromPath().empty() && !GetToPath().empty() && !GetFromFile().empty() && !GetToFile().empty();
}

CChmodCommand::CChmodCommand(CServerPath const& path, std::wstring const& file, std::wstring const& permission)
	: m_path(path)
	, m_file(file)
	, m_permission(permission)
{}

bool CChmodCommand::valid() const
{
	return !GetPath().empty() && !GetFile().empty() && !GetPermission().empty();
}
