#include "updater.h"

#if FZ_MANUALUPDATECHECK

#include "buildinfo.h"
#include "fz_paths.h"
#include "site.h"
#include "updater_cert.h"

#ifdef FZ_WINDOWS
#include <libfilezilla/glue/registry.hpp>
#elif FZ_MAC
#include <sys/sysctl.h>
#endif

#include "../include/engine_context.h"
#include "../include/engine_options.h"
#include "../include/FileZillaEngine.h"
#include "../include/misc.h"
#include "../include/version.h"

#include <libfilezilla/file.hpp>
#include <libfilezilla/hash.hpp>
#include <libfilezilla/invoker.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/signature.hpp>
#include <libfilezilla/translate.hpp>

namespace {
struct run_event_type;
typedef fz::simple_event<run_event_type, bool> run_event;

unsigned int register_updater_options()
{
	static int const value = register_options({
		{ "Disable update check", false, option_flags::predefined_only },

		{ "Update Check", 1, option_flags::normal, 0, 1 },
		{ "Update Check Interval", 7, option_flags::normal, 1, 7 },
		{ "Last automatic update check", L"", option_flags::product, 100 },
		{ "Last automatic update version", L"", option_flags::product },
		{ "Update Check New Version", L"", option_flags::platform|option_flags::product },
		{ "Update Check Check Beta", 0, option_flags::normal, 0, 2 },
	});
	return value;
}

option_registrator r(&register_updater_options);
}

optionsIndex mapOption(updaterOptions opt)
{
	static unsigned int const offset = register_updater_options();

	auto ret = optionsIndex::invalid;
	if (opt < OPTIONS_UPDATER_NUM) {
		return static_cast<optionsIndex>(opt + offset);
	}
	return ret;
}

static CUpdater* instance = 0;

CUpdater::CUpdater(CFileZillaEngineContext& engine_context)
	: fz::event_handler(engine_context.GetEventLoop())
	, state_(UpdaterState::idle)
	, engine_context_(engine_context)
{
	if (!instance) {
		instance = this;
	}

	send_event<run_event>(false);
}

UpdaterState CUpdater::LoadLocalData()
{
	// Load existing data if it isn't stale, or if it is stale and we cannot check anew
	{
		fz::scoped_lock l(mtx_);
		log_.clear();
		raw_version_information_.clear();
		auto& options = engine_context_.GetOptions();
		if (!LongTimeSinceLastCheck() || options.get_int(OPTION_DEFAULT_DISABLEUPDATECHECK) != 0) {
			raw_version_information_ = options.get_string(OPTION_UPDATECHECK_NEWVERSION);
		}
	}

	stop_timer(update_timer_);
	update_timer_ = add_timer(fz::duration::from_hours(1), false);

	return ProcessFinishedData(FZ_AUTOUPDATECHECK);
}

CUpdater::~CUpdater()
{
	remove_handler();

	if (instance == this) {
		instance = 0;
	}

	delete engine_;
}

CUpdater* CUpdater::GetInstance()
{
	return instance;
}

bool CUpdater::LongTimeSinceLastCheck() const
{
	auto& options = engine_context_.GetOptions();
	std::wstring const lastCheckStr = options.get_string(OPTION_UPDATECHECK_LASTDATE);
	if (lastCheckStr.empty()) {
		return true;
	}

	fz::datetime lastCheck(lastCheckStr, fz::datetime::utc);
	if (lastCheck.empty()) {
		return true;
	}

	auto const span = fz::datetime::now() - lastCheck;

	if (span.get_seconds() < 0) {
		// Last check in future
		return true;
	}

	int days = 1;
	if (!CBuildInfo::IsUnstable()) {
		days = options.get_int(OPTION_UPDATECHECK_INTERVAL);
	}
	return span.get_days() >= days;
}

#if FZ_WINDOWS
namespace {
bool SystemIs64bit()
{
#ifdef _WIN64
	// Technically, it could still be a 32bit system emulating 64bit...
	return true;
#else
	BOOL w64{};
	IsWow64Process(GetCurrentProcess(), &w64);
	return w64;
#endif
}
}
#endif

