#include "events.hpp"

#include <libfilezilla/file.hpp>
#include <libfilezilla/format.hpp>
#include <libfilezilla/mutex.hpp>

#define UPLINK_DISABLE_NAMESPACE_COMPAT
typedef bool _Bool;
#include <uplink/uplink.h>

#include <memory>

#ifndef FZ_WINDOWS
#include <sys/mman.h>
#endif

namespace {

fz::mutex output_mutex;

void fzprintf(storjEvent event)
{
	fz::scoped_lock l(output_mutex);

	fputc('0' + static_cast<int>(event), stdout);

	fflush(stdout);
}

template<typename ...Args>
void fzprintf(storjEvent event, Args &&... args)
{
	fz::scoped_lock l(output_mutex);

	fputc('0' + static_cast<int>(event), stdout);

	std::string s = fz::sprintf(std::forward<Args>(args)...);
	fwrite(s.c_str(), s.size(), 1, stdout);

	fputc('\n', stdout);
	fflush(stdout);
}

void print_error(std::string_view fmt, UplinkError const* err)
{
	std::string_view msg(err->message, fz::strlen(err->message));
	auto lines = fz::strtok_view(msg, "\r\n");

	fzprintf(storjEvent::Error, fmt, lines.empty() ? std::string_view() : lines.front());

	for (size_t i = 1; i < lines.size(); ++i) {
		fz::trim(lines[i]);
		fzprintf(storjEvent::Verbose, "%s", lines[i]);
	}
}

bool getLine(std::string & line)
{
	line.clear();
	while (true) {
		int c = fgetc(stdin);
		if (c == -1) {
			return false;
		}
		else if (!c) {
			return line.empty();
		}
		else if (c == '\n') {
			return !line.empty();
		}
		else if (c == '\r') {
			continue;
		}
		else {
			line += static_cast<unsigned char>(c);
		}
	}
}

std::string next_argument(std::string & line)
{
	std::string ret;

	fz::trim(line);

	if (line[0] == '"') {
		size_t pos = 1;
		size_t pos2;
		while ((pos2 = line.find('"', pos)) != std::string::npos && line[pos2 + 1] == '"') {
			ret += line.substr(pos, pos2 - pos + 1);
			pos = pos2 + 2;
		}
		if (pos2 == std::string::npos || (line[pos2 + 1] != ' ' && line[pos2 + 1] != '\0')) {
			line.clear();
			ret.clear();
		}
		ret += line.substr(pos, pos2 - pos);
		line = line.substr(pos2 + 1);
	}
	else {
		size_t pos = line.find(' ');
		if (pos == std::string::npos) {
			ret = line;
			line.clear();
		}
		else {
			ret = line.substr(0, pos);
			line = line.substr(pos + 1);
		}
	}

	fz::trim(line);

	return ret;
}

void listBuckets(UplinkProject *project)
{
	UplinkBucketIterator *it = uplink_list_buckets(project, nullptr);

	while (uplink_bucket_iterator_next(it)) {
		UplinkBucket *bucket = uplink_bucket_iterator_item(it);
		std::string name = bucket->name;
		fz::replace_substrings(name, "\r", "");
		fz::replace_substrings(name, "\n", "");
		fzprintf(storjEvent::Listentry, "%s\n-1\n%d", name, bucket->created);
		uplink_free_bucket(bucket);
	}
	UplinkError *err = uplink_bucket_iterator_err(it);
	if (err) {
		print_error("bucket listing failed: %s", err);
		uplink_free_error(err);
		uplink_free_bucket_iterator(it);
		return;
	}

	uplink_free_bucket_iterator(it);

	fzprintf(storjEvent::Done);
}

void listObjects(UplinkProject *project, std::string const& bucket, std::string const& prefix)
{
	if (!prefix.empty() && prefix.back() != '/') {
		listObjects(project, bucket, prefix + '/');
	}

	UplinkListObjectsOptions options = {
		prefix.c_str(),
		"",
		false,
		true,
		true,
	};

	UplinkObjectIterator *it = uplink_list_objects(project, bucket.c_str(), &options);

	while (uplink_object_iterator_next(it)) {
		UplinkObject *object = uplink_object_iterator_item(it);

		std::string objectName = object->key;
		if (!prefix.empty()) {
			size_t pos = objectName.find(prefix);
			if (pos != 0) {
				continue;
			}
			objectName = objectName.substr(prefix.size());
		}
		if (objectName != ".") {
			fzprintf(storjEvent::Listentry, "%s\n%d\n%d", objectName, object->system.content_length, objectName, object->system.created);
		}

		uplink_free_object(object);
	}

	UplinkError *err = uplink_object_iterator_err(it);
	if (err) {
		print_error("object listing failed: %s", err);
		uplink_free_error(err);
		uplink_free_object_iterator(it);
		return;
	}
	uplink_free_object_iterator(it);

	fzprintf(storjEvent::Done);
}

bool close_and_free_download(UplinkDownloadResult& download_result, bool silent)
{
	UplinkError *close_error = uplink_close_download(download_result.download);
	if (close_error) {
		if (!silent) {
			print_error("download failed to close: %s", close_error);
		}
		uplink_free_error(close_error);
		uplink_free_download_result(download_result);
		return false;
	}

	uplink_free_download_result(download_result);

	return true;
}

void downloadObject(UplinkProject *project, std::string bucket, std::string const& id, uint8_t * const memory)
{
	UplinkDownloadResult download_result = uplink_download_object(project, bucket.c_str(), id.c_str(), nullptr);
	if (download_result.error) {
		print_error("download starting failed: %s", download_result.error);
		uplink_free_download_result(download_result);
		return;
	}

	UplinkDownload *download = download_result.download;

	size_t capacity{};
	size_t written{};
	uint8_t* buffer{};

	while (true) {
		if (written == capacity) {
			fzprintf(storjEvent::io_nextbuf, "%u", written);
			std::string line;
			if (!getLine(line) || line.empty() || line[0] != '-' || line[1] == '-') {
				fzprintf(storjEvent::Error, "Could not get next buffer");
				close_and_free_download(download_result, true);
				return;
			}
			line = line.substr(1);
			buffer = memory + fz::to_integral<uintptr_t>(next_argument(line));
			capacity = fz::to_integral<size_t>(next_argument(line));
			written = 0;
		}

		UplinkReadResult result = uplink_download_read(download, buffer + written, capacity - written);

		if (result.error) {
			if (result.error->code == EOF) {
				uplink_free_read_result(result);
				break;
			}
			print_error("download failed receiving data: %s", result.error);
			uplink_free_read_result(result);
			close_and_free_download(download_result, true);

			return;
		}

		written += result.bytes_read;
		uplink_free_read_result(result);
	}

	if (!close_and_free_download(download_result, false)) {
		return;
	}

	fzprintf(storjEvent::io_finalize, "%d", written);
	std::string line;
	if (!getLine(line) || line.empty() || line[0] != '-' || line[1] != '1') {
		fzprintf(storjEvent::Error, "Could not write to file");
		return;
	}

	fzprintf(storjEvent::Done);
}

void uploadObject(UplinkProject *project, std::string const& bucket, std::string const& key, uint8_t * const memory)
{
	UplinkUploadResult upload_result = uplink_upload_object(project, bucket.c_str(), key.c_str(), nullptr);

	if (upload_result.error) {
		print_error("upload failed: %s", upload_result.error);
		uplink_free_upload_result(upload_result);
		return;
	}

	if (!upload_result.upload->_handle) {
		fzprintf(storjEvent::Error, "Missing upload handle");
		uplink_free_upload_result(upload_result);
		return;
	}

	UplinkUpload *upload = upload_result.upload;

	if (memory) {
		size_t remaining{};
		uint8_t * buffer{};
		while (true) {
			if (!remaining) {
				fzprintf(storjEvent::io_nextbuf, "0");
				std::string line;
				if (!getLine(line) || line.empty() || line[0] != '-' || line[1] == '-') {
					fzprintf(storjEvent::Error, "Could not get next buffer");
					uplink_free_upload_result(upload_result);
					return;
				}
				line = line.substr(1);
				buffer = memory + fz::to_integral<uintptr_t>(next_argument(line));
				remaining = fz::to_integral<size_t>(next_argument(line));
				if (!remaining) {
					break;
				}
			}
			UplinkWriteResult result = uplink_upload_write(upload, buffer, remaining);

			if (result.error) {
				print_error("upload failed: %s", result.error);
				uplink_free_write_result(result);
				uplink_free_upload_result(upload_result);
				return;
			}

			if (!result.bytes_written) {
				fzprintf(storjEvent::Error, "upload_write did not write anything");
				uplink_free_write_result(result);
				uplink_free_upload_result(upload_result);
				return;
			}
			remaining -= result.bytes_written;

			fzprintf(storjEvent::Transfer, "%u", result.bytes_written);
			uplink_free_write_result(result);
		}
	}

	UplinkError *commit_err = uplink_upload_commit(upload);
	if (commit_err) {
		print_error("finalizing upload failed: %s", commit_err);
		uplink_free_error(commit_err);
		uplink_free_upload_result(upload_result);
		return;
	}

	uplink_free_upload_result(upload_result);

	fzprintf(storjEvent::Done);
}

bool deleteObject(UplinkProject *project, std::string bucketName, std::string objectKey)
{
	bool ret = true;

	UplinkObjectResult object_result = uplink_delete_object(project, bucketName.c_str(), objectKey.c_str());
	if (object_result.error) {
		if (object_result.error->code != UPLINK_ERROR_OBJECT_NOT_FOUND) {
			ret = false;
			print_error("failed to create bucket: %s", object_result.error);
		}
	}
	uplink_free_object_result(object_result);

	return ret;
}

void createBucket(UplinkProject *project, std::string bucketName)
{
	UplinkBucketResult bucket_result = uplink_ensure_bucket(project, bucketName.c_str());
	if (bucket_result.error) {
		print_error("failed to create bucket: %s", bucket_result.error);
		uplink_free_bucket_result(bucket_result);
		return;
	}

	UplinkBucket *bucket = bucket_result.bucket;
	fzprintf(storjEvent::Status, "created bucket %s", bucket->name);
	uplink_free_bucket_result(bucket_result);

	fzprintf(storjEvent::Done);
}

void deleteBucket(UplinkProject *project, std::string bucketName)
{
	UplinkBucketResult bucket_result = uplink_delete_bucket(project, bucketName.c_str());
	if (bucket_result.error) {
		print_error("Failed to remove bucket: %s", bucket_result.error);
		uplink_free_bucket_result(bucket_result);
		return;
	}

	if (bucket_result.bucket) {
		UplinkBucket *bucket = bucket_result.bucket;
		fzprintf(storjEvent::Status, "deleted bucket %s", bucket->name);
	}
	uplink_free_bucket_result(bucket_result);

	fzprintf(storjEvent::Done);
}

std::pair<std::string, std::string> SplitPath(std::string_view path)
{
	std::pair<std::string, std::string> ret;

	if (!path.empty()) {
		size_t pos = path.find('/', 1);
		if (pos != std::string::npos) {
			ret.first = path.substr(1, pos - 1);
			ret.second = path.substr(pos + 1);
		}
		else {
			ret.first = path.substr(1);
		}
	}
	return ret;
}
}

