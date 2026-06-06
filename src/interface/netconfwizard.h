#ifndef FILEZILLA_INTERFACES_NETCONFWIZARD_HEADER
#define FILEZILLA_INTERFACES_NETCONFWIZARD_HEADER

#include "wrapengine.h"

#include "../include/externalipresolver.h"

#include <wx/timer.h>
#include <wx/wizard.h>

#include <libfilezilla/buffer.hpp>
#include <libfilezilla/socket.hpp>

#define NETCONFBUFFERSIZE 200

class COptions;

class CNetConfWizard final : public wxWizard, protected CWrapEngine, protected fz::event_handler
{
public:
	CNetConfWizard(wxWindow* parent, CFileZillaEngineContext & context);
	virtual ~CNetConfWizard();

	bool Load();
	bool Run();

protected:

	bool CreatePages();

	void PrintMessage(std::wstring const& msg, int type);

	void ResetTest();

	CFileZillaEngineContext & engine_context_;

	wxWindow* const m_parent;

	std::vector<wxWizardPageSimple*> m_pages;

	DECLARE_EVENT_TABLE()
	void OnPageChanging(wxWizardEvent& event);
	void OnPageChanged(wxWizardEvent& event);
	void OnRestart();
	void OnFinish(wxWizardEvent& event);
	void OnTimer(wxTimerEvent& event);
	void OnExternalIPAddress2(wxCommandEvent&);

	virtual void operator()(fz::event_base const& ev);
	void OnExternalIPAddress();
	void OnSocketEvent(fz::socket_event_source*, fz::socket_event_flag t, int error);

	void DoOnSocketEvent(fz::socket_event_source*, fz::socket_event_flag t, int error);

	void OnReceive();
	void ParseResponse(const char* line);
	void OnClose();
	void OnConnect();
	void OnSend();
	void CloseSocket();
	bool Send(std::wstring const& cmd);
	void OnDataReceive();
	void OnDataClose();

	void OnAccept();

	void SendNextCommand();

	std::wstring GetExternalIPAddress();

	int CreateListenSocket();
	int CreateListenSocket(unsigned int port);

	wxString m_nextLabelText;

	// Test data
	std::unique_ptr<fz::socket> socket_;
	int m_state;

	char m_recvBuffer[NETCONFBUFFERSIZE];
	int m_recvBufferPos;
	bool m_testDidRun;
	bool m_connectSuccessful;

	enum testResults
	{
		unknown,
		successful,
		mismatch,
		tainted,
		mismatchandtainted,
		servererror,
		externalfailed,
		datatainted
	} m_testResult;

	CExternalIPResolver* m_pIPResolver{};
	std::wstring m_externalIP;

	std::unique_ptr<fz::listen_socket> listen_socket_;
	std::unique_ptr<fz::socket> data_socket_;
	int m_listenPort{};
	bool gotListReply{};
	int m_data{};

	fz::buffer sendBuffer_;

	wxTimer m_timer;

	struct impl;
	std::unique_ptr<impl> impl_;
};

#endif