fz::uri CUpdater::GetUrl()
{
	fz::uri uri("https://update.filezilla-project.org/update.php");
	fz::query_string qs;

	std::string host = fz::to_utf8(CBuildInfo::GetHostname());
	if (host.empty()) {
		host = "unknown";
	}
	qs["platform"] = host;
	qs["version"] = fz::to_utf8(GetFileZillaVersion());

#if defined(FZ_WINDOWS) || defined(FZ_MAC)
	// Makes not much sense to submit OS version on Linux, *BSD and the likes, too many flavours.
	auto sysver = GetSystemVersion();
	if (sysver) {
		qs["osversion"] = fz::sprintf("%u.%u", sysver.major, sysver.minor);
	}
#endif

#ifdef FZ_WINDOWS
	if (SystemIs64bit()) {
		qs["osarch"] = "64";
	}
	else {
		qs["osarch"] = "32";
	}

	// Add information about package
	// Installer always writes to 32bit section
	auto key = fz::regkey(HKEY_CURRENT_USER, L"Software\\FileZilla Client", true, fz::regkey::regview_32);
	if (!key) {
		key.open(HKEY_LOCAL_MACHINE, L"Software\\FileZilla Client", true, fz::regkey::regview_32);
	}

	if (key.has_value(L"Updated")) {
		qs["updated"] = fz::to_string(key.int_value(L"Updated"));
	}
	if (key.has_value(L"Package")) {
		qs["package"] = fz::to_string(key.int_value(L"Package"));
	}
	if (key.has_value(L"Channel")) {
		qs["channel"] = fz::to_string(key.int_value(L"Channel"));
	}
#elif FZ_MAC
	int arch{};
	size_t len = sizeof(arch);
	if (sysctlbyname("hw.optional.arm64", &arch, &len, nullptr, 0) == 0) {
		if (arch == 1) {
			qs["osarch"] = "arm64";
		}
	}
#endif

	std::string const cpuCaps = fz::to_utf8(CBuildInfo::GetCPUCaps(','));
	if (!cpuCaps.empty()) {
		qs["cpuid"] = cpuCaps;
	}

	std::wstring const lastVersion = engine_context_.GetOptions().get_string(OPTION_UPDATECHECK_LASTVERSION);
	if (lastVersion != GetFileZillaVersion()) {
		qs["initial"] = "1";
	}
	else {
		qs["initial"] = "0";
	}

	if (manual_) {
		qs["manual"] = "1";
	}

	if (GetEnv("FZUPDATETEST") == L"1") {
		qs["test"] = "1";
	}
	uri.query_ = qs.to_string(true);
	return uri;
}

void CUpdater::OnRun(bool manual)
{
	if (Busy()) {
		return;
	}

	if (GetFileZillaVersion().empty()) {
		return;
	}

	manual_ = manual;
	SetState(UpdaterState::checking);

	UpdaterState s = LoadLocalData();

	if (!ShouldCheck(s)) {
		SetState(s);
		return;
	}

	auto const t = fz::datetime::now();
	engine_context_.GetOptions().set(OPTION_UPDATECHECK_LASTDATE, t.format(L"%Y-%m-%d %H:%M:%S", fz::datetime::utc));

	{
		fz::scoped_lock l(mtx_);
		local_file_.clear();
		log_ = fz::sprintf(fztranslate("Started update check on %s\n"), t.format(L"%Y-%m-%d %H:%M:%S", fz::datetime::local));
	}

	std::wstring build = CBuildInfo::GetBuildType();
	if (build.empty())  {
		build = fztranslate("custom");
	}

	{
		fz::scoped_lock l(mtx_);
		log_ += fz::sprintf(fztranslate("Own build type: %s\n"), build);
	}

	m_use_internal_rootcert = true;
	int res = Request(GetUrl());

	if (res != FZ_REPLY_WOULDBLOCK) {
		SetState(UpdaterState::failed);
	}
	raw_version_information_.clear();
}

int CUpdater::Download(std::wstring const& url, std::wstring const& local_file)
{
	if (!pending_commands_.empty()) {
		return FZ_REPLY_ERROR;
	}

	pending_commands_.clear();
	pending_commands_.emplace_back(new CDisconnectCommand);
	if (!CreateConnectCommand(url) || !CreateTransferCommand(url, local_file)) {
		pending_commands_.clear();
		return FZ_REPLY_ERROR;
	}

	return ContinueDownload();
}

