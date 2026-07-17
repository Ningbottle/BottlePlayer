#include "echo/storage/Database.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace echo::storage {
namespace {

std::int64_t NowSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

#if defined(ECHO_NATIVE_HAS_SQLITE)
void QuarantineInvalidSqliteFile(const std::filesystem::path& path) {
  if (path.empty() || !std::filesystem::exists(path) || std::filesystem::is_directory(path)) {
    return;
  }

  const auto size = std::filesystem::file_size(path);
  if (size == 0) {
    return;
  }

  char header[16] = {};
  {
    std::ifstream file(path, std::ios::binary);
    file.read(header, sizeof(header));
  }

  constexpr char sqliteHeader[16] = {
      'S', 'Q', 'L', 'i', 't', 'e', ' ', 'f', 'o', 'r', 'm', 'a', 't', ' ', '3', '\0'};
  if (std::equal(std::begin(header), std::end(header), std::begin(sqliteHeader))) {
    return;
  }

  const auto invalidPath = path.wstring() + L".invalid-" +
                           std::to_wstring(
                               std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count());
  std::error_code fsError;
  std::filesystem::rename(path, invalidPath, fsError);
  if (fsError) {
    fsError.clear();
    std::filesystem::remove(path, fsError);
  }
  fsError.clear();
  std::filesystem::remove(path.wstring() + L"-wal", fsError);
  fsError.clear();
  std::filesystem::remove(path.wstring() + L"-shm", fsError);
}

void ThrowSqlite(sqlite3* db, const std::string& context) {
  throw std::runtime_error(context + ": " + sqlite3_errmsg(db));
}

void BindText(sqlite3_stmt* stmt, int index, const std::string& value) {
  sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

void BindParams(sqlite3_stmt* stmt, const std::vector<BindValue>& params) {
  for (size_t i = 0; i < params.size(); ++i) {
    const int idx = static_cast<int>(i + 1);
    std::visit(
        [&](const auto& v) {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, std::int64_t>) {
            sqlite3_bind_int64(stmt, idx, v);
          } else if constexpr (std::is_same_v<T, double>) {
            sqlite3_bind_double(stmt, idx, v);
          } else {
            BindText(stmt, idx, v);
          }
        },
        params[i]);
  }
}

// Per-thread read connections keyed by DB path. Closed when the thread exits.
// Tolerate process-exit abandoned workers: never block on write_mutex here.
struct TlsReadConnections {
  std::map<std::wstring, sqlite3*> conns;
  ~TlsReadConnections() {
    for (auto& [_, db] : conns) {
      if (db) {
        sqlite3_close(db);
        db = nullptr;
      }
    }
  }
};

TlsReadConnections& TlsReads() {
  thread_local TlsReadConnections tls;
  return tls;
}
#endif

}  // namespace

Database::Database() = default;

Database::~Database() {
  Close();
}

#if defined(ECHO_NATIVE_HAS_SQLITE)

void Database::ApplyBusyTimeout(sqlite3* db) const {
  if (!db) return;
  char* error = nullptr;
  sqlite3_exec(db, "PRAGMA busy_timeout=5000;", nullptr, nullptr, &error);
  if (error) sqlite3_free(error);
}

sqlite3* Database::WriteDb() {
  return db_;
}

