#include "filezilla.h"

#include <fzssh/pubkey.hpp>

CNotification::CNotification(NotificationId id)
	: id_(id)
{
}

CDirectoryListingNotification::CDirectoryListingNotification(CServerPath const& path, bool const primary, bool const failed)
	: primary_(primary), m_failed(failed), m_path(path)
{
}

CAsyncRequestNotification::CAsyncRequestNotification(RequestId id)
	: req_id_(id)
{
}

bool CAsyncRequestNotification::IsPending() const
{
	return requestNumber_.lock().operator bool();
}

CInteractiveLoginNotification::CInteractiveLoginNotification(CServer const& server, ServerHandle const& handle)
	: CAsyncRequestNotification(reqId_interactiveLogin)
	, server_(server)
	, handle_(handle)
{
}

PasswordRequest::PasswordRequest(CServer const& server, ServerHandle const& handle, bool canRemember)
	: CAsyncRequestNotification(reqId_password)
	, server_(server)
	, handle_(handle)
	, canRemember_(canRemember)
{
}

OtpRequest::OtpRequest(CServer const& server, ServerHandle const& handle)
	: CAsyncRequestNotification(reqId_otp)
	, server_(server)
	, handle_(handle)
{
}

KeyfilePasswordRequest::KeyfilePasswordRequest(CServer const& server, ServerHandle const& handle)
	: CAsyncRequestNotification(reqId_keyfile_password)
	, server_(server)
	, handle_(handle)
{
}

CTransferStatusNotification::CTransferStatusNotification(CTransferStatus const& status)
	: status_(status)
{
}

CTransferStatus const& CTransferStatusNotification::GetStatus() const
{
	return status_;
}

CHostKeyNotification::CHostKeyNotification(CServer const& server, ServerHandle const& handle, std::unique_ptr<fz::ssh::public_key> && hostkey, fz::ssh::algorithm_info && algorithms)
	: CAsyncRequestNotification(reqId_hostkey)
	, server_(server)
	, handle_(handle)
	, hostkey_(std::move(hostkey))
	, algorithms_(std::move(algorithms))
{}

CHostKeyNotification::~CHostKeyNotification()
{
}

CCertificateNotification::CCertificateNotification(fz::tls_session_info&& info)
	: CAsyncRequestNotification(reqId_certificate)
	, info_(info)
{
}

CInsecureConnectionNotification::CInsecureConnectionNotification(CServer const& server)
	: CAsyncRequestNotification(reqId_insecure_connection)
	, server_(server)
{
}
