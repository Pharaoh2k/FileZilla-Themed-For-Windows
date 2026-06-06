#include "filezilla.h"
#include "queue_storage.h"
#include "Options.h"
#include "queue.h"

#include <sqlite3.h>

#include <unordered_map>

#include <libfilezilla/uri.hpp>

#include "../commonui/protect.h"

#define INVALID_DATA -1

enum class Column_type
{
	text,
	integer,
	blob
};

enum _column_flags
{
	not_null = 0x1,
	default_null = 0x2,
	autoincrement = 0x4
};

struct _column
{
	char const* const name;
	Column_type type;
	unsigned int flags;
};

namespace server_table_column_names
{
	enum type
	{
		id,
		host,
		port,
		user,
		password,
		account,
		keyfile,
		protocol,
		type,
		logontype,
		timezone_offset,
		transfer_mode,
		max_connections,
		encoding,
		bypass_proxy,
		post_login_commands,
		name,
		parameters,
		site_path
	};
}

_column server_table_columns[] = {
	{ "id", Column_type::integer, not_null | autoincrement },
	{ "host", Column_type::text, not_null },
	{ "port", Column_type::integer, 0 },
	{ "user", Column_type::text, 0 },
	{ "password", Column_type::text, 0 },
	{ "account", Column_type::text, 0 },
	{ "keyfile", Column_type::text, 0 },
	{ "protocol", Column_type::integer, 0 },
	{ "type", Column_type::integer, 0 },
	{ "logontype", Column_type::integer, 0 },
	{ "timezone_offset", Column_type::integer, 0 },
	{ "transfer_mode", Column_type::text, 0 },
	{ "max_connections", Column_type::integer, 0 },
	{ "encoding", Column_type::text, 0 },
	{ "bypass_proxy", Column_type::integer, 0 },
	{ "post_login_commands", Column_type::text, 0 },
	{ "name", Column_type::text, 0 },
	{ "parameters", Column_type::text, 0 },
	{ "site_path", Column_type::text, default_null }
};

namespace file_table_column_names
{
	enum type
	{
		id,
		server,
		source_file,
		target_file,
		local_path,
		remote_path,
		size,
		error_count,
		priority,
		flags,
		default_exists_action,
		extra_flags,
		persistent_state
	};
}

_column file_table_columns[] = {
	{ "id", Column_type::integer, not_null | autoincrement },
	{ "server", Column_type::integer, not_null },
	{ "source_file", Column_type::text, 0 },
	{ "target_file", Column_type::text, 0 },
	{ "local_path", Column_type::integer, 0 },
	{ "remote_path", Column_type::integer, 0 },
	{ "size", Column_type::integer, 0 },
	{ "error_count", Column_type::integer, 0 },
	{ "priority", Column_type::integer, 0 },
	{ "flags", Column_type::integer, 0 },
	{ "default_exists_action", Column_type::integer, 0 },
	{ "extra_flags", Column_type::text, 0 },
	{ "persistent_state", Column_type::blob, 0 }
};

namespace path_table_column_names
{
	enum type
	{
		id,
		path
	};
}

_column path_table_columns[] = {
	{ "id", Column_type::integer, not_null | autoincrement },
	{ "path", Column_type::text, not_null }
};

class CQueueStorage::Impl final
{
public:
	Impl(COptionsBase & options, login_manager & lim)
		: options_(options)
		, login_manager_(lim)
	{}

	void CreateTables();
	std::string CreateColumnDefs(_column const* columns, size_t count);

	bool PrepareStatements();

	sqlite3_stmt* PrepareStatement(std::string const& query);
	sqlite3_stmt* PrepareInsertStatement(std::string const& name, _column const*, unsigned int count);

	bool SaveServer(CServerItem const& item);
	bool SaveFile(CFileItem const& item);
	bool SaveDirectory(CFolderItem const& item);

	int64_t SaveLocalPath(CLocalPath const& path);
	int64_t SaveRemotePath(CServerPath const& path);

	void ReadLocalPaths();
	void ReadRemotePaths();

	CLocalPath const& GetLocalPath(int64_t id) const;
	CServerPath const& GetRemotePath(int64_t id) const;

	bool Bind(sqlite3_stmt* statement, int index, int value);
	bool Bind(sqlite3_stmt* statement, int index, unsigned int value);
	bool Bind(sqlite3_stmt* statement, int index, int64_t value);
	bool Bind(sqlite3_stmt* statement, int index, std::wstring const& value);
	bool Bind(sqlite3_stmt* statement, int index, std::string const& value);
	bool Bind(sqlite3_stmt* statement, int index, const char* const value);
	bool BindBlob(sqlite3_stmt* statement, int index, std::string const& value);
	bool BindNull(sqlite3_stmt* statement, int index);

	std::wstring GetColumnText(sqlite3_stmt* statement, int index);
	std::string GetColumnTextUtf8(sqlite3_stmt* statement, int index);
	std::string GetColumnBlob(sqlite3_stmt* statement, int index);
	int64_t GetColumnInt64(sqlite3_stmt* statement, int index, int64_t def = 0);
	int GetColumnInt(sqlite3_stmt* statement, int index, int def = 0);

	int64_t ParseServerFromRow(Site & site);
	int64_t ParseFileFromRow(CFileItem** pItem);

	bool MigrateSchema();

	bool BeginTransaction();
	bool EndTransaction(bool roolback);

	void Close();

	sqlite3* db_{};

	sqlite3_stmt* insertServerQuery_{};
	sqlite3_stmt* insertFileQuery_{};
	sqlite3_stmt* insertLocalPathQuery_{};
	sqlite3_stmt* insertRemotePathQuery_{};

	sqlite3_stmt* selectServersQuery_{};
	sqlite3_stmt* selectFilesQuery_{};
	sqlite3_stmt* selectLocalPathQuery_{};
	sqlite3_stmt* selectRemotePathQuery_{};

	// Caches to speed up saving and loading
	void ClearCaches();

	std::unordered_map<std::wstring, int64_t> localPaths_;
	std::unordered_map<std::wstring, int64_t> remotePaths_;