int CUpdater::Request(fz::uri const& uri)
{
	if (!pending_commands_.empty()) {
		return FZ_REPLY_ERROR;
	}

	pending_commands_.clear();
	pending_commands_.emplace_back(new CDisconnectCommand);

	CServer server(fz::equal_insensitive_ascii(uri.scheme_, std::string("http")) ? HTTP : HTTPS, UNIX, fz::to_wstring_from_utf8(uri.host_), uri.port_);
	pending_commands_.emplace_back(new CConnectCommand(server, ServerHandle(), Credentials()));
	pending_commands_.emplace_back(new CHttpRequestCommand(uri, fz::writer_factory_holder(std::make_unique<fz::buffer_writer_factory>(output_buffer_, L"Updater", 1024*1024)), "GET", fz::reader_factory_holder(), true));

	return ContinueDownload();
}

int CUpdater::ContinueDownload()
{
	if (pending_commands_.empty()) {
		return FZ_REPLY_OK;
	}

	if (!engine_) {
		engine_ = new CFileZillaEngine(engine_context_, fz::make_invoker(*this, [this](CFileZillaEngine* engine){ OnEngineEvent(engine); }));
	}

	int res = engine_->Execute(*pending_commands_.front());
	if (res == FZ_REPLY_OK) {
		pending_commands_.pop_front();
		return ContinueDownload();
	}

	return res;
}

bool CUpdater::CreateConnectCommand(std::wstring const& url)
{
	Site s;
	CServerPath path;
	std::wstring error;
	if (!s.ParseUrl(url, 0, std::wstring(), std::wstring(), error, path) || (s.server.GetProtocol() != HTTP && s.server.GetProtocol() != HTTPS)) {
		return false;
	}

	pending_commands_.emplace_back(new CConnectCommand(s.server, s.Handle(), s.credentials));
	return true;
}

bool CUpdater::CreateTransferCommand(std::wstring const& url, std::wstring const& local_file)
{
	if (local_file.empty()) {
		return false;
	}

	Site s;
	CServerPath path;
	std::wstring error;
	if (!s.ParseUrl(url, 0, std::wstring(), std::wstring(), error, path) || (s.server.GetProtocol() != HTTP && s.server.GetProtocol() != HTTPS)) {
		return false;
	}
	std::wstring file = path.GetLastSegment();
	path = path.GetParent();

	transfer_flags const flags = transfer_flags::download;
	auto cmd = new CFileTransferCommand(fz::file_writer_factory(local_file, engine_context_.GetThreadPool(), fz::file_writer_flags::fsync), path, file, flags);
	resume_offset_ = cmd->GetWriter().size();
	if (resume_offset_ == fz::aio_base::nosize) {
		resume_offset_ = 0;
	}
	pending_commands_.emplace_back(cmd);
	return true;
}

void CUpdater::OnEngineEvent(CFileZillaEngine* engine)
{
	if (!engine_ || engine_ != engine) {
		return;
	}

	std::unique_ptr<CNotification> notification;
	while ((notification = engine_->GetNextNotification())) {
		ProcessNotification(std::move(notification));
	}
}

void CUpdater::ProcessNotification(std::unique_ptr<CNotification> && notification)
{
	if (state_ != UpdaterState::checking && state_ != UpdaterState::newversion_downloading) {
		return;
	}

	switch (notification->GetID())
	{
	case nId_asyncrequest:
		{
			auto pData = unique_static_cast<CAsyncRequestNotification>(std::move(notification));
			if (pData->GetRequestID() == reqId_fileexists) {
				static_cast<CFileExistsNotification *>(pData.get())->overwriteAction = CFileExistsNotification::resume;
			}
			else if (pData->GetRequestID() == reqId_certificate) {
				auto & certNotification = static_cast<CCertificateNotification &>(*pData.get());
				if (m_use_internal_rootcert) {
					auto certs = certNotification.info_.get_certificates();
					if (certs.size() > 1) {
						auto const& ca = certs.back();
						std::vector<uint8_t> ca_data = ca.get_raw_data();

						auto const updater_root = fz::base64_decode(updater_cert);
						if (ca_data == updater_root) {
							certNotification.trusted_ = true;
						}
					}
				}
				else {
					certNotification.trusted_ = true;
				}
			}
			engine_->SetAsyncRequestReply(std::move(pData));
		}
		break;
	case nId_operation:
		ProcessOperation(static_cast<COperationNotification const&>(*notification.get()));
		break;
	case nId_logmsg:
		{
			auto const& msg = static_cast<CLogmsgNotification const&>(*notification.get());

			fz::scoped_lock l(mtx_);
			log_ += msg.msg + L"\n";
		}
		break;
	default:
		break;
	}
}

