#ifndef FILEZILLA_VOLUME_ENUMERATOR_HEADER
#define FILEZILLA_VOLUME_ENUMERATOR_HEADER

#include <libfilezilla/thread_pool.hpp>

// Class to enumerate volume labels of volumes assigned
// a drive letter under MSW.
// Also gets the full UNC path for drive-mapped network
// shares.

// Windows has this very exotic concept of drive letters (nowadays called
// volumes), even if the drive isn't mounted (in the sense of no media
// inserted).
// This can result in a long seek time if trying to enumerate the volume
// labels, especially with legacy floppy drives (why are people still using
// them?). Worse, even if no floppy drive is installed the BIOS can report
// one to exist and Windows dutifully displays A:

// Since the local directory tree including the drives is populated at
// startup, use a background thread to obtain the labels.
#ifdef FZ_WINDOWS

wxDECLARE_EVENT(fzEVT_VOLUMEENUMERATED, wxCommandEvent);
wxDECLARE_EVENT(fzEVT_VOLUMESENUMERATED, wxCommandEvent);

class CVolumeDescriptionEnumeratorThread final
{
public:
	CVolumeDescriptionEnumeratorThread(wxEvtHandler* pEvtHandler, fz::thread_pool & pool);
	~CVolumeDescriptionEnumeratorThread();

	bool Failed() const { return m_failure; }

	struct t_VolumeInfo
	{
		std::wstring volume;
		std::wstring volumeName;
		int icon{-1};
	};

	std::vector<t_VolumeInfo> GetVolumes();

	static std::vector<std::wstring> GetDrives();

	static long GetDrivesToHide();
	static bool IsHidden(wchar_t const* drive, long noDrives);

protected:
	static std::vector<std::wstring> GetDrives(long drivesToHide);

	bool GetDriveLabels();
	void ProcessDrive(std::wstring const& drive);
	bool GetDriveLabel(std::wstring const& drive);
	bool GetDriveIcon(std::wstring const& drive);
	void entry();

	wxEvtHandler* m_pEvtHandler;

	bool m_failure{};
	bool m_stop{};

	std::vector<t_VolumeInfo> m_volumeInfo;

	fz::async_task thread_;
	fz::mutex sync_{false};

	long drivesToHide_{};
};

#endif

#endif