sqlite3* Database::ReadDb() const {
  if (!db_ || path_.empty() || !schema_ready_) {
    // Fall back to write connection under lock if schema not ready / no path.
    return db_;
  }
  auto& tls = TlsReads();
  const auto key = path_.wstring();
  auto it = tls.conns.find(key);
  if (it != tls.conns.end() && it->second) {
    return it->second;
  }

  sqlite3* read = nullptr;
  // Open UTF-16 path for Windows consistency with write connection.
  if (sqlite3_open16(path_.c_str(), &read) != SQLITE_OK) {
    if (read) {
      sqlite3_close(read);
    }
    return db_;  // degrade to write conn (caller may still lock)
  }
  // Read-only reopen: close and open with SQLITE_OPEN_READONLY flags.
  sqlite3_close(read);
  read = nullptr;
  const std::string utf8 = path_.string();
  if (sqlite3_open_v2(utf8.c_str(), &read, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    if (read) sqlite3_close(read);
    return db_;
  }
  ApplyBusyTimeout(read);
  tls.conns[key] = read;
  return read;
}

void Database::Open(const std::filesystem::path& path) {
  Close();
  path_ = path;
  schema_ready_ = false;
  std::filesystem::create_directories(path.parent_path());
  QuarantineInvalidSqliteFile(path_);
  if (sqlite3_open16(path_.c_str(), &db_) != SQLITE_OK) {
    ThrowSqlite(db_, "sqlite3_open16");
  }
  ApplyBusyTimeout(db_);
}

void Database::Close() {
  // Close this thread's RO handle for path_ so the file can be deleted in tests.
  // Other threads' TLS handles are left alone (abandoned-worker safe: they close on thread exit).
  if (!path_.empty()) {
    auto& tls = TlsReads();
    const auto key = path_.wstring();
    auto it = tls.conns.find(key);
    if (it != tls.conns.end()) {
      if (it->second) {
        sqlite3_close(it->second);
        it->second = nullptr;
      }
      tls.conns.erase(it);
    }
  }
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
  schema_ready_ = false;
}

void Database::Initialize() {
  try {
    InitializeSchema();
  } catch (const std::runtime_error& error) {
    const std::string message = error.what();
    if (path_.empty() || message.find("file is not a database") == std::string::npos) {
      throw;
    }

    Close();
    const auto invalidPath = path_.wstring() + L".invalid-" +
                             std::to_wstring(
                                 std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
    std::error_code fsError;
    std::filesystem::rename(path_, invalidPath, fsError);
    if (fsError) {
      std::filesystem::remove(path_, fsError);
    }
    std::filesystem::remove(path_.wstring() + L"-wal", fsError);
    std::filesystem::remove(path_.wstring() + L"-shm", fsError);

    if (sqlite3_open16(path_.c_str(), &db_) != SQLITE_OK) {
      ThrowSqlite(db_, "sqlite3_open16 recover");
    }
    ApplyBusyTimeout(db_);
    InitializeSchema();
  }
}

void Database::InitializeSchema() {
  Execute("PRAGMA journal_mode=WAL;");
  Execute("PRAGMA synchronous=NORMAL;");
  Execute("PRAGMA busy_timeout=5000;");
  Execute("CREATE TABLE IF NOT EXISTS kv_store ("
          "key TEXT PRIMARY KEY,"
          "value TEXT NOT NULL,"
          "updated_at INTEGER NOT NULL"
          ");");
  Execute("CREATE TABLE IF NOT EXISTS api_cache ("
          "cache_key TEXT PRIMARY KEY,"
          "response_json TEXT NOT NULL,"
          "expires_at INTEGER NOT NULL,"
          "created_at INTEGER NOT NULL"
          ");");
  Execute("CREATE TABLE IF NOT EXISTS play_history ("
          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "mix_song_id TEXT NOT NULL,"
          "played_at INTEGER NOT NULL,"
          "progress_seconds INTEGER NOT NULL DEFAULT 0"
          ");");
  Execute("CREATE TABLE IF NOT EXISTS play_history_v2 ("
          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "song_hash TEXT NOT NULL,"
          "song_name TEXT NOT NULL,"
          "singer_name TEXT,"
          "album_id TEXT,"
          "album_name TEXT,"
          "cover_url TEXT,"
          "duration_seconds REAL NOT NULL DEFAULT 0,"
          "completed INTEGER NOT NULL DEFAULT 0,"
          "listened_seconds REAL NOT NULL DEFAULT 0,"
          "quality TEXT,"
          "played_at INTEGER NOT NULL"
          ");");
  Execute("CREATE INDEX IF NOT EXISTS idx_ph2_played_at ON play_history_v2(played_at DESC);");
  Execute("CREATE INDEX IF NOT EXISTS idx_ph2_song_hash ON play_history_v2(song_hash);");
  Execute("CREATE INDEX IF NOT EXISTS idx_ph2_singer ON play_history_v2(singer_name);");
  Execute("CREATE TABLE IF NOT EXISTS image_cache ("
          "url TEXT PRIMARY KEY,"
          "file_path TEXT NOT NULL,"
          "bytes INTEGER NOT NULL,"
          "last_access_at INTEGER NOT NULL,"
          "created_at INTEGER NOT NULL"
          ");");
  Execute("PRAGMA user_version=1;");
  Execute("CREATE INDEX IF NOT EXISTS idx_api_cache_expires ON api_cache(expires_at);");
  schema_ready_ = true;
}

void Database::Execute(const std::string& sql) {
  std::lock_guard<std::mutex> guard(write_mutex_);
  char* error = nullptr;
  if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
    std::string message = error ? error : "unknown sqlite error";
    sqlite3_free(error);
    throw std::runtime_error("sqlite3_exec: " + message);
  }
}

void Database::ExecuteBound(const std::string& sql, const std::vector<BindValue>& params) {
  std::lock_guard<std::mutex> guard(write_mutex_);
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    ThrowSqlite(db_, "sqlite3_prepare_v2 ExecuteBound");
  }
  BindParams(stmt, params);
  const int rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    ThrowSqlite(db_, "sqlite3_step ExecuteBound");
  }
  sqlite3_finalize(stmt);
}