UpdaterState CUpdater::ProcessFinishedData(bool can_download)
{
	UpdaterState s = UpdaterState::failed;

	ParseData();

	if (version_information_.eol_) {
		s = UpdaterState::eol;
	}
	else if (version_information_.available_.version_.empty()) {
		s = UpdaterState::idle;
	}
	else if (!version_information_.available_.url_.empty()) {
		std::wstring const temp = GetTempFile();
		std::wstring const local_file = GetLocalFile(version_information_.available_, true);
		if (!local_file.empty() && fz::local_filesys::get_file_type(fz::to_native(local_file)) != fz::local_filesys::unknown) {
			fz::scoped_lock l(mtx_);
			local_file_ = local_file;
			log_ += fz::sprintf(fztranslate("Local file is %s\n"), local_file);
			s = UpdaterState::newversion_ready;
		}
		else {
			// We got a checksum over a secure channel already.
			m_use_internal_rootcert = false;

			if (temp.empty() || local_file.empty()) {
				s = UpdaterState::newversion;
			}
			else {
				s = UpdaterState::newversion_downloading;
				auto size = fz::local_filesys::get_size(fz::to_native(temp));
				if (size >= 0 && size >= version_information_.available_.size_) {
					s = ProcessFinishedDownload();
				}
				else if (!can_download || Download(version_information_.available_.url_, temp) != FZ_REPLY_WOULDBLOCK) {
					s = UpdaterState::newversion;
				}
			}
		}
	}
	else {
		s = UpdaterState::newversion;
	}

	return s;
}

void CUpdater::ProcessOperation(COperationNotification const& operation)
{
	if (state_ != UpdaterState::checking && state_ != UpdaterState::newversion_downloading) {
		return;
	}

	if (pending_commands_.empty()) {
		SetState(UpdaterState::failed);
		return;
	}


	UpdaterState s = UpdaterState::failed;

	int res = operation.replyCode_;
	if (res == FZ_REPLY_OK || (operation.commandId_ == Command::disconnect && res & FZ_REPLY_DISCONNECTED)) {
		pending_commands_.pop_front();
		res = ContinueDownload();
		if (res == FZ_REPLY_WOULDBLOCK) {
			return;
		}
	}

	if (res != FZ_REPLY_OK) {
		if (state_ == UpdaterState::newversion_downloading) {
			auto temp = GetTempFile();
			if (!temp.empty()) {
				int64_t s = fz::local_filesys::get_size(fz::to_native(temp));
				if (s > 0 && static_cast<uint64_t>(s) > resume_offset_) {
					resume_offset_ = static_cast<uint64_t>(s);
					res = ContinueDownload();
					if (res == FZ_REPLY_WOULDBLOCK) {
						return;
					}
				}
			}
		}
		if (state_ != UpdaterState::checking) {
			s = UpdaterState::newversion;
		}
	}
	else if (state_ == UpdaterState::checking) {
		if (!FilterOutput()) {
			SetState(UpdaterState::failed);
			return;
		}
		engine_context_.GetOptions().set(OPTION_UPDATECHECK_LASTVERSION, GetFileZillaVersion());
		s = ProcessFinishedData(true);
	}
	else {
		s = ProcessFinishedDownload();
	}
	SetState(s);
}

UpdaterState CUpdater::ProcessFinishedDownload()
{
	UpdaterState s = UpdaterState::newversion;

	std::wstring const temp = GetTempFile();
	if (temp.empty()) {
		s = UpdaterState::newversion;
	}
	else if (!VerifyChecksum(temp, version_information_.available_.size_, version_information_.available_.hash_)) {
		fz::remove_file(fz::to_native(temp), false);
		s = UpdaterState::newversion;
	}
	else {
		s = UpdaterState::newversion_ready;

		std::wstring local_file = GetLocalFile(version_information_.available_, false);

		if (local_file.empty() || !fz::rename_file(fz::to_native(temp), fz::to_native(local_file))) {
			s = UpdaterState::newversion;
			fz::remove_file(fz::to_native(temp), false);
			fz::scoped_lock l(mtx_);
			log_ += fz::sprintf(fztranslate("Could not create local file %s\n"), local_file);
		}
		else {
			fz::scoped_lock l(mtx_);
			local_file_ = local_file;
			log_ += fz::sprintf(fztranslate("Local file is %s\n"), local_file);
		}
	}

	return s;
}

