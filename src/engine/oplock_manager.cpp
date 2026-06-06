#include "filezilla.h"

#include "controlsocket.h"
#include "oplock_manager.h"

#include <assert.h>

OpLock::OpLock(OpLockManager * mgr, size_t socket, size_t lock)
	: mgr_(mgr)
	, socket_(socket)
	, lock_(lock)
{
}

OpLock::~OpLock()
{
	if (mgr_) {
		mgr_->Unlock(*this);
		mgr_ = nullptr;
	}
}

OpLock::OpLock(OpLock && op) noexcept
{
	if (mgr_) {
		mgr_->Unlock(*this);
	}
	mgr_ = op.mgr_;
	socket_ = op.socket_;
	lock_ = op.lock_;

	op.mgr_ = nullptr;
}

OpLock& OpLock::operator=(OpLock && op) noexcept
{
	if (this != &op) {
		if (mgr_) {
			mgr_->Unlock(*this);
		}
		mgr_ = op.mgr_;
		socket_ = op.socket_;
		lock_ = op.lock_;

		op.mgr_ = nullptr;
	}

	return *this;
}

bool OpLock::waiting() const
{
	if (mgr_) {
		return mgr_->Waiting(*this);
	}

	return false;
}

OpLock OpLockManager::Lock(CControlSocket * socket, locking_reason reason, CServerPath const& path, bool inclusive)
{
	fz::scoped_lock l(mtx_);

	size_t socket_index = get_or_create(socket);
	auto & sli = socket_locks_[socket_index];

	lock_info info;
	info.reason = reason;
	info.inclusive = inclusive;
	info.path = path;

	for (auto const& other_sli : socket_locks_) {
		if (other_sli.control_socket_ == socket || other_sli.server_ != sli.server_) {
			continue;
		}

		for (auto const& lock : other_sli.locks_) {
			if (reason != lock.reason || lock.waiting || lock.released) {
				continue;
			}
			if (lock.path == path || (lock.inclusive && lock.path.IsParentOf(path, false))) {
				info.waiting = true;
				break;
			}
			if (inclusive && path.IsParentOf(lock.path, false)) {
				info.waiting = true;
				break;
			}
		}
		if (info.waiting) {
			break;
		}
	}

	sli.locks_.push_back(info);

	return OpLock(this, socket_index, sli.locks_.size() - 1);
}

void OpLockManager::Unlock(OpLock & lock)
{
	fz::scoped_lock l(mtx_);
	assert(lock.socket_ < socket_locks_.size());
	assert(lock.lock_ < socket_locks_[lock.socket_].locks_.size());

	bool was_waiting = false;

	auto & sli = socket_locks_[lock.socket_];

	was_waiting = sli.locks_[lock.lock_].waiting;

	if (lock.lock_ + 1 == sli.locks_.size()) {
		sli.locks_.pop_back();
		while (!sli.locks_.empty() && sli.locks_.back().released) {
			sli.locks_.pop_back();
		}
		if (sli.locks_.empty()) {
			if (lock.socket_ + 1 == socket_locks_.size()) {
				socket_locks_.pop_back();
				while (!socket_locks_.empty() && !socket_locks_.back().control_socket_) {
					socket_locks_.pop_back();
				}
			}
			else {
				socket_locks_[lock.socket_].control_socket_ = nullptr;
			}
		}
	}
	else {
		sli.locks_[lock.lock_].waiting = false;
		sli.locks_[lock.lock_].released = true;
	}

	lock.mgr_ = nullptr;

	if (!was_waiting) {
		Wakeup();
	}
}

void OpLockManager::Wakeup()
{
	for (auto & sli : socket_locks_) {
		for (auto & lock : sli.locks_) {
			if (lock.waiting) {
				sli.control_socket_->send_event<CObtainLockEvent>();
				break;
			}
		}
	}
}

bool OpLockManager::ObtainWaiting(CControlSocket * socket)
{
	bool obtained = false;

	fz::scoped_lock l(mtx_);
	for (auto & sli : socket_locks_) {
		if (sli.control_socket_ == socket) {
			for (auto & lock : sli.locks_) {
				if (lock.waiting) {
					obtained |= ObtainWaiting(sli, lock);
				}
			}
		}
	}

	return obtained;
}

bool OpLockManager::ObtainWaiting(socket_lock_info const& sli, lock_info& lock)
{
	for (auto const& other_sli : socket_locks_) {
		if (&other_sli == &sli) {
			continue;
		}

		for (auto const& other_lock : other_sli.locks_) {
			if (lock.reason != other_lock.reason || other_lock.waiting || other_lock.released) {
				continue;
			}
			if (other_lock.path == lock.path || (other_lock.inclusive && other_lock.path.IsParentOf(lock.path, false))) {
				return false;
			}
			if (lock.inclusive && lock.path.IsParentOf(other_lock.path, false)) {
				return false;
			}
		}
	}

	lock.waiting = false;
	return true;
}

size_t OpLockManager::get_or_create(CControlSocket * socket)
{
	for (size_t i = 0; i < socket_locks_.size(); ++i) {
		if (socket_locks_[i].control_socket_ == socket) {
			return i;
		}
	}

	socket_lock_info info;
	info.control_socket_ = socket;
	info.server_ = socket->GetCurrentServer();
	socket_locks_.push_back(info);

	return socket_locks_.size() - 1;
}

bool OpLockManager::Waiting(OpLock const& lock) const
{
	fz::scoped_lock l(mtx_);
	assert(lock.socket_ < socket_locks_.size());
	assert(lock.lock_ < socket_locks_[lock.socket_].locks_.size());

	auto & sli = socket_locks_[lock.socket_];
	return sli.locks_[lock.lock_].waiting;
}

bool OpLockManager::Waiting(CControlSocket * socket) const
{
	fz::scoped_lock l(mtx_);
	for (auto const& sli : socket_locks_) {
		if (sli.control_socket_ == socket) {
			for (auto const& lock : sli.locks_) {
				if (lock.waiting) {
					return true;
				}
			}
		}
	}

	return false;
}
