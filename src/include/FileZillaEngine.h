#ifndef FILEZILLA_ENGINE_ENGINE_HEADER
#define FILEZILLA_ENGINE_ENGINE_HEADER

#include "commands.h"
#include "notification.h"

#include <functional>

class CAsyncRequestNotification;
class CFileZillaEngineContext;
class CFileZillaEnginePrivate;
class CNotification;

class FZC_PUBLIC_SYMBOL CFileZillaEngine final
{
public:
	// notification_cb is called from unspecified engine-internal thread. Do
	// not call into engine rom it. Use it just to signal the thread that
	// owns the engine.
	CFileZillaEngine(CFileZillaEngineContext& engine_context, std::function<void(CFileZillaEngine*)> const& notification_cb);
	~CFileZillaEngine();

	CFileZillaEngine(CFileZillaEngine const&) = delete;
	CFileZillaEngine& operator=(CFileZillaEngine const&) = delete;

	// Execute the given command. See commands.h for a list of the available
	// commands and reply codes.
	int Execute(CCommand const& command);

	// Cancels the current command
	int Cancel();

	bool IsBusy() const;
	bool IsConnected() const;

	// Returns the next pending notification.
	// It is mandatory to call this function until it returns a nullptr each time you
	// get the pending notifications event, or you'll either lose notifications
	// or your memory will fill with pending notifications.
	// See notification.h for details.
	std::unique_ptr<CNotification> GetNextNotification();

	// Sets the reply to the asynchronous request. Takes ownership of the pointer.
	void SetAsyncRequestReply(std::unique_ptr<CAsyncRequestNotification> && pNotification);

	// Get a progress update about the current transfer. changed will be set
	// to true if the data has been updated compared to the last time
	// GetTransferStatus was called.
	CTransferStatus GetTransferStatus(bool &changed);

	int CacheLookup(CServerPath const& path, CDirectoryListing& listing);

private:
	std::unique_ptr<CFileZillaEnginePrivate> impl_;
};

#endif