std::wstring CUpdater::GetLocalFile(build const& b, bool allow_existing)
{
	std::wstring const fn = GetFilename(b.url_);
	std::wstring const dl = GetDownloadDir().GetPath();
	if (dl.empty()) {
		return {};
	}

	int i = 1;
	std::wstring f = dl + fn;

	while (fz::local_filesys::get_file_type(fz::to_native(f)) != fz::local_filesys::unknown && (!allow_existing || !VerifyChecksum(f, b.size_, b.hash_))) {
		if (++i > 99) {
			return std::wstring();
		}

		size_t pos;
		if (fn.size() > 8 && fz::str_tolower_ascii(fn.substr(fn.size() - 8)) == L".tar.bz2") {
			pos = fn.size() - 8;
		}
		else {
			pos = fn.rfind('.');
		}

		if (pos == std::wstring::npos) {
			f = dl + fn + fz::sprintf(L" (%d)", i);
		}
		else {
			f = dl + fn.substr(0, pos) + fz::sprintf(L" (%d)", i) + fn.substr(pos);
		}
	}

	return f;
}

bool CUpdater::FilterOutput()
{
	if (state_ != UpdaterState::checking) {
		return false;
	}

	raw_version_information_.resize(output_buffer_.size());
	for (size_t i = 0; i < output_buffer_.size(); ++i) {
		if (output_buffer_[i] < 10 || static_cast<unsigned char>(output_buffer_[i]) > 127) {
			fz::scoped_lock l(mtx_);
			log_ += fztranslate("Received invalid character in version information") + L"\n";
			raw_version_information_.clear();
			return false;
		}
		raw_version_information_[i] = output_buffer_[i];
	}

	return true;
}

