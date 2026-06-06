#ifndef FILEZILLA_ENGINE_OPLOCK_MANAGER_HEADER
#define FILEZILLA_ENGINE_OPLOCK_MANAGER_HEADER

#include "../include/serverpath.h"

#include <libfilezilla/event.hpp>
#include <libfilezilla/mutex.hpp>

#include <memory>
#include <vector>

struct obtain_lock_event_type;
typedef fz::simple_event<obtain_lock_event_type> CObtainLockEvent;

enum class locking_reason
{
	unknown = -1,
	list,
	mkdir,

	private1
};

class CControlSocket;

class OpLockManager;
class OpLock final
{
public:
	OpLock() = default;
	~OpLock();

	OpLock(OpLock const&) = delete;
	OpLock& operator=(OpLock const&) = delete;

	OpLock(OpLock && op) noexcept;
	OpLock& operator=(OpLock && op) noexcept;

	explicit operator bool() const {
		return mgr_ != nullptr;
	}

	bool waiting() const;

private:
	friend class OpLockManager;

	OpLock(OpLockManager * mgr, size_t socket, size_t lock);

	OpLockManager * mgr_{};
	size_t socket_{};
	size_t lock_{};
};


class OpLockManager final
{
public:
	OpLock Lock(CControlSocket * socket, locking_reason reason, CServerPath const& path, bool inclusive = false);

	bool Waiting(CControlSocket * socket) const;

	bool ObtainWaiting(CControlSocket * socket);

private:
	friend class OpLock;

	struct lock_info
	{
		CServerPath path;
		locking_reason reason{};
		bool inclusive{};
		bool waiting{};
		bool released{};
	};

	struct socket_lock_info
	{
		CServer server_;
		CControlSocket * control_socket_;

		std::vector<lock_info> locks_;
	};

	void Unlock(OpLock & lock);

	void Wakeup();
	bool ObtainWaiting(socket_lock_info const& sli, lock_info& lock);

	bool Waiting(OpLock const& lock) const;

	size_t get_or_create(CControlSocket * socket);

	std::vector<socket_lock_info> socket_locks_;

	mutable fz::mutex mtx_{false};
};

#endif