int main()
{
	fzprintf(storjEvent::Reply, "fzStorj started, protocol_version=%d", FZSTORJ_PROTOCOL_VERSION);

	std::string apiKey;
	std::string encryptionPassPhrase;
	std::string satelliteURL;

	UplinkProjectResult project_result{};

	UplinkConfig config = {
		"FileZilla",
		0,
		nullptr
	};

	auto openStorjProject = [&]() -> bool {
		if (project_result.project) {
			return true;
		}
		UplinkAccessResult access_result{};
		if (apiKey.empty()) {
			access_result = uplink_parse_access(encryptionPassPhrase.c_str());
			if (!access_result.error) {
				UplinkStringResult sr = uplink_access_satellite_address(access_result.access);
				if (sr.error) {
					print_error("No satellite URI in access grant: %s", sr.error);
					uplink_free_string_result(sr);
					uplink_free_access_result(access_result);
					return false;
				}

				std::string sat = sr.string;
				uplink_free_string_result(sr);

				size_t pos = sat.rfind('@');
				if (pos != std::string::npos) {
					sat = sat.substr(pos + 1);
				}
				if (sat != satelliteURL) {
					fzprintf(storjEvent::Error, "The passed access grant cannot be used on the the satellite '%s'", satelliteURL);
					uplink_free_access_result(access_result);
					return false;
				}
			}
		}
		else {
			access_result = uplink_config_request_access_with_passphrase(config, satelliteURL.c_str(), apiKey.c_str(), encryptionPassPhrase.c_str());
		}
		if (access_result.error) {
			print_error(apiKey.empty() ? "failed to parse access grant: %s" : "failed to parse API key: %s", access_result.error);
			uplink_free_access_result(access_result);
			return false;
		}

		project_result = uplink_config_open_project(config, access_result.access);

		uplink_free_access_result(access_result);

		if (project_result.error) {
			print_error("failed to open project: %s", project_result.error);
			uplink_free_project_result(project_result);
			project_result = UplinkProjectResult{};
			return false;
		}

		return true;
	};

#if FZ_WINDOWS
	auto get_mapping = [](std::string_view s) { return reinterpret_cast<HANDLE>(fz::to_integral<uintptr_t>(s)); };
	auto unmap = [](uint8_t* p, uint64_t) { UnmapViewOfFile(p); };
#else
	auto get_mapping = [](std::string_view s) { return fz::to_integral<int>(s, -1); };
	auto unmap = [](uint8_t* p, uint64_t s) { munmap(p, s); };
#endif

	int ret = 0;
	while (true) {
		std::string command;
		if (!getLine(command)) {
			ret = 1;
			break;
		}

		if (command.empty()) {
			break;
		}

		std::size_t pos = command.find(' ');
		std::string arg;
		if (pos != std::string::npos) {
			arg = command.substr(pos + 1);
			command = command.substr(0, pos);
		}

		if (command == "host") {
			satelliteURL = next_argument(arg);
			fzprintf(storjEvent::Done);
		}
		else if (command == "key") {
			apiKey = next_argument(arg);
			fzprintf(storjEvent::Done);
		}
		else if (command == "pass") {
			encryptionPassPhrase = next_argument(arg);
			fzprintf(storjEvent::Done);
		}
		else if (command == "validate") {

			bool ret = true;

			std::string type = next_argument(arg);

			std::string out;
			UplinkAccessResult access_result{};
			if (type == "grant") {
				encryptionPassPhrase = next_argument(arg);

				access_result = uplink_parse_access(encryptionPassPhrase.c_str());
				if (access_result.error) {
					print_error("failed to parse access grant: %s", access_result.error);
					ret = false;
				}
				else {
					UplinkStringResult sr = uplink_access_satellite_address(access_result.access);
					if (sr.error) {
						print_error("No satellite URI in access grant: %s", sr.error);
						ret = false;
					}
					else {
						out = sr.string;
					}
					uplink_free_string_result(sr);
				}
			}
			else {
				apiKey = next_argument(arg);
				satelliteURL = next_argument(arg);

				access_result = uplink_config_request_access_with_passphrase(config, satelliteURL.c_str(), apiKey.c_str(), "null");
				if (access_result.error) {
					print_error("failed to parse API key: %s", access_result.error);
					ret = false;
				}
			}
			uplink_free_access_result(access_result);

			if (ret) {
				fzprintf(storjEvent::Done, "%s", out);
			}
		}
		else if (command == "list") {

			auto [bucket, prefix] = SplitPath(next_argument(arg));

			if (bucket.empty()) {
				if (!openStorjProject()) {
					continue;
				}
				listBuckets(project_result.project);
			}
			else {
				if (!prefix.empty() && prefix.back() != '/') {
					prefix += '/';
				}

				if (!openStorjProject()) {
					continue;
				}
				listObjects(project_result.project, bucket, prefix);
			}
		}
		else if (command == "get") {
			auto [bucket, key] = SplitPath(next_argument(arg));
			std::string file = next_argument(arg);

			auto mapping = get_mapping(next_argument(arg));
			uint64_t memory_size = fz::to_integral<uint64_t>(next_argument(arg));
			uint64_t offset = fz::to_integral<uint64_t>(next_argument(arg));

			if (!memory_size || offset) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}

#if FZ_WINDOWS
			uint8_t* memory = reinterpret_cast<uint8_t*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, memory_size));
			CloseHandle(mapping);