void CUpdater::ParseData()
{
	int64_t const ownVersionNumber = ConvertToVersionNumber(GetFileZillaVersion().c_str());

	fz::scoped_lock l(mtx_);
	version_information_ = version_information();

	std::wstring raw_version_information = raw_version_information_;

	log_ += fz::sprintf(fztranslate("Parsing %d bytes of version information.\n"), static_cast<int>(raw_version_information.size()));

	auto & options = engine_context_.GetOptions();

	while (!raw_version_information.empty()) {
		std::wstring line;
		size_t pos = raw_version_information.find('\n');
		if (pos != std::wstring::npos) {
			line = raw_version_information.substr(0, pos);
			raw_version_information = raw_version_information.substr(pos + 1);
		}
		else {
			line = raw_version_information;
			raw_version_information.clear();
		}

		auto const tokens = fz::strtok(line, L" \t\r\n");
		if (tokens.empty()) {
			// After empty line, changelog follows
			version_information_.changelog_ = raw_version_information;
			fz::trim(version_information_.changelog_);

			if (options.get_int(OPTION_LOGGING_DEBUGLEVEL) == 4) {
				log_ += fz::sprintf(L"Changelog: %s\n", version_information_.changelog_);
			}
			break;
		}

		std::wstring const& type = tokens[0];
		if (tokens.size() < 2) {
			if (options.get_int(OPTION_LOGGING_DEBUGLEVEL) == 4) {
				log_ += fz::sprintf(L"Skipping line with one token of type %s\n", type);
			}
			continue;
		}

		if (type == L"resources") {
			if (UpdatableBuild()) {
				version_information_.resources_[resource_type::update_dialog] = tokens[1];
			}
			continue;
		}
		else if (type == L"resource") {
			if (tokens.size() >= 3) {
				std::wstring resource;
				for (size_t i = 2; i < tokens.size(); ++i) {
					if (!resource.empty()) {
						resource += ' ';
					}
					resource += tokens[i];
				}
				version_information_.resources_[fz::to_integral<resource_type>(tokens[1])] = std::move(resource);
			}
			continue;
		}
		else if (type == L"eol") {
#if defined(FZ_WINDOWS) || defined(FZ_MAC)
			std::string host = fz::to_utf8(CBuildInfo::GetHostname());
			if (host.empty()) {
				host = "unknown";
			}
			fz::to_utf8(GetFileZillaVersion());

			auto sysver = GetSystemVersion();
			std::string data = host + '|' + fz::to_utf8(GetFileZillaVersion()) + '|' + fz::sprintf("%u.%u", sysver.major, sysver.minor);

			bool valid_signature{};
			for (size_t i = 1; i < tokens.size(); ++i) {
				auto const& token = tokens[i];
				if (token.substr(0, 4) == L"sig:") {
					auto const& sig = token.substr(4);
					auto raw_sig = fz::base64_decode_s(fz::to_utf8(sig));

					if (!raw_sig.empty()) {
						auto const pub = fz::public_verification_key::from_base64("xrjuitldZT7pvIhK9q1GVNfptrepB/ctt5aK1QO5RaI");
						valid_signature = fz::verify(data, raw_sig, pub);
					}
				}
			}
			if (!valid_signature) {
				log_ += fz::sprintf(L"Ignoring eol statement not matching our version and platform.\n");
				continue;
			}
			version_information_.eol_ = true;
#endif
		}

		std::wstring const& versionOrDate = tokens[1];

		if (type == L"nightly") {
			fz::datetime nightlyDate(versionOrDate, fz::datetime::utc);
			if (nightlyDate.empty()) {
				if (options.get_int(OPTION_LOGGING_DEBUGLEVEL) == 4) {
					log_ += L"Could not parse nightly date\n";
				}
				continue;
			}

			fz::datetime buildDate = CBuildInfo::GetBuildDate();
			if (buildDate.empty() || nightlyDate.empty() || nightlyDate <= buildDate) {
				if (options.get_int(OPTION_LOGGING_DEBUGLEVEL) == 4) {
					log_ += L"Nightly isn't newer\n";
				}
				continue;
			}
		}
		else if (type == L"release" || type == L"beta") {
			int64_t v = ConvertToVersionNumber(versionOrDate.c_str());
			if (v <= ownVersionNumber) {
				continue;
			}
		}
		else {
			if (options.get_int(OPTION_LOGGING_DEBUGLEVEL) == 4) {
				log_ += fz::sprintf(L"Skipping line with unknown type %s\n", type);
			}
			continue;
		}

		build b;
		b.version_ = versionOrDate;

		if (tokens.size() < 6) {
			if (options.get_int(OPTION_LOGGING_DEBUGLEVEL) == 4) {
				log_ += fz::sprintf(L"Not parsing build line with only %d tokens", tokens.size());
			}
		}
		else if (UpdatableBuild()) {
			std::wstring const& url = tokens[2];
			std::wstring const& sizestr = tokens[3];
			std::wstring const& hash_algo = tokens[4];
			std::wstring const& hash = tokens[5];

			if (GetFilename(url).empty()) {
				if (options.get_int(OPTION_LOGGING_DEBUGLEVEL) == 4) {
					log_ += fz::sprintf(L"Could not extract filename from URL: %s\n", url);
				}
				continue;
			}

			if (!fz::equal_insensitive_ascii(hash_algo, std::wstring(L"sha512"))) {
				continue;
			}

			auto const size = fz::to_integral<uint64_t>(sizestr);
			if (!size) {
				if (options.get_int(OPTION_LOGGING_DEBUGLEVEL) == 4) {
					log_ += fz::sprintf(L"Could not parse size: %s\n", sizestr);
				}
				continue;
			}

			bool valid_signature{};
			for (size_t i = 6; i < tokens.size(); ++i) {
				auto const& token = tokens[i];
				if (token.substr(0, 4) == L"sig:") {
					auto const& sig = token.substr(4);
					auto raw_sig = fz::base64_decode(fz::to_utf8(sig));
					auto raw_hash = fz::hex_decode(hash);

					// Append the version to the file hash to protect against replays
					raw_hash.push_back(0);
					raw_hash.insert(raw_hash.cend(), versionOrDate.cbegin(), versionOrDate.cend());

					if (!raw_sig.empty() || !raw_hash.empty()) {
						auto const pub = fz::public_verification_key::from_base64("xrjuitldZT7pvIhK9q1GVNfptrepB/ctt5aK1QO5RaI");
						valid_signature = fz::verify(raw_hash, raw_sig, pub);
					}
				}
			}
			if (!valid_signature) {
				log_ += fz::sprintf(L"Ignoring line with inalid or missing signature for hash %s\n", hash);
				continue;
			}

			b.url_ = url;
			b.size_ = size;
			b.hash_ = fz::str_tolower_ascii(hash);
			bool valid_hash = true;
			for (auto const& c : b.hash_) {
				if ((c < 'a' || c > 'f') && (c < '0' || c > '9')) {
					valid_hash = false;
					break;
				}
			}
			if (!valid_hash) {
				log_ += fz::sprintf(fztranslate("Invalid hash: %s\n"), hash);
				continue;
			}

			// @translator: Two examples: Found new nightly 2014-04-03\n, Found new release 3.9.0.1\n
			log_ += fz::sprintf(fztranslate("Found new %s %s\n"), type, b.version_);
		}

		if (type == L"nightly" && UpdatableBuild()) {
			version_information_.nightly_ = b;
		}
		else if (type == L"release") {
			version_information_.stable_ = b;
		}
		else if (type == L"beta") {
			version_information_.beta_ = b;
		}
	}

	if (!version_information_.nightly_.url_.empty() && options.get_int(OPTION_UPDATECHECK_CHECKBETA) == 2) {
		version_information_.available_ = version_information_.nightly_;
	}
	else if (!version_information_.beta_.version_.empty() && options.get_int(OPTION_UPDATECHECK_CHECKBETA) != 0) {
		version_information_.available_ = version_information_.beta_;
	}
	else {
		version_information_.available_ = version_information_.stable_;
	}

	options.set(OPTION_UPDATECHECK_NEWVERSION, raw_version_information_);
}

