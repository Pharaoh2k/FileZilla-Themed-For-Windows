#include "controlsocket.h"
#include "directorycache.h"
#include "engineprivate.h"
#include "filezilla.h"

CFileZillaEngine::CFileZillaEngine(CFileZillaEngineContext& engine_context, std::function<void(CFileZillaEngine*)> const& cb)
	: impl_(std::make_unique<CFileZillaEnginePrivate>(engine_context, *this, cb))
{
}

CFileZillaEngine::~CFileZillaEngine()
{
	if (impl_) {
		impl_->shutdown();
		impl_.reset();
	}
}

int CFileZillaEngine::Execute(const CCommand &command)
{
	return impl_->Execute(command);
}

std::unique_ptr<CNotification> CFileZillaEngine::GetNextNotification()
{
	return impl_->GetNextNotification();
}

void CFileZillaEngine::SetAsyncRequestReply(std::unique_ptr<CAsyncRequestNotification> && pNotification)
{
	impl_->SetAsyncRequestReply(std::move(pNotification));
}

CTransferStatus CFileZillaEngine::GetTransferStatus(bool &changed)
{
	return impl_->GetTransferStatus(changed);
}

int CFileZillaEngine::CacheLookup(const CServerPath& path, CDirectoryListing& listing)
{
	return impl_->CacheLookup(path, listing);
}

int CFileZillaEngine::Cancel()
{
	return impl_->Cancel();
}

bool CFileZillaEngine::IsBusy() const
{
	return impl_->IsBusy();
}

bool CFileZillaEngine::IsConnected() const
{
	return impl_->IsConnected();
}
