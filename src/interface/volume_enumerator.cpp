#include "filezilla.h"
#include "volume_enumerator.h"

#ifdef FZ_WINDOWS

#include <libfilezilla/glue/registry.hpp>

wxDEFINE_EVENT(fzEVT_VOLUMEENUMERATED, wxCommandEvent);
wxDEFINE_EVENT(fzEVT_VOLUMESENUMERATED, wxCommandEvent);

CVolumeDescriptionEnumeratorThread::CVolumeDescriptionEnumeratorThread(wxEvtHandler* pEvtHandler, fz::thread_pool & pool)
	: m_pEvtHandler(pEvtHandler)
{
	if (pEvtHandler) {
		drivesToHide_ = GetDrivesToHide();

		thread_ = pool.spawn([this] { entry(); });
		if (!thread_) {
			m_failure = true;
		}
	}
}

CVolumeDescriptionEnumeratorThread::~CVolumeDescriptionEnumeratorThread()
{
	m_stop = true;
	thread_.join();
	m_volumeInfo.clear();
}

void CVolumeDescriptionEnumeratorThread::entry()
{
	if (!m_pEvtHandler) {
		return;
	}

	if (!GetDriveLabels()) {
		m_failure = true;
	}

	m_pEvtHandler->QueueEvent(new wxCommandEvent(fzEVT_VOLUMESENUMERATED));
}

void CVolumeDescriptionEnumeratorThread::ProcessDrive(std::wstring const& drive)
{
	if (GetDriveLabel(drive)) {
		m_pEvtHandler->QueueEvent(new wxCommandEvent(fzEVT_VOLUMEENUMERATED));
	}

	if (GetDriveIcon(drive)) {
		m_pEvtHandler->QueueEvent(new wxCommandEvent(fzEVT_VOLUMEENUMERATED));
	}
}

bool CVolumeDescriptionEnumeratorThread::GetDriveLabel(std::wstring const& drive)
{
	if (drive.empty()) {
		return false;
	}

	std::wstring volume;
	if (drive[drive.size() - 1] == '\\') {
		if (drive.size() == 1) {
			return false;
		}
		volume = drive.substr(0, drive.size() - 1);
	}
	else {
		volume = drive;
	}

	// Check if it is a network share
	wchar_t share_name[512];
	DWORD dwSize = 511;
	if (!WNetGetConnection(volume.c_str(), share_name, &dwSize) && share_name[0]) {
		t_VolumeInfo volumeInfo;
		volumeInfo.volume = volume;
		volumeInfo.volumeName = share_name;
		fz::scoped_lock l(sync_);
		m_volumeInfo.push_back(volumeInfo);
		return true;
	}

	// Get the label of the drive
	wchar_t volume_name[501];
	int oldErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS);
	BOOL res = GetVolumeInformation(drive.c_str(), volume_name, 500, 0, 0, 0, 0, 0);
	SetErrorMode(oldErrorMode);
	if (res && volume_name[0]) {
		t_VolumeInfo volumeInfo;
		volumeInfo.volume = volume;
		volumeInfo.volumeName = volume_name;
		fz::scoped_lock l(sync_);
		m_volumeInfo.push_back(volumeInfo);
		return true;
	}

	return false;
}

bool CVolumeDescriptionEnumeratorThread::GetDriveIcon(std::wstring const& drive)
{
	if (drive.empty()) {
		return false;
	}

	std::wstring volume;
	if (drive[drive.size() - 1] == '\\') {
		if (drive.size() == 1) {
			return false;
		}
		volume = drive.substr(0, drive.size() - 1);
	}
	else {
		volume = drive;
	}

	SHFILEINFO shFinfo;
	memset(&shFinfo, 0, sizeof(SHFILEINFO));
	if (SHGetFileInfo(volume.c_str(), FILE_ATTRIBUTE_DIRECTORY,
		&shFinfo, sizeof(SHFILEINFO),
		SHGFI_ICON))
	{
		// we only need the index from the system image list
		DestroyIcon(shFinfo.hIcon);

		t_VolumeInfo volumeInfo;
		volumeInfo.volume = volume;
		volumeInfo.icon = shFinfo.iIcon;
		fz::scoped_lock l(sync_);
		m_volumeInfo.push_back(volumeInfo);
		return true;
	}

	return false;
}

bool CVolumeDescriptionEnumeratorThread::GetDriveLabels()
{
	std::vector<std::wstring> drives = GetDrives(drivesToHide_);

	if (drives.empty()) {
		return true;
	}

	std::vector<std::wstring>::const_iterator drive_a = drives.end();
	for (std::vector<std::wstring>::const_iterator it = drives.begin(); it != drives.end() && !m_stop; ++it) {
		if (m_stop) {
			return false;
		}

		std::wstring const& drive = *it;
		if ((drive[0] == 'a' || drive[0] == 'A') && drive_a == drives.end()) {
			// Defer processing of A:, most commonly the slowest of all drives.
			drive_a = it;
		}
		else {
			ProcessDrive(drive);
		}
	}

	if (drive_a != drives.end() && !m_stop) {
		ProcessDrive(*drive_a);
	}

	return !m_stop;
}

long CVolumeDescriptionEnumeratorThread::GetDrivesToHide()
{
	// Adhere to the NODRIVES group policy
	uint64_t drivesToHide = fz::regkey(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", true).int_value(L"NoDrives");
	return static_cast<long>(drivesToHide);
}

bool CVolumeDescriptionEnumeratorThread::IsHidden(wchar_t const* drive, long noDrives)
{
	int bit = 0;
	if (drive && drive[0] != 0 && drive[1] == ':') {
		wchar_t letter = drive[0];
		if (letter >= 'A' && letter <= 'Z') {
			bit = 1 << (letter - 'A');
		}
		else if (letter >= 'a' && letter <= 'z') {
			bit = 1 << (letter - 'a');
		}
	}

	return (noDrives & bit) != 0;
}

std::vector<std::wstring> CVolumeDescriptionEnumeratorThread::GetDrives()
{
	return GetDrives(GetDrivesToHide());
}

std::vector<std::wstring> CVolumeDescriptionEnumeratorThread::GetDrives(long drivesToHide)
{
	std::vector<std::wstring> ret;

	DWORD bufferLen{};
	wchar_t* drives{};

	DWORD neededLen = 1000;

	do {
		delete[] drives;

		bufferLen = neededLen * 2;
		drives = new wchar_t[bufferLen + 1];
		neededLen = GetLogicalDriveStrings(bufferLen, drives);
	} while (neededLen >= bufferLen);
	drives[neededLen] = 0;


	wchar_t const* pDrive = drives;
	while (*pDrive) {
		const int drivelen = fz::strlen(pDrive);

		if (!IsHidden(pDrive, drivesToHide)) {
			ret.emplace_back(pDrive);
		}

		pDrive += drivelen + 1;
	}

	delete[] drives;

	return ret;
}

std::vector<CVolumeDescriptionEnumeratorThread::t_VolumeInfo> CVolumeDescriptionEnumeratorThread::GetVolumes()
{
	std::vector<t_VolumeInfo> volumeInfo;

	{
		fz::scoped_lock l(sync_);
		m_volumeInfo.swap(volumeInfo);
	}

	return volumeInfo;
}

#endif
