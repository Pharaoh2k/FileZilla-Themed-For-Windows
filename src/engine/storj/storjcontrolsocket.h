#ifndef FILEZILLA_ENGINE_STORJCONTROLSOCKET_HEADER
#define FILEZILLA_ENGINE_STORJCONTROLSOCKET_HEADER

#include "../controlsocket.h"

namespace fz {
class process;
}

class CStorjInputThread;

struct storj_message;
class CStorjControlSocket final : public CControlSocket
{
public:
	CStorjControlSocket(CFileZillaEnginePrivate & engine);
	virtual ~CStorjControlSocket();

	virtual void Connect(CServer const& server, Credentials const& credentials) override;

	virtual void List(CServerPath const& path = CServerPath(), std::wstring const& subDir = std::wstring(), int flags = 0) override;
	virtual void FileTransfer(CFileTransferCommand const& cmd) override;
	virtual void Delete(CServerPath const& path, std::vector<std::wstring>&& files) override;
	virtual void Mkdir(const CServerPath& path, transfer_flags const& flags = {}) override;
	virtual void RemoveDir(CServerPath const& path = CServerPath(), std::wstring const& subDir = std::wstring()) override;
	/*virtual void Rename(const CRenameCommand& command) override;*/
	virtual void Cancel() override;

	virtual bool SetAsyncRequestReply(CAsyncRequestNotification *pNotification) override;

protected:
	virtual void Push(std::unique_ptr<COpData> && pNewOpData) override;

	// Replaces filename"with"quotes with
	// "filename""with""quotes"
	std::wstring QuoteFilename(std::wstring const& filename);

	virtual int DoClose(int nErrorCode = FZ_REPLY_DISCONNECTED) override;

	virtual int ResetOperation(int nErrorCode) override;

	void ProcessReply(int result, std::wstring const& reply);

	int SendCommand(std::wstring const& cmd, std::wstring const& show = std::wstring());
	int AddToStream(std::wstring const& cmd);
	int AddToStream(std::string_view cmd);

	std::unique_ptr<fz::process> process_;
	std::unique_ptr<CStorjInputThread> input_thread_;

	virtual void operator()(fz::event_base const& ev) override;
	void OnStorjEvent(storj_message const& message);
	void OnTerminate(std::wstring const& error);

	int result_{};
	std::wstring response_;

	friend class CProtocolOpData<CStorjControlSocket>;
	friend class CStorjConnectOpData;
	friend class CStorjDeleteOpData;
	friend class CStorjFileTransferOpData;
	friend class CStorjListOpData;
	friend class CStorjMkdirOpData;
	friend class CStorjRemoveDirOpData;
};

typedef CProtocolOpData<CStorjControlSocket> CStorjOpData;

#endif