void CUpdater::operator()(fz::event_base const& ev)
{
	fz::dispatch<run_event, fz::timer_event>(ev, this, &CUpdater::OnRun, &CUpdater::on_timer);
}

void CUpdater::on_timer(fz::timer_id const&)
{
	OnRun(false);
}

bool CUpdater::VerifyChecksum(std::wstring const& file, int64_t size, std::wstring const& checksum)
{
	if (file.empty() || checksum.empty()) {
		return false;
	}

	auto filesize = fz::local_filesys::get_size(fz::to_native(file));
	if (filesize < 0) {
		log_ += fz::sprintf(fztranslate("Could not obtain size of '%s'"), file) + L"\n";
		return false;
	}
	else if (filesize != size) {
		log_ += fz::sprintf(fztranslate("Local size of '%s' does not match expected size: %d != %d"), file, filesize, size) + L"\n";
		return false;
	}

	fz::hash_accumulator acc(fz::hash_algorithm::sha512);

	{
		fz::file f(fz::to_native(file), fz::file::reading);
		if (!f.opened()) {
			log_ += fz::sprintf(fztranslate("Could not open '%s'"), file) + L"\n";
			return false;
		}
		unsigned char buffer[65536];
		fz::rwresult read;
		while ((read = f.read2(buffer, sizeof(buffer))) && read.value_ > 0) {
			acc.update(buffer, read.value_);
		}
		if (!read) {
			log_ += fz::sprintf(fztranslate("Could not read from '%s'"), file) + L"\n";
			return false;
		}
	}

	auto const digest = fz::hex_encode<std::wstring>(acc.digest());

	if (digest != checksum) {
		log_ += fz::sprintf(fztranslate("Checksum mismatch on file %s\n"), file);
		return false;
	}

	log_ += fz::sprintf(fztranslate("Checksum match on file %s\n"), file);
	return true;
}

std::wstring CUpdater::GetTempFile() const
{
	if (version_information_.available_.hash_.empty()) {
		return std::wstring();
	}

	std::wstring ret = GetTempDir().GetPath();
	if (!ret.empty()) {
		ret += L"fzupdate_" + version_information_.available_.hash_.substr(0, 16) + L".tmp";
	}

	return ret;
}

std::wstring CUpdater::GetFilename(std::wstring const& url) const
{
	std::wstring ret;
	size_t pos = url.rfind('/');
	if (pos != std::wstring::npos) {
		ret = url.substr(pos + 1);
	}
	size_t p = ret.find_first_of(L"?#");
	if (p != std::string::npos) {
		ret = ret.substr(0, p);
	}
#ifdef FZ_WINDOWS
	fz::replace_substrings(ret, L":", L"_");
	fz::replace_substrings(ret, L"\\", L"_");
#endif

	return ret;
}

void CUpdater::SetState(UpdaterState s)
{
	if (s != state_) { // No mutex needed for this, no other thread changes state

		fz::scoped_lock l(mtx_);
		state_ = s;

		if (s != UpdaterState::checking && s != UpdaterState::newversion_downloading) {
			pending_commands_.clear();
		}
		build b = version_information_.available_;
		for (auto const& handler : handlers_) {
			if (handler) {
				handler->UpdaterStateChanged(s, b);
			}
		}
	}
}

std::wstring CUpdater::DownloadedFile() const
{
	fz::scoped_lock l(mtx_);

	std::wstring ret;
	if (state_ == UpdaterState::newversion_ready) {
		ret = local_file_;
	}
	return ret;
}

