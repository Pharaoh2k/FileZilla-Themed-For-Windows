#ifndef FILEZILLA_ENGINE_LOOKUP_HEADER
#define FILEZILLA_ENGINE_LOOKUP_HEADER

#include "controlsocket.h"
#include "directorycache.h"

class LookupOpData final : public COpData, public CProtocolOpData<CControlSocket>
{
public:
	LookupOpData(CControlSocket &controlSocket, CServerPath const& path, std::wstring const& file, CDirentry * entry);

	virtual int Send() override;
	virtual int ParseResponse() override { return FZ_REPLY_INTERNALERROR; }
	virtual int SubcommandResult(int prevResult, COpData const& previousOperation) override;

	CDirentry const& entry() const { return *entry_; }

private:
	CServerPath const path_;
	std::wstring const file_;

	CDirentry * entry_;
	std::unique_ptr<CDirentry> internal_entry_;
};

class LookupManyOpData final : public COpData, public CProtocolOpData<CControlSocket>
{
public:
	LookupManyOpData(CControlSocket &controlSocket, CServerPath const& path, std::vector<std::wstring> const& files)
		: COpData(Command::lookup, L"LookupManyOpData")
		, CProtocolOpData(controlSocket)
		, path_(path)
		, files_(files)
	{
	}

	virtual int Send() override;
	virtual int ParseResponse() override { return FZ_REPLY_INTERNALERROR; }
	virtual int SubcommandResult(int prevResult, COpData const& previousOperation) override;

	std::vector<std::tuple<LookupResults, CDirentry>> const& entries() const { return entries_; }

private:
	CServerPath const path_;
	std::vector<std::wstring> const files_;
	std::vector<std::tuple<LookupResults, CDirentry>> entries_;
};

#endif