	std::map<int64_t, CLocalPath> reverseLocalPaths_;
	std::map<int64_t, CServerPath> reverseRemotePaths_;

	COptionsBase & options_;
	login_manager & login_manager_;
};


void CQueueStorage::Impl::ReadLocalPaths()
{
	if (!selectLocalPathQuery_) {
		return;
	}

	int res;
	do {
		res = sqlite3_step(selectLocalPathQuery_);
		if (res == SQLITE_ROW) {
			int64_t id = GetColumnInt64(selectLocalPathQuery_, path_table_column_names::id);
			std::wstring localPathRaw = GetColumnText(selectLocalPathQuery_, path_table_column_names::path);
			CLocalPath localPath;
			if (id > 0 && !localPathRaw.empty() && localPath.SetPath(localPathRaw)) {
				reverseLocalPaths_[id] = localPath;
			}
		}
	}
	while (res == SQLITE_BUSY || res == SQLITE_ROW);

	sqlite3_reset(selectLocalPathQuery_);
}


void CQueueStorage::Impl::ReadRemotePaths()
{
	if (!selectRemotePathQuery_) {
		return;
	}

	int res;
	do {
		res = sqlite3_step(selectRemotePathQuery_);
		if (res == SQLITE_ROW) {
			int64_t id = GetColumnInt64(selectRemotePathQuery_, path_table_column_names::id);
			std::wstring remotePathRaw = GetColumnText(selectRemotePathQuery_, path_table_column_names::path);
			CServerPath remotePath;
			if (id > 0 && !remotePathRaw.empty() && remotePath.SetSafePath(remotePathRaw)) {
				reverseRemotePaths_[id] = remotePath;
			}
		}
	}
	while (res == SQLITE_BUSY || res == SQLITE_ROW);

	sqlite3_reset(selectRemotePathQuery_);
}


const CLocalPath& CQueueStorage::Impl::GetLocalPath(int64_t id) const
{
	std::map<int64_t, CLocalPath>::const_iterator it = reverseLocalPaths_.find(id);
	if (it != reverseLocalPaths_.end()) {
		return it->second;
	}

	static CLocalPath const empty{};
	return empty;
}


const CServerPath& CQueueStorage::Impl::GetRemotePath(int64_t id) const
{
	std::map<int64_t, CServerPath>::const_iterator it = reverseRemotePaths_.find(id);
	if (it != reverseRemotePaths_.end()) {
		return it->second;
	}

	static CServerPath const empty{};
	return empty;
}


static int int_callback(void* p, int n, char** v, char**)
{
	int* i = static_cast<int*>(p);
	if (!i || !n || !v || !*v) {
		return -1;
	}

	*i = atoi(*v);
	return 0;
}


bool CQueueStorage::Impl::MigrateSchema()
{
	if (!db_) {
		return false;
	}

	if (!BeginTransaction()) {
		Close();
		return false;
	}

	int version = 0;
	bool ret = sqlite3_exec(db_, "PRAGMA user_version", int_callback, &version, 0) == SQLITE_OK;

	if (ret) {
		if (version > 8) {
			ret = false;
		}
		else if (version > 0) {
			// Do the schema changes
			if (ret && version < 2) {
				ret = sqlite3_exec(db_, "ALTER TABLE servers ADD COLUMN keyfile TEXT", 0, 0, 0) == SQLITE_OK;
			}
			if (ret && version < 4) {
				ret = sqlite3_exec(db_, "ALTER TABLE servers ADD COLUMN parameters TEXT", 0, 0, 0) == SQLITE_OK;
			}
			if (ret && version < 5) {
				ret = sqlite3_exec(db_, "ALTER TABLE servers ADD COLUMN site_path TEXT DEFAULT NULL", 0, 0, 0) == SQLITE_OK;
			}
			if (ret && version < 6) {
				std::string query("CREATE TABLE IF NOT EXISTS files2 ");
				query += CreateColumnDefs(file_table_columns, sizeof(file_table_columns) / sizeof(_column));
				ret = sqlite3_exec(db_, query.c_str(), 0, 0, 0) == SQLITE_OK;
				ret &= sqlite3_exec(db_, "CREATE INDEX IF NOT EXISTS server_index ON files2 (server)", 0, 0, 0) == SQLITE_OK;
				query = fz::sprintf(
					"INSERT INTO files2 (id, server, source_file, target_file, local_path, remote_path, size, error_count, priority, default_exists_action, flags) "
					"SELECT id, server, source_file, target_file, local_path, remote_path, size, error_count, priority, default_exists_action, download * %d + ascii_file * %d FROM files"
					, transfer_flags::download, ftp_transfer_flags::ascii);
				ret &= sqlite3_exec(db_, query.c_str(), 0, 0, 0) == SQLITE_OK;
				ret &= sqlite3_exec(db_, "DROP TABLE files", 0, 0, 0) == SQLITE_OK;
				ret &= sqlite3_exec(db_, "ALTER TABLE files2 RENAME TO files", 0, 0, 0) == SQLITE_OK;
			}
			if (ret && version < 7) {
				std::string query("CREATE TABLE IF NOT EXISTS files2 ");
				query += CreateColumnDefs(file_table_columns, sizeof(file_table_columns) / sizeof(_column));
				ret = sqlite3_exec(db_, query.c_str(), 0, 0, 0) == SQLITE_OK;
				ret &= sqlite3_exec(db_, "CREATE INDEX IF NOT EXISTS server_index ON files2 (server)", 0, 0, 0) == SQLITE_OK;
				query =
					"INSERT INTO files2 (id, server, source_file, target_file, local_path, remote_path, size, error_count, priority, default_exists_action, flags) "
					"SELECT id, server, source_file, target_file, local_path, remote_path, size, error_count, priority, default_exists_action, flags FROM files";
				ret &= sqlite3_exec(db_, query.c_str(), 0, 0, 0) == SQLITE_OK;
				ret &= sqlite3_exec(db_, "DROP TABLE files", 0, 0, 0) == SQLITE_OK;
				ret &= sqlite3_exec(db_, "ALTER TABLE files2 RENAME TO files", 0, 0, 0) == SQLITE_OK;
				ret &= sqlite3_exec(db_, "ALTER TABLE files DROP COLUMN persistent_state", 0, 0, 0) == SQLITE_OK;
			}
			if (ret && version < 8) {
				ret = sqlite3_exec(db_, "ALTER TABLE files ADD COLUMN persistent_state BLOB DEFAULT NULL", 0, 0, 0) == SQLITE_OK;
			}
		}
		if (ret && version != 8) {
			ret = sqlite3_exec(db_, "PRAGMA user_version = 8", 0, 0, 0) == SQLITE_OK;
		}
	}

	EndTransaction(!ret);
	if (!ret) {
		Close();
	}

	return ret;
}