#else
			uint8_t* memory = reinterpret_cast<uint8_t*>(mmap(NULL, memory_size, PROT_READ|PROT_WRITE, MAP_SHARED, mapping, 0));
#endif
			if (!memory) {
				fzprintf(storjEvent::Error, "Could not map memory");
				continue;
			}

			if (file.empty() || bucket.empty() || key.empty() || key.back() == '/') {
				unmap(memory, memory_size);
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}

			if (!openStorjProject()) {
				unmap(memory, memory_size);
				continue;
			}

			downloadObject(project_result.project, bucket, key, memory);
			unmap(memory, memory_size);
		}
		else if (command == "put") {
			std::string file = next_argument(arg);
			auto [bucket, key] = SplitPath(next_argument(arg));

			auto mapping = get_mapping(next_argument(arg));
			uint64_t memory_size = fz::to_integral<uint64_t>(next_argument(arg));
			uint64_t offset = fz::to_integral<uint64_t>(next_argument(arg));

			if (!memory_size || offset) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}

#if FZ_WINDOWS
			uint8_t* memory = reinterpret_cast<uint8_t*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, memory_size));
			CloseHandle(mapping);
#else
			uint8_t* memory = reinterpret_cast<uint8_t*>(mmap(NULL, memory_size, PROT_READ|PROT_WRITE, MAP_SHARED, mapping, 0));
