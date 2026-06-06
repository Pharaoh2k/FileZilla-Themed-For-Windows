#ifndef FILEZILLA_ENGINE_STORJ_FILETRANSFER_HEADER
#define FILEZILLA_ENGINE_STORJ_FILETRANSFER_HEADER

#include "storjcontrolsocket.h"

class CStorjFileTransferOpData final : public CFileTransferOpData, public CStorjOpData, public fz::event_handler
{
public:
	CStorjFileTransferOpData(CStorjControlSocket & controlSocket, CFileTransferCommand const& cmd)
		: CFileTransferOpData(L"CStorjFileTransferOpData", cmd)
		, CStorjOpData(controlSocket)
		, fz::event_handler(controlSocket, fz::child_event_handler)
	{
	}

	~CStorjFileTransferOpData();

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int prevResult, COpData const& previousOperation) override;

	void OnNextBufferRequested(uint64_t processed);
	void OnFinalizeRequested(uint64_t lastWrite);

private:
	virtual void operator()(fz::event_base const& ev) override;
	void OnBufferAvailability(fz::aio_waitable const* w);

	std::unique_ptr<fz::reader_base> reader_;
	std::unique_ptr<fz::writer_base> writer_;
	bool finalizing_{};

	uint8_t const* base_address_{};
	fz::buffer_lease buffer_;
};

#endif