void CQueueStorage::Impl::ClearCaches()
{
	localPaths_.clear();
	remotePaths_.clear();
	reverseLocalPaths_.clear();
	reverseRemotePaths_.clear();
}


int64_t CQueueStorage::Impl::SaveLocalPath(CLocalPath const& path)
{
	auto it = localPaths_.find(path.GetPath());
	if (it != localPaths_.end()) {
		return it->second;
	}

	Bind(insertLocalPathQuery_, path_table_column_names::path, path.GetPath());

	int res;
	do {
		res = sqlite3_step(insertLocalPathQuery_);
	} while (res == SQLITE_BUSY);

	sqlite3_reset(insertLocalPathQuery_);

	if (res == SQLITE_DONE) {
		int64_t id = sqlite3_last_insert_rowid(db_);
		localPaths_[path.GetPath()] = id;
		return id;
	}

	return -1;
}


int64_t CQueueStorage::Impl::SaveRemotePath(CServerPath const& path)
{
	std::wstring const& safePath = path.GetSafePath();
	auto it = remotePaths_.find(safePath);
	if (it != remotePaths_.end()) {
		return it->second;
	}

	Bind(insertRemotePathQuery_, path_table_column_names::path, safePath);

	int res;
	do {
		res = sqlite3_step(insertRemotePathQuery_);
	} while (res == SQLITE_BUSY);

	sqlite3_reset(insertRemotePathQuery_);

	if (res == SQLITE_DONE) {
		int64_t id = sqlite3_last_insert_rowid(db_);
		remotePaths_[safePath] = id;
		return id;
	}

	return -1;
}


std::string CQueueStorage::Impl::CreateColumnDefs(_column const* columns, size_t count)
{
	std::string query = "(";
	for (size_t i = 0; i < count; ++i) {
		if (i) {
			query += ", ";
		}
		query += columns[i].name;
		if (columns[i].type == Column_type::integer) {
			query += " INTEGER";
		}
		else if (columns[i].type == Column_type::blob) {
			query += " BLOB";
		}
		else {
			query += " TEXT";
		}

		if (columns[i].flags & autoincrement) {
			query += " PRIMARY KEY AUTOINCREMENT";
		}
		if (columns[i].flags & not_null) {
			query += " NOT NULL";
		}
		if (columns[i].flags & default_null) {
			query += " DEFAULT NULL";
		}
	}
	query += ")";

	return query;
}

void CQueueStorage::Impl::CreateTables()
{
	if (!db_) {
		return;
	}

	{
		std::string query("CREATE TABLE IF NOT EXISTS servers ");
		query += CreateColumnDefs(server_table_columns, sizeof(server_table_columns) / sizeof(_column));

		if (sqlite3_exec(db_, query.c_str(), 0, 0, 0) != SQLITE_OK)
		{
		}
	}
	{
		std::string query("CREATE TABLE IF NOT EXISTS files ");
		query += CreateColumnDefs(file_table_columns, sizeof(file_table_columns) / sizeof(_column));

		if (sqlite3_exec(db_, query.c_str(), 0, 0, 0) != SQLITE_OK)
		{
		}

		query = "CREATE INDEX IF NOT EXISTS server_index ON files (server)";
		if (sqlite3_exec(db_, query.c_str(), 0, 0, 0) != SQLITE_OK)
		{
		}
	}

	{
		std::string query("CREATE TABLE IF NOT EXISTS local_paths ");
		query += CreateColumnDefs(path_table_columns, sizeof(path_table_columns) / sizeof(_column));

		if (sqlite3_exec(db_, query.c_str(), 0, 0, 0) != SQLITE_OK)
		{
		}
	}

	{
		std::string query("CREATE TABLE IF NOT EXISTS remote_paths ");
		query += CreateColumnDefs(path_table_columns, sizeof(path_table_columns) / sizeof(_column));

		if (sqlite3_exec(db_, query.c_str(), 0, 0, 0) != SQLITE_OK)
		{
		}
	}
}

sqlite3_stmt* CQueueStorage::Impl::PrepareInsertStatement(std::string const& name, _column const* columns, unsigned int count)
{
	if (!db_) {
		return 0;
	}

	std::string query = "INSERT INTO " + name + " (";
	for (unsigned int i = 1; i < count; ++i) {
		if (i > 1) {
			query += ", ";
		}
		query += columns[i].name;
	}
	query += ") VALUES (";
	for (unsigned int i = 1; i < count; ++i) {
		if (i > 1) {
			query += ",";
		}
		query += ":";
		query += columns[i].name;
	}

	query += ")";

	return PrepareStatement(query);
}


sqlite3_stmt* CQueueStorage::Impl::PrepareStatement(std::string const& query)
{
	sqlite3_stmt* ret = 0;

	int res;
	do {
		res = sqlite3_prepare_v2(db_, query.c_str(), -1, &ret, 0);
	} while (res == SQLITE_BUSY);

	if (res != SQLITE_OK) {
		ret = 0;
	}

	return ret;
}