std::vector<std::vector<std::string>> Database::ExecuteQuery(const std::string& sql) const {
  return ExecuteQueryBound(sql, {});
}

std::vector<std::vector<std::string>> Database::ExecuteQueryBound(
    const std::string& sql, const std::vector<BindValue>& params) const {
  std::vector<std::vector<std::string>> rows;
  // Prefer thread_local RO connection; no write_mutex for WAL concurrent reads.
  sqlite3* conn = ReadDb();
  if (!conn) return rows;

  // If we fell back to the write connection, serialize.
  const bool useWriteLock = (conn == db_);
  std::unique_lock<std::mutex> guard(write_mutex_, std::defer_lock);
  if (useWriteLock) guard.lock();

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(conn, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    // Legacy tolerance: prepare failure → empty result.
    return rows;
  }
  BindParams(stmt, params);
  int colCount = sqlite3_column_count(stmt);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    std::vector<std::string> row;
    row.reserve(colCount);
    for (int i = 0; i < colCount; ++i) {
      const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
      row.push_back(val ? val : "");
    }
    rows.push_back(std::move(row));
  }
  sqlite3_finalize(stmt);
  return rows;
}

void Database::SetJson(const std::string& key, const nlohmann::json& value) {
  std::lock_guard<std::mutex> guard(write_mutex_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO kv_store(key, value, updated_at) VALUES(?1, ?2, ?3) "
      "ON CONFLICT(key) DO UPDATE SET value=excluded.value, updated_at=excluded.updated_at;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    ThrowSqlite(db_, "sqlite3_prepare_v2 SetJson");
  }
  const auto payload = value.dump();
  BindText(stmt, 1, key);
  BindText(stmt, 2, payload);
  sqlite3_bind_int64(stmt, 3, NowSeconds());
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    ThrowSqlite(db_, "sqlite3_step SetJson");
  }
  sqlite3_finalize(stmt);
}

std::optional<nlohmann::json> Database::GetJson(const std::string& key) const {
  auto rows = ExecuteQueryBound("SELECT value FROM kv_store WHERE key=?1 LIMIT 1;", {key});
  if (rows.empty() || rows[0].empty()) return std::nullopt;
  auto parsed = nlohmann::json::parse(rows[0][0], nullptr, false);
  if (parsed.is_discarded()) return std::nullopt;
  return parsed;
}

void Database::PutApiCache(
    const std::string& key,
    const nlohmann::json& value,
    std::int64_t expiresAt) {
  std::lock_guard<std::mutex> guard(write_mutex_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO api_cache(cache_key, response_json, expires_at, created_at) "
      "VALUES(?1, ?2, ?3, ?4) "
      "ON CONFLICT(cache_key) DO UPDATE SET response_json=excluded.response_json, "
      "expires_at=excluded.expires_at, created_at=excluded.created_at;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    ThrowSqlite(db_, "sqlite3_prepare_v2 PutApiCache");
  }
  const auto payload = value.dump();
  BindText(stmt, 1, key);
  BindText(stmt, 2, payload);
  sqlite3_bind_int64(stmt, 3, expiresAt);
  sqlite3_bind_int64(stmt, 4, NowSeconds());
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    ThrowSqlite(db_, "sqlite3_step PutApiCache");
  }
  sqlite3_finalize(stmt);
}

