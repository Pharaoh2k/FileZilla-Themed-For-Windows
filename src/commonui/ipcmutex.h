#ifndef FILEZILLA_COMMONUI_IPCMUTEX_HEADER
#define FILEZILLA_COMMONUI_IPCMUTEX_HEADER

#include <string>
#include <vector>

#include "../include/libfilezilla_engine.h" // FIXME Add a generic platform.h

#include "visibility.h"

/*
 * CInterProcessMutex represents an interprocess mutex. The mutex will be
 * created and locked in the constructor and released in the destructor.
 * Under Windows we use the normal Windows mutexes, under all other platforms
 * we use lockfiles using fcntl advisory locks.
 */

enum t_ipcMutexType
{
	// Important: Never ever change a value.
	// Otherwise this will cause interesting effects between different
	// versions of FileZilla
	MUTEX_OPTIONS = 1,
	MUTEX_SITEMANAGER = 2,
	MUTEX_SITEMANAGERGLOBAL = 3,
	MUTEX_QUEUE = 4,
	MUTEX_FILTERS = 5,
	MUTEX_LAYOUT = 6,
	MUTEX_MOSTRECENTSERVERS = 7,
	MUTEX_TRUSTEDCERTS = 8,
	MUTEX_GLOBALBOOKMARKS = 9,
	MUTEX_SEARCHCONDITIONS = 10,
	MUTEX_MAC_SANDBOX_USERDIRS = 11, // Only used if configured with --enable-mac-sandbox
	MUTEX_TOKENSTORE = 12,
	MUTEX_URLHANDLER = 13,
	MUTEX_TRUSTEDHOSTKEYS = 14,
};

// this sets the path where the lock file is located in non-windows systems
void FZCUI_PUBLIC_SYMBOL set_ipcmutex_lockfile_path(std::wstring const& path);

class FZCUI_PUBLIC_SYMBOL CInterProcessMutex final
{
public:
	CInterProcessMutex(t_ipcMutexType mutexType, bool initialLock = true);
	~CInterProcessMutex();

	bool Lock();
	int TryLock(); // Tries to lock the mutex. Returns 1 on success, 0 if held by other process, -1 on failure
	void Unlock();

	bool IsLocked() const { return m_locked; }

	t_ipcMutexType GetType() const { return m_type; }

private:

#ifdef FZ_WINDOWS
	// Under windows use normal mutexes
	HANDLE hMutex;
#else
	// Use a lockfile under all other systems
	static int m_fd;
	static int m_instanceCount;
#endif
	t_ipcMutexType m_type;

	bool m_locked;
};

class FZCUI_PUBLIC_SYMBOL CReentrantInterProcessMutexLocker final
{
public:
	CReentrantInterProcessMutexLocker(t_ipcMutexType mutexType);
	~CReentrantInterProcessMutexLocker();

protected:
	struct t_data final
	{
		CInterProcessMutex* pMutex;
		unsigned int lockCount;
	};
	static std::vector<t_data> m_mutexes;

	t_ipcMutexType m_type;
};

#endif