bool CQueueStorage::Impl::PrepareStatements()
{
	if (!db_) {
		return false;
	}

	insertServerQuery_ = PrepareInsertStatement("servers", server_table_columns, sizeof(server_table_columns) / sizeof(_column));
	insertFileQuery_ = PrepareInsertStatement("files", file_table_columns, sizeof(file_table_columns) / sizeof(_column));
	insertLocalPathQuery_ = PrepareInsertStatement("local_paths", path_table_columns, sizeof(path_table_columns) / sizeof(_column));
	insertRemotePathQuery_ = PrepareInsertStatement("remote_paths", path_table_columns, sizeof(path_table_columns) / sizeof(_column));
	if (!insertServerQuery_ || !insertFileQuery_ || !insertLocalPathQuery_ || !insertRemotePathQuery_) {
		return false;
	}

	{
		std::string query = "SELECT ";
		for (unsigned int i = 0; i < (sizeof(server_table_columns) / sizeof(_column)); ++i) {
			if (i > 0) {
				query += ", ";
			}
			query += server_table_columns[i].name;
		}

		query += " FROM servers ORDER BY id ASC";

		if (!(selectServersQuery_ = PrepareStatement(query))) {
			return false;
		}
	}

	{
		std::string query = "SELECT ";
		for (unsigned int i = 0; i < (sizeof(file_table_columns) / sizeof(_column)); ++i) {
			if (i > 0) {
				query += ", ";
			}
			query += file_table_columns[i].name;
		}

		query += " FROM files WHERE server=:server ORDER BY id ASC";

		if (!(selectFilesQuery_ = PrepareStatement(query))) {
			return false;
		}
	}

	{
		std::string query = "SELECT id, path FROM local_paths";
		if (!(selectLocalPathQuery_ = PrepareStatement(query))) {
			return false;
		}
	}

	{
		std::string query = "SELECT id, path FROM remote_paths";
		if (!(selectRemotePathQuery_ = PrepareStatement(query))) {
			return false;
		}
	}
	return true;
}


bool CQueueStorage::Impl::Bind(sqlite3_stmt* statement, int index, int value)
{
	return sqlite3_bind_int(statement, index, value) == SQLITE_OK;
}

bool CQueueStorage::Impl::Bind(sqlite3_stmt* statement, int index, unsigned int value)
{
	return sqlite3_bind_int64(statement, index, value) == SQLITE_OK;
}


bool CQueueStorage::Impl::Bind(sqlite3_stmt* statement, int index, int64_t value)
{
	int res = sqlite3_bind_int64(statement, index, value);
	return res == SQLITE_OK;
}

bool CQueueStorage::Impl::Bind(sqlite3_stmt* statement, int index, std::wstring const& value)
{
#ifdef FZ_WINDOWS
	return sqlite3_bind_text16(statement, index, value.c_str(), value.size() * 2, SQLITE_TRANSIENT) == SQLITE_OK;
#else
	std::string utf8 = fz::to_utf8(value);
	bool ret = sqlite3_bind_text(statement, index, utf8.c_str(), utf8.size(), SQLITE_TRANSIENT) == SQLITE_OK;
	return ret;
#endif
}


bool CQueueStorage::Impl::Bind(sqlite3_stmt* statement, int index, std::string const& value)
{
	return sqlite3_bind_text(statement, index, value.c_str(), value.size(), SQLITE_TRANSIENT) == SQLITE_OK;
}

bool CQueueStorage::Impl::Bind(sqlite3_stmt* statement, int index, const char* const value)
{
	return sqlite3_bind_text(statement, index, value, -1, SQLITE_TRANSIENT) == SQLITE_OK;
}


bool CQueueStorage::Impl::BindNull(sqlite3_stmt* statement, int index)
{
	return sqlite3_bind_null(statement, index) == SQLITE_OK;
}

bool CQueueStorage::Impl::BindBlob(sqlite3_stmt* statement, int index, std::string const& value)
{
	return sqlite3_bind_blob(statement, index, value.c_str(), value.size(), SQLITE_TRANSIENT) == SQLITE_OK;
}