std::optional<nlohmann::json> Database::GetApiCache(
    const std::string& key,
    std::int64_t now) const {
  auto rows = ExecuteQueryBound(
      "SELECT response_json FROM api_cache WHERE cache_key=?1 AND expires_at>?2 LIMIT 1;",
      {key, now});
  if (rows.empty() || rows[0].empty()) return std::nullopt;
  auto parsed = nlohmann::json::parse(rows[0][0], nullptr, false);
  if (parsed.is_discarded()) return std::nullopt;
  return parsed;
}

void Database::PruneExpiredApiCache(std::int64_t now) {
  std::lock_guard<std::mutex> guard(write_mutex_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "DELETE FROM api_cache WHERE expires_at<=?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    ThrowSqlite(db_, "sqlite3_prepare_v2 PruneExpiredApiCache");
  }
  sqlite3_bind_int64(stmt, 1, now);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    ThrowSqlite(db_, "sqlite3_step PruneExpiredApiCache");
  }
  sqlite3_finalize(stmt);
}

#else

void Database::Open(const std::filesystem::path& path) {
  path_ = path;
  std::filesystem::create_directories(path.parent_path());
  fallback_ = nlohmann::json{{"kv_store", nlohmann::json::object()}, {"api_cache", nlohmann::json::object()}};
  if (std::filesystem::exists(path_)) {
    std::ifstream file(path_);
    auto parsed = nlohmann::json::parse(file, nullptr, false);
    if (!parsed.is_discarded() && parsed.is_object()) {
      fallback_ = std::move(parsed);
      if (!fallback_.contains("kv_store")) fallback_["kv_store"] = nlohmann::json::object();
      if (!fallback_.contains("api_cache")) fallback_["api_cache"] = nlohmann::json::object();
    }
  }
}

void Database::Close() {
  if (!path_.empty()) FlushFallback();
}

void Database::Initialize() {
  FlushFallback();
}

void Database::Execute(const std::string& sql) {
  (void)sql;
}

void Database::ExecuteBound(const std::string& sql, const std::vector<BindValue>& params) {
  (void)sql;
  (void)params;
}

std::vector<std::vector<std::string>> Database::ExecuteQuery(const std::string& sql) const {
  (void)sql;
  return {};
}

std::vector<std::vector<std::string>> Database::ExecuteQueryBound(
    const std::string& sql, const std::vector<BindValue>& params) const {
  (void)sql;
  (void)params;
  return {};
}

void Database::FlushFallback() const {
  if (path_.empty()) return;
  std::ofstream file(path_, std::ios::trunc);
  file << fallback_.dump(2);
}

void Database::SetJson(const std::string& key, const nlohmann::json& value) {
  std::lock_guard<std::mutex> guard(write_mutex_);
  fallback_["kv_store"][key] = {{"value", value}, {"updated_at", NowSeconds()}};
  FlushFallback();
}

std::optional<nlohmann::json> Database::GetJson(const std::string& key) const {
  std::lock_guard<std::mutex> guard(write_mutex_);
  const auto& store = fallback_.at("kv_store");
  if (!store.contains(key)) return std::nullopt;
  return store.at(key).value("value", nlohmann::json{});
}

void Database::PutApiCache(
    const std::string& key,
    const nlohmann::json& value,
    std::int64_t expiresAt) {
  std::lock_guard<std::mutex> guard(write_mutex_);
  fallback_["api_cache"][key] = {
      {"response_json", value},
      {"expires_at", expiresAt},
      {"created_at", NowSeconds()}};
  FlushFallback();
}

std::optional<nlohmann::json> Database::GetApiCache(
    const std::string& key,
    std::int64_t now) const {
  std::lock_guard<std::mutex> guard(write_mutex_);
  const auto& store = fallback_.at("api_cache");
  if (!store.contains(key)) return std::nullopt;
  const auto& entry = store.at(key);
  if (entry.value("expires_at", 0LL) <= now) return std::nullopt;
  return entry.value("response_json", nlohmann::json{});
}

void Database::PruneExpiredApiCache(std::int64_t now) {
  std::lock_guard<std::mutex> guard(write_mutex_);
  auto& store = fallback_["api_cache"];
  std::vector<std::string> expired;
  for (auto it = store.begin(); it != store.end(); ++it) {
    if (it.value().value("expires_at", 0LL) <= now) expired.push_back(it.key());
  }
  for (const auto& k : expired) store.erase(k);
  FlushFallback();
}

#endif

}  // namespace echo::storage