void CUpdater::AddHandler(CUpdateHandler& handler)
{
	fz::scoped_lock l(mtx_);

	for (auto const& h : handlers_) {
		if (h == &handler) {
			return;
		}
	}
	for (auto& h : handlers_) {
		if (!h) {
			h = &handler;
			return;
		}
	}
	handlers_.push_back(&handler);
	if (state_ != UpdaterState::idle) {
		handler.UpdaterStateChanged(state_, version_information_.available_);
	}
}

void CUpdater::RemoveHandler(CUpdateHandler& handler)
{
	fz::scoped_lock l(mtx_);

	for (auto& h : handlers_) {
		if (h == &handler) {
			// Set to 0 instead of removing from list to avoid issues with reentrancy.
			h = 0;
			return;
		}
	}
}

int64_t CUpdater::BytesDownloaded() const
{
	fz::scoped_lock l(mtx_);

	int64_t ret{-1};
	if (state_ == UpdaterState::newversion_ready) {
		if (!local_file_.empty()) {
			ret = fz::local_filesys::get_size(fz::to_native(local_file_));
		}
	}
	else if (state_ == UpdaterState::newversion_downloading) {
		std::wstring const temp = GetTempFile();
		if (!temp.empty()) {
			ret = fz::local_filesys::get_size(fz::to_native(temp));
		}
	}
	return ret;
}

bool CUpdater::UpdatableBuild() const
{
	fz::scoped_lock l(mtx_);
	return CBuildInfo::GetBuildType() == L"nightly" || CBuildInfo::GetBuildType() == L"official";
}

bool CUpdater::Busy() const
{
	fz::scoped_lock l(mtx_);
	return state_ == UpdaterState::checking || state_ == UpdaterState::newversion_downloading;
}

std::wstring CUpdater::GetResources(resource_type t) const
{
	fz::scoped_lock l(mtx_);

	std::wstring ret;
	auto const it = version_information_.resources_.find(t);
	if (it != version_information_.resources_.cend()) {
		ret = it->second;
	}
	return ret;
}

bool CUpdater::ShouldCheck(UpdaterState & s)
{
	if (manual_) {
		build const b = AvailableBuild();
		if (s == UpdaterState::idle || s == UpdaterState::failed || s == UpdaterState::newversion_stale ||
			s == UpdaterState::eol ||
			LongTimeSinceLastCheck() || (s == UpdaterState::newversion && !b.url_.empty()) ||
			(s == UpdaterState::newversion_ready && !VerifyChecksum(DownloadedFile(), b.size_, b.hash_)))
		{
			return true;
		}
	}
	else {
#if FZ_AUTOUPDATECHECK
		if (s == UpdaterState::failed || s == UpdaterState::idle || s == UpdaterState::newversion_stale) {
			auto& options = engine_context_.GetOptions();
			if (!options.get_int(OPTION_DEFAULT_DISABLEUPDATECHECK) && options.get_int(OPTION_UPDATECHECK) != 0) {
				if (LongTimeSinceLastCheck()) {
					return true;
				}
			}
			else {
				auto const age = fz::datetime::now() - CBuildInfo::GetBuildDate();
				if (age >= fz::duration::from_days(31 * 6)) {
					version_information_ = version_information();
					s = UpdaterState::newversion_stale;
				}
			}
		}
#endif
	}

	return false;
}

UpdaterState CUpdater::GetState() const
{
	fz::scoped_lock l(mtx_);
	return state_;
}

build CUpdater::AvailableBuild() const
{
	fz::scoped_lock l(mtx_);
	return version_information_.available_;
}

std::wstring CUpdater::GetChangelog() const
{
	fz::scoped_lock l(mtx_);
	return version_information_.changelog_;
}

std::wstring CUpdater::GetLog() const
{
	fz::scoped_lock l(mtx_);
	return log_;
}

void CUpdater::Run(bool manual)
{
	send_event<run_event>(manual);
}

void CUpdater::Reset()
{
	fz::scoped_lock l(mtx_);

	if (Busy()) {
		return;
	}

	auto& options = engine_context_.GetOptions();
	options.set(OPTION_UPDATECHECK_LASTDATE, std::wstring());
	options.set(OPTION_UPDATECHECK_NEWVERSION, std::wstring());
	options.set(OPTION_UPDATECHECK, 1);
	options.set(OPTION_UPDATECHECK_INTERVAL, 7);

	version_information_ = version_information();
	raw_version_information_.clear();
	local_file_.clear();

	SetState(UpdaterState::idle);
}

#endif