bool CQueueStorage::Impl::SaveServer(CServerItem const& item)
{
	bool kiosk_mode = options_.get_int(OPTION_DEFAULT_KIOSKMODE) != 0;

	Site const& site = item.GetSite();

	Bind(insertServerQuery_, server_table_column_names::host, site.server.GetHost());
	Bind(insertServerQuery_, server_table_column_names::port, static_cast<int>(site.server.GetPort()));
	Bind(insertServerQuery_, server_table_column_names::protocol, static_cast<int>(site.server.GetProtocol()));
	Bind(insertServerQuery_, server_table_column_names::type, static_cast<int>(site.server.GetType() + 1));

	ProtectedCredentials credentials = site.credentials;
	protect(credentials, login_manager_, options_);

	LogonType logonType = credentials.logonType_;
	if (logonType != LogonType::anonymous) {
		Bind(insertServerQuery_, server_table_column_names::user, site.server.GetUser());

		if (logonType == LogonType::normal || logonType == LogonType::account || logonType == LogonType::profile) {
			if (kiosk_mode) {
				logonType = LogonType::ask;
				BindNull(insertServerQuery_, server_table_column_names::password);
				BindNull(insertServerQuery_, server_table_column_names::account);
			}
			else {
				std::wstring pw;
				if (credentials.encrypted_) {
					pw = fz::to_wstring_from_utf8(credentials.encrypted_.to_base64());
					pw += ' ';
				}
				pw += credentials.GetPass();
				Bind(insertServerQuery_, server_table_column_names::password, pw);

				if (credentials.account_.empty()) {
					BindNull(insertServerQuery_, server_table_column_names::account);
				}
				else {
					Bind(insertServerQuery_, server_table_column_names::account, credentials.account_);
				}
			}
		}
		else {
			BindNull(insertServerQuery_, server_table_column_names::password);
			BindNull(insertServerQuery_, server_table_column_names::account);
		}

		if (credentials.keyFile_.empty()) {
			BindNull(insertServerQuery_, server_table_column_names::keyfile);
		}
		else {
			Bind(insertServerQuery_, server_table_column_names::keyfile, credentials.keyFile_);
		}
	}
	else {
		BindNull(insertServerQuery_, server_table_column_names::user);
		BindNull(insertServerQuery_, server_table_column_names::password);
		BindNull(insertServerQuery_, server_table_column_names::account);
		BindNull(insertServerQuery_, server_table_column_names::keyfile);
	}

	{
		int64_t lt = static_cast<int>(logonType);
		if (credentials.encrypted_) {
			static_assert(static_cast<int64_t>(LogonType::count) < (1ll << 62), "LogonType::count too big");
			lt |= 1ll << 62;
		}
		Bind(insertServerQuery_, server_table_column_names::logontype, lt);
	}

	Bind(insertServerQuery_, server_table_column_names::timezone_offset, site.server.GetTimezoneOffset());

	switch (site.server.GetPasvMode())
	{
	case MODE_PASSIVE:
		Bind(insertServerQuery_, server_table_column_names::transfer_mode, _T("passive"));
		break;
	case MODE_ACTIVE:
		Bind(insertServerQuery_, server_table_column_names::transfer_mode, _T("active"));
		break;
	default:
		Bind(insertServerQuery_, server_table_column_names::transfer_mode, _T("default"));
		break;
	}
	Bind(insertServerQuery_, server_table_column_names::max_connections, site.connection_limit_);

	switch (site.server.GetEncodingType())
	{
	default:
	case ENCODING_UTF8:
		Bind(insertServerQuery_, server_table_column_names::encoding, _T("UTF-8"));
		break;
	case ENCODING_CUSTOM:
		Bind(insertServerQuery_, server_table_column_names::encoding, site.server.GetCustomEncoding());
		break;
	}

	if (CServer::ProtocolHasFeature(site.server.GetProtocol(), ProtocolFeature::PostLoginCommands)) {
		std::vector<std::wstring> const& postLoginCommands = site.server.GetPostLoginCommands();
		if (!postLoginCommands.empty()) {
			std::wstring commands;
			for (auto const& command : commands) {
				if (!commands.empty()) {
					commands += _T("\n");
				}
				commands += command;
			}
			Bind(insertServerQuery_, server_table_column_names::post_login_commands, commands);
		}
		else {
			BindNull(insertServerQuery_, server_table_column_names::post_login_commands);
		}
	}
	else {
		BindNull(insertServerQuery_, server_table_column_names::post_login_commands);
	}

	Bind(insertServerQuery_, server_table_column_names::bypass_proxy, site.server.GetBypassProxy() ? 1 : 0);
	if (!site.GetName().empty()) {
		Bind(insertServerQuery_, server_table_column_names::name, site.GetName());
	}
	else {
		BindNull(insertServerQuery_, server_table_column_names::name);
	}

	auto const& parameters = site.server.GetExtraParameters();
	if (!parameters.empty()) {
		fz::query_string qs;
		for (auto const& parameter : parameters) {
			qs[parameter.first] = fz::to_utf8(parameter.second);
		}
		Bind(insertServerQuery_, server_table_column_names::parameters, qs.to_string(false));
	}

	auto const& site_path = site.SitePath();
	if (site_path.empty()) {
		BindNull(insertServerQuery_, server_table_column_names::site_path);
	}
	else {
		Bind(insertServerQuery_, server_table_column_names::site_path, site_path);
	}

	int res;
	do {
		res = sqlite3_step(insertServerQuery_);
	} while (res == SQLITE_BUSY);

	sqlite3_reset(insertServerQuery_);

	bool ret = res == SQLITE_DONE;
	if (ret) {
		sqlite3_int64 serverId = sqlite3_last_insert_rowid(db_);
		Bind(insertFileQuery_, file_table_column_names::server, static_cast<int64_t>(serverId));

		const std::vector<CQueueItem*>& children = item.GetChildren();
		for (std::vector<CQueueItem*>::const_iterator it = children.begin() + item.GetRemovedAtFront(); it != children.end(); ++it) {
			CQueueItem & childItem = **it;
			if (childItem.GetType() == QueueItemType::File) {
				ret &= SaveFile(static_cast<CFileItem&>(childItem));
			}
			else if (childItem.GetType() == QueueItemType::Folder) {
				ret &= SaveDirectory(static_cast<CFolderItem&>(childItem));
			}
		}
	}
	return ret;
}


bool CQueueStorage::Impl::SaveFile(CFileItem const& file)
{
	if (file.m_edit != CEditHandler::none) {
		return true;
	}

	Bind(insertFileQuery_, file_table_column_names::source_file, file.GetSourceFile());
	auto const& extra_data = file.GetExtraData();
	if (extra_data) {
		if (!extra_data->targetFile_.empty()) {
			Bind(insertFileQuery_, file_table_column_names::target_file, extra_data->targetFile_);
		}
		else {
			BindNull(insertFileQuery_, file_table_column_names::target_file);
		}

		if (!extra_data->extraFlags_.empty()) {
			Bind(insertFileQuery_, file_table_column_names::extra_flags, extra_data->extraFlags_);
		}
		else {
			BindNull(insertFileQuery_, file_table_column_names::extra_flags);
		}

		if (!extra_data->persistentState_.empty()) {
			Bind(insertFileQuery_, file_table_column_names::persistent_state, extra_data->persistentState_);
		}
		else {
			BindNull(insertFileQuery_, file_table_column_names::persistent_state);
		}
	}
	else {
		BindNull(insertFileQuery_, file_table_column_names::target_file);
		BindNull(insertFileQuery_, file_table_column_names::extra_flags);
		BindNull(insertFileQuery_, file_table_column_names::persistent_state);
	}

	int64_t localPathId = SaveLocalPath(file.GetLocalPath());
	int64_t remotePathId = SaveRemotePath(file.GetRemotePath());
	if (localPathId == -1 || remotePathId == -1) {
		return false;
	}

	Bind(insertFileQuery_, file_table_column_names::local_path, localPathId);
	Bind(insertFileQuery_, file_table_column_names::remote_path, remotePathId);

	if (file.GetSize() != -1) {
		Bind(insertFileQuery_, file_table_column_names::size, file.GetSize());
	}
	else {
		BindNull(insertFileQuery_, file_table_column_names::size);
	}
	if (file.m_errorCount) {
		Bind(insertFileQuery_, file_table_column_names::error_count, file.m_errorCount);
	}
	else {
		BindNull(insertFileQuery_, file_table_column_names::error_count);
	}
	Bind(insertFileQuery_, file_table_column_names::priority, static_cast<int>(file.GetPriority()));
	Bind(insertFileQuery_, file_table_column_names::flags, static_cast<int64_t>(file.flags() - queue_flags::mask));

	if (file.m_defaultFileExistsAction != CFileExistsNotification::unknown) {
		Bind(insertFileQuery_, file_table_column_names::default_exists_action, file.m_defaultFileExistsAction);
	}
	else {
		BindNull(insertFileQuery_, file_table_column_names::default_exists_action);
	}

	int res;
	do {
		res = sqlite3_step(insertFileQuery_);
	} while (res == SQLITE_BUSY);

	sqlite3_reset(insertFileQuery_);

	return res == SQLITE_DONE;
}