#endif
			if (!memory) {
				fzprintf(storjEvent::Error, "Could not map memory");
				continue;
			}

			if (file.empty() || bucket.empty() || key.empty() || key.back() == '/') {
				fzprintf(storjEvent::Error, "Bad arguments");
				unmap(memory, memory_size);
				continue;
			}

			if (!openStorjProject()) {
				unmap(memory, memory_size);
				continue;
			}

			uploadObject(project_result.project, bucket, key, memory);
			unmap(memory, memory_size);
		}
		else if (command == "mkd") {
			auto path = next_argument(arg);
			auto [bucket, key] = SplitPath(path);

			if (bucket.empty() || key.empty()) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}
			if (key.back() != '/') {
				key += '/';
			}

			if (!openStorjProject()) {
				continue;
			}

			uploadObject(project_result.project, bucket, key + '.', nullptr);
		}
		else if (command == "rm") {
			auto [bucket, key] = SplitPath(next_argument(arg));
			if (bucket.empty() || key.empty()) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}

			if (!openStorjProject()) {
				continue;
			}

			if (deleteObject(project_result.project, bucket, key)) {
				fzprintf(storjEvent::Status, "Deleted /%s/%s", bucket, key);
				fzprintf(storjEvent::Done);
			}
		}
		else if (command == "rmd") {
			auto [bucket, key] = SplitPath(next_argument(arg));
			if (bucket.empty() || key.empty()) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}

			if (key.back() != '/') {
				key += '/';
			}

			if (!openStorjProject()) {
				continue;
			}

			if (deleteObject(project_result.project, bucket, key)) {
				if (deleteObject(project_result.project, bucket, key + '.')) {
					fzprintf(storjEvent::Status, "Deleted /%s/%s", bucket, key);
					fzprintf(storjEvent::Done);
				}
			}
		}
		else if (command == "mkbucket") {
			std::string bucketName = next_argument(arg);
			if (bucketName.empty()) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}

			if (!openStorjProject()) {
				continue;
			}
			createBucket(project_result.project, bucketName);
		}
		else if (command == "rmbucket") {
			std::string bucketName = next_argument(arg);
			if (bucketName.empty()) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}

			if (!openStorjProject()) {
				continue;
			}
			deleteBucket(project_result.project, bucketName);
		}
		else {
			fzprintf(storjEvent::Error, "No such command: %s", command);
		}
	}

	return ret;
}
