#ifndef FILEZILLA_INTERFACE_STATUSBAR_HEADER
#define FILEZILLA_INTERFACE_STATUSBAR_HEADER

#include "option_change_event_handler.h"
#include "state.h"

#include "../include/sizeformatting.h"

#include <wx/timer.h>

#include <array>

enum widgets
{
	widget_led_send,
	widget_led_recv,
	widget_speedlimit,
	widget_datatype,
	widget_encryption,

	WIDGET_COUNT
};

class wxStatusBarEx : public wxStatusBar
{
public:
	wxStatusBarEx(wxTopLevelWindow* parent);
	virtual ~wxStatusBarEx();

	// We override these for two reasons:
	// - wxWidgets does not provide a function to get the field widths back
	// - fixup for last field. Under MSW it has a gripper if window is not
	//   maximized, under GTK2 it always has a gripper. These grippers overlap
	//   last field.
	virtual void SetFieldsCount(int number = 1, const int* widths = NULL);
	virtual void SetStatusWidths(int n, const int *widths);

	virtual void SetFieldWidth(int field, int width);

	int GetGripperWidth();

#ifdef __WXGTK__
	// Basically identical to the wx one, but not calling Update
	virtual void SetStatusText(const wxString& text, int number = 0);
#endif

protected:
	int GetFieldIndex(int field);

	wxTopLevelWindow* m_pParent;
#ifdef __WXMSW__
	bool m_parentWasMaximized;
#endif

	void FixupFieldWidth(int field);

	int* m_columnWidths;

	DECLARE_EVENT_TABLE()
	void OnSize(wxSizeEvent& event);
};

class CWidgetsStatusBar : public wxStatusBarEx
{
public:
	CWidgetsStatusBar(wxTopLevelWindow* parent);
	virtual ~CWidgetsStatusBar();

	// Adds a child window that gets repositioned on window resize
	// Positioned in the field given in the constructor,
	// right aligned and in reverse order.
	bool AddField(int field, int idx, wxWindow* pChild);

	void RemoveField(int idx);

	virtual void SetFieldWidth(int field, int width);
protected:

	struct t_statbar_child
	{
		int field;
		wxWindow* pChild;
	};

	std::map<int, t_statbar_child> m_children;

	void PositionChildren(int field);

	DECLARE_EVENT_TABLE()
	void OnSize(wxSizeEvent& event);
};

class activity_logger;
class CLed;
class COptionsBase;
class TimeFormatter;
class CStatusBar final : public CWidgetsStatusBar, public COptionChangeEventHandler, protected CGlobalStateEventHandler
{
public:
	CStatusBar(wxTopLevelWindow* parent, activity_logger& al, COptionsBase& options, TimeFormatter & time_formatter);
	virtual ~CStatusBar();

	void DisplayQueueSize(int64_t totalSize, bool hasUnknown);

	void OnHandleLeftClick(wxWindow* wnd);
	void OnHandleRightClick(wxWindow* wnd);

protected:
	void SetFieldBitmap(int field, wxStaticBitmap*& indicator, wxBitmap const& bmp, wxSize const& s);

	void OnActivity();
	void UpdateActivityTooltip();
	void UpdateSizeFormat();
	void DisplayDataType();
	void DisplayEncrypted();
	void UpdateSpeedLimitsIcon();

	void MeasureQueueSizeWidth();

	void ShowDataTypeMenu();

	virtual void OnOptionsChanged(watched_options const& options) override;
	virtual void OnStateChange(CState* pState, t_statechange_notifications notification, std::wstring const& data, const void* data2) override;

	void DoDisplayQueueSize();

	COptionsBase& options_;
	TimeFormatter & time_formatter_;

	SizeFormat m_sizeFormat;
	bool m_sizeFormatThousandsSep;
	int m_sizeFormatDecimalPlaces;
	int64_t m_size{};
	bool m_hasUnknownFiles{};

	activity_logger& activity_logger_;

	CLed* activityLeds_[2]{};
	wxStaticBitmap* m_pDataTypeIndicator{};
	wxStaticBitmap* m_pEncryptionIndicator{};
	wxStaticBitmap* m_pSpeedLimitsIndicator{};

	wxTimer m_queue_size_timer;
	wxTimer activityTimer_;
	bool m_queue_size_changed{};

	std::array<std::pair<fz::monotonic_clock, std::pair<uint64_t, uint64_t>>, 20> past_activity_;
	size_t past_activity_index_{};

	DECLARE_EVENT_TABLE()
	void OnSpeedLimitsEnable(wxCommandEvent& event);
	void OnSpeedLimitsConfigure(wxCommandEvent& event);
	void OnTimer(wxTimerEvent& ev);
};

#endif