bool CQueueStorage::Impl::SaveDirectory(CFolderItem const& directory)
{
	if (directory.Download()) {
		BindNull(insertFileQuery_, file_table_column_names::source_file);
	}
	else {
		Bind(insertFileQuery_, file_table_column_names::source_file, directory.GetSourceFile());
	}
	BindNull(insertFileQuery_, file_table_column_names::target_file);

	int64_t localPathId = directory.Download() ? SaveLocalPath(directory.GetLocalPath()) : -1;
	int64_t remotePathId = directory.Download() ? -1 : SaveRemotePath(directory.GetRemotePath());
	if (localPathId == -1 && remotePathId == -1) {
		return false;
	}

	Bind(insertFileQuery_, file_table_column_names::local_path, localPathId);
	Bind(insertFileQuery_, file_table_column_names::remote_path, remotePathId);

	BindNull(insertFileQuery_, file_table_column_names::size);
	if (directory.m_errorCount) {
		Bind(insertFileQuery_, file_table_column_names::error_count, directory.m_errorCount);
	}
	else {
		BindNull(insertFileQuery_, file_table_column_names::error_count);
	}
	Bind(insertFileQuery_, file_table_column_names::priority, static_cast<int>(directory.GetPriority()));
	Bind(insertFileQuery_, file_table_column_names::flags, static_cast<int>(directory.flags() - queue_flags::mask));

	BindNull(insertFileQuery_, file_table_column_names::default_exists_action);

	int res;
	do {
		res = sqlite3_step(insertFileQuery_);
	} while (res == SQLITE_BUSY);

	sqlite3_reset(insertFileQuery_);

	return res == SQLITE_DONE;
}


std::wstring CQueueStorage::Impl::GetColumnText(sqlite3_stmt* statement, int index)
{
	std::wstring ret;

#ifdef FZ_WINDOWS
	static_assert(sizeof(wchar_t) == 2, "wchar_t not of size 2");
	wchar_t const* text = static_cast<wchar_t const*>(sqlite3_column_text16(statement, index));
	if (text) {
		ret.assign(text, sqlite3_column_bytes16(statement, index) / 2);
	}
#else
	char const* text = reinterpret_cast<char const*>(sqlite3_column_text(statement, index));
	if (text) {
		int len = sqlite3_column_bytes(statement, index);
		std::string utf8(text, len);
		ret = fz::to_wstring_from_utf8(utf8);
	}
#endif

	return ret;
}

std::string CQueueStorage::Impl::GetColumnTextUtf8(sqlite3_stmt* statement, int index)
{
	char const* text = reinterpret_cast<char const*>(sqlite3_column_text(statement, index));
	if (text) {
		int len = sqlite3_column_bytes(statement, index);
		return std::string(text, len);
	}

	return std::string();
}

std::string CQueueStorage::Impl::GetColumnBlob(sqlite3_stmt* statement, int index)
{
	int len = sqlite3_column_bytes(statement, index);
	if (len) {
		char const* text = reinterpret_cast<char const*>(sqlite3_column_blob(statement, index));
		if (text) {
			return std::string(text, len);
		}
	}

	return std::string();
}

int64_t CQueueStorage::Impl::GetColumnInt64(sqlite3_stmt* statement, int index, int64_t def)
{
	if (sqlite3_column_type(statement, index) == SQLITE_NULL) {
		return def;
	}
	else {
		return sqlite3_column_int64(statement, index);
	}
}

int CQueueStorage::Impl::GetColumnInt(sqlite3_stmt* statement, int index, int def)
{
	if (sqlite3_column_type(statement, index) == SQLITE_NULL) {
		return def;
	}
	else {
		return sqlite3_column_int(statement, index);
	}
}

int64_t CQueueStorage::Impl::ParseServerFromRow(Site & site)
{
	site = Site();

	std::wstring host = GetColumnText(selectServersQuery_, server_table_column_names::host);
	if (host.empty()) {
		return INVALID_DATA;
	}

	int port = GetColumnInt(selectServersQuery_, server_table_column_names::port);
	if (port < 1 || port > 65535) {
		return INVALID_DATA;
	}

	if (!site.server.SetHost(host, port)) {
		return INVALID_DATA;
	}

	int const protocol = GetColumnInt(selectServersQuery_, server_table_column_names::protocol);
	if (protocol < 0 || protocol > MAX_VALUE) {
		return INVALID_DATA;
	}
	site.server.SetProtocol(static_cast<ServerProtocol>(protocol));

	if (site.server.HasFeature(ProtocolFeature::ServerType)) {
		int type = GetColumnInt(selectServersQuery_, server_table_column_names::type);
		if (type < 0 || static_cast<unsigned>(type) > SERVERTYPE_MAX) {
			return INVALID_DATA;
		}
		if (type) {
			--type;
		}
		site.server.SetType(static_cast<ServerType>(type));
	}


	int64_t logonType = GetColumnInt64(selectServersQuery_, server_table_column_names::logontype);
	bool const encrypted = logonType & (1ll << 62);
	logonType &= ~(1ll << 62);
	if (logonType < 0 || logonType >= static_cast<int>(LogonType::count)) {
		return INVALID_DATA;
	}

	site.SetLogonType(static_cast<LogonType>(logonType));

	if (site.credentials.logonType_ != LogonType::anonymous) {
		std::wstring user = GetColumnText(selectServersQuery_, server_table_column_names::user);
		std::wstring pass = GetColumnText(selectServersQuery_, server_table_column_names::password);

		site.SetUser(user);
		if (encrypted) {
			size_t pos = pass.find(' ');
			if (pos == std::string::npos) {
				return INVALID_DATA;
			}
			else {
				site.credentials.encrypted_ = fz::public_key::from_base64(fz::to_utf8(pass.substr(0, pos)));
				pass = pass.substr(pos + 1);
			}
		}
		site.credentials.SetPass(pass);

		site.credentials.account_ = GetColumnText(selectServersQuery_, server_table_column_names::account);
		if (site.credentials.account_.empty() && site.credentials.logonType_ == LogonType::account) {
			return INVALID_DATA;
		}

		site.credentials.keyFile_ = GetColumnText(selectServersQuery_, server_table_column_names::keyfile);
		if (site.credentials.keyFile_.empty() && site.credentials.logonType_ == LogonType::key) {
			return INVALID_DATA;
		}
	}

	int timezoneOffset = GetColumnInt(selectServersQuery_, server_table_column_names::timezone_offset);
	if (!site.server.SetTimezoneOffset(timezoneOffset)) {
		return INVALID_DATA;
	}

	std::wstring pasvMode = GetColumnText(selectServersQuery_, server_table_column_names::transfer_mode);
	if (pasvMode == _T("passive")) {
		site.server.SetPasvMode(MODE_PASSIVE);
	}
	else if (pasvMode == _T("active")) {
		site.server.SetPasvMode(MODE_ACTIVE);
	}
	else {
		site.server.SetPasvMode(MODE_DEFAULT);
	}

	int64_t maximumMultipleConnections = GetColumnInt64(selectServersQuery_, server_table_column_names::max_connections);
	if (maximumMultipleConnections < 0) {
		return INVALID_DATA;
	}
	site.connection_limit_ = static_cast<unsigned int>(maximumMultipleConnections);

	std::wstring encodingType = GetColumnText(selectServersQuery_, server_table_column_names::encoding);
	if (encodingType.empty() || encodingType == L"Auto" || encodingType == L"UTF-8") {
		site.server.SetEncodingType(ENCODING_UTF8);
	}
	else {
		if (!site.server.SetEncodingType(ENCODING_CUSTOM, encodingType)) {
			return INVALID_DATA;
		}
	}

	if (CServer::ProtocolHasFeature(site.server.GetProtocol(), ProtocolFeature::PostLoginCommands)) {
		std::wstring const commands = GetColumnText(selectServersQuery_, server_table_column_names::post_login_commands);
		std::vector<std::wstring> postLoginCommands = fz::strtok(commands, '\n');
		if (!site.server.SetPostLoginCommands(postLoginCommands)) {
			return INVALID_DATA;
		}
	}


	site.server.SetBypassProxy(GetColumnInt(selectServersQuery_, server_table_column_names::bypass_proxy) == 1 );
	site.SetName(GetColumnText(selectServersQuery_, server_table_column_names::name).substr(0, 255));

	fz::query_string qs = fz::query_string(GetColumnTextUtf8(selectServersQuery_, server_table_column_names::parameters));
	for (auto const& pair : qs.pairs()) {
		site.server.SetExtraParameter(pair.first, fz::to_wstring_from_utf8(pair.second));
	}

	std::wstring site_path = GetColumnText(selectServersQuery_, server_table_column_names::site_path);
	if (!site_path.empty()) {
		site.SetSitePath(site_path);
	}

	return GetColumnInt64(selectServersQuery_, server_table_column_names::id);
}


int64_t CQueueStorage::Impl::ParseFileFromRow(CFileItem** pItem)
{
	std::wstring sourceFile = GetColumnText(selectFilesQuery_, file_table_column_names::source_file);
	std::wstring targetFile = GetColumnText(selectFilesQuery_, file_table_column_names::target_file);

	int64_t localPathId = GetColumnInt64(selectFilesQuery_, file_table_column_names::local_path, false);
	int64_t remotePathId = GetColumnInt64(selectFilesQuery_, file_table_column_names::remote_path, false);

	CLocalPath const localPath(GetLocalPath(localPathId));
	CServerPath const remotePath(GetRemotePath(remotePathId));

	auto flags = static_cast<transfer_flags>(GetColumnInt(selectFilesQuery_, file_table_column_names::flags));
	bool const download = flags & transfer_flags::download;

	if (localPathId == -1 || remotePathId == -1) {
		// QueueItemType::Folder
		if ((download && localPath.empty()) ||
			(!download && remotePath.empty()))
		{
			return INVALID_DATA;
		}

		if (download) {
			*pItem = new CFolderItem(0, true, localPath);
		}
		else {
			*pItem = new CFolderItem(0, true, remotePath, sourceFile);
		}
	}
	else {
		int64_t size = GetColumnInt64(selectFilesQuery_, file_table_column_names::size);
		unsigned char errorCount = static_cast<unsigned char>(GetColumnInt(selectFilesQuery_, file_table_column_names::error_count));
		int priority = GetColumnInt(selectFilesQuery_, file_table_column_names::priority, static_cast<int>(QueuePriority::normal));

		std::wstring extraFlags = GetColumnText(selectFilesQuery_, file_table_column_names::extra_flags);
		std::string persistentState = GetColumnTextUtf8(selectFilesQuery_, file_table_column_names::persistent_state);

		int overwrite_action = GetColumnInt(selectFilesQuery_, file_table_column_names::default_exists_action, CFileExistsNotification::unknown);

		if (sourceFile.empty() || localPath.empty() ||
			remotePath.empty() ||
			size < -1 ||
			priority < 0 || priority >= static_cast<int>(QueuePriority::count))
		{
			return INVALID_DATA;
		}

		CFileItem* fileItem = new CFileItem(0, flags | queue_flags::queued, std::move(sourceFile), std::move(targetFile), std::move(localPath), std::move(remotePath), size, std::move(extraFlags), std::move(persistentState));
		*pItem = fileItem;
		fileItem->SetPriorityRaw(QueuePriority(priority));
		fileItem->m_errorCount = errorCount;

		if (overwrite_action > 0 && overwrite_action < CFileExistsNotification::ACTION_COUNT) {
			fileItem->m_defaultFileExistsAction = (CFileExistsNotification::OverwriteAction)overwrite_action;
		}
	}

	return GetColumnInt64(selectFilesQuery_, file_table_column_names::id);
}

bool CQueueStorage::Impl::BeginTransaction()
{
	return sqlite3_exec(db_, "BEGIN TRANSACTION", 0, 0, 0) == SQLITE_OK;
}

bool CQueueStorage::Impl::EndTransaction(bool rollback)
{
	if (rollback) {
		return sqlite3_exec(db_, "ROLLBACK", 0, 0, 0) == SQLITE_OK;
	}
	else {
		return sqlite3_exec(db_, "END TRANSACTION", 0, 0, 0) == SQLITE_OK;
	}
}


void CQueueStorage::Impl::Close()
{
	sqlite3_finalize(insertServerQuery_);
	sqlite3_finalize(insertFileQuery_);
	sqlite3_finalize(insertLocalPathQuery_);
	sqlite3_finalize(insertRemotePathQuery_);
	sqlite3_finalize(selectServersQuery_);
	sqlite3_finalize(selectFilesQuery_);
	sqlite3_finalize(selectLocalPathQuery_);
	sqlite3_finalize(selectRemotePathQuery_);
	insertServerQuery_ = 0;
	insertFileQuery_ = 0;
	insertLocalPathQuery_ = 0;
	insertRemotePathQuery_ = 0;
	selectServersQuery_ = 0;
	selectFilesQuery_ = 0;
	selectLocalPathQuery_ = 0;
	selectRemotePathQuery_ = 0;
	sqlite3_close(db_);
	db_ = 0;
}

CQueueStorage::CQueueStorage(COptionsBase& options, login_manager & lim)
: d_(new Impl(options, lim))
{
	int ret = sqlite3_open(fz::to_utf8(GetDatabaseFilename()).c_str(), &d_->db_ );
	if (ret != SQLITE_OK) {
		d_->db_ = 0;
	}

	if (sqlite3_exec(d_->db_, "PRAGMA encoding=\"UTF-16le\"", 0, 0, 0) == SQLITE_OK) {
		d_->MigrateSchema();
		d_->CreateTables();
		d_->PrepareStatements();
	}
}

CQueueStorage::~CQueueStorage()
{
	d_->Close();
	delete d_;
}

bool CQueueStorage::SaveQueue(std::vector<CServerItem*> const& queue)
{
	d_->ClearCaches();

	bool ret = true;
	if (sqlite3_exec(d_->db_, "BEGIN TRANSACTION", 0, 0, 0) == SQLITE_OK) {
		for (auto const& serverItem : queue) {
			ret &= d_->SaveServer(*serverItem);
		}

		// Even on previous failure, we want to at least try to commit the data we have so far
		ret &= sqlite3_exec(d_->db_, "END TRANSACTION", 0, 0, 0) == SQLITE_OK;

		d_->ClearCaches();
	}
	else {
		ret = false;
	}

	return ret;
}

int64_t CQueueStorage::GetServer(Site& site, bool fromBeginning)
{
	int64_t ret = -1;

	if (d_->selectServersQuery_) {
		if (fromBeginning) {
			d_->ReadLocalPaths();
			d_->ReadRemotePaths();
			sqlite3_reset(d_->selectServersQuery_);
		}

		for (;;) {
			int res;
			do {
				res = sqlite3_step(d_->selectServersQuery_);
			}
			while (res == SQLITE_BUSY);

			if (res == SQLITE_ROW) {
				ret = d_->ParseServerFromRow(site);
				if (ret > 0) {
					break;
				}
			}
			else if (res == SQLITE_DONE) {
				ret = 0;
				sqlite3_reset(d_->selectServersQuery_);
				break;
			}
			else {
				ret = -1;
				sqlite3_reset(d_->selectServersQuery_);
				break;
			}
		}
	}
	else {
		ret = -1;
	}

	return ret;
}


int64_t CQueueStorage::GetFile(CFileItem** pItem, int64_t server)
{
	int64_t ret = -1;
	*pItem = 0;

	if (d_->selectFilesQuery_) {
		if (server > 0) {
			sqlite3_reset(d_->selectFilesQuery_);
			sqlite3_bind_int64(d_->selectFilesQuery_, 1, server);
		}

		for (;;) {
			int res;
			do {
				res = sqlite3_step(d_->selectFilesQuery_);
			}
			while (res == SQLITE_BUSY);

			if (res == SQLITE_ROW) {
				ret = d_->ParseFileFromRow(pItem);
				if (ret > 0) {
					break;
				}
			}
			else if (res == SQLITE_DONE) {
				ret = 0;
				sqlite3_reset(d_->selectFilesQuery_);
				break;
			}
			else {
				ret = -1;
				sqlite3_reset(d_->selectFilesQuery_);
				break;
			}
		}
	}
	else {
		ret = -1;
	}

	return ret;
}

bool CQueueStorage::Clear()
{
	if (!d_->db_) {
		return false;
	}

	if (sqlite3_exec(d_->db_, "DELETE FROM files", 0, 0, 0) != SQLITE_OK) {
		return false;
	}

	if (sqlite3_exec(d_->db_, "DELETE FROM servers", 0, 0, 0) != SQLITE_OK) {
		return false;
	}

	if (sqlite3_exec(d_->db_, "DELETE FROM local_paths", 0, 0, 0) != SQLITE_OK) {
		return false;
	}

	if (sqlite3_exec(d_->db_, "DELETE FROM remote_paths", 0, 0, 0) != SQLITE_OK) {
		return false;
	}

	d_->ClearCaches();

	return true;
}

std::wstring CQueueStorage::GetDatabaseFilename()
{
	return d_->options_.get_string(OPTION_DEFAULT_SETTINGSDIR) + L"queue.sqlite3";
}

bool CQueueStorage::BeginTransaction()
{
	return d_->BeginTransaction();
}

bool CQueueStorage::EndTransaction(bool rollback)
{
	return d_->EndTransaction(rollback);
}

bool CQueueStorage::Vacuum()
{
	return sqlite3_exec(d_->db_, "VACUUM", 0, 0, 0) == SQLITE_OK;
}
