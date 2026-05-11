#include "echo/storage/Database.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>

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
#endif

}  // namespace

Database::Database() = default;

Database::~Database() {
  Close();
}

#if defined(ECHO_NATIVE_HAS_SQLITE)

void Database::Open(const std::filesystem::path& path) {
  Close();
  path_ = path;
  std::filesystem::create_directories(path.parent_path());
  QuarantineInvalidSqliteFile(path_);
  if (sqlite3_open16(path_.c_str(), &db_) != SQLITE_OK) {
    ThrowSqlite(db_, "sqlite3_open16");
  }
}

void Database::Close() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
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
    InitializeSchema();
  }
}

void Database::InitializeSchema() {
  Execute("PRAGMA journal_mode=WAL;");
  Execute("PRAGMA synchronous=NORMAL;");
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
  Execute("CREATE TABLE IF NOT EXISTS image_cache ("
          "url TEXT PRIMARY KEY,"
          "file_path TEXT NOT NULL,"
          "bytes INTEGER NOT NULL,"
          "last_access_at INTEGER NOT NULL,"
          "created_at INTEGER NOT NULL"
          ");");
  Execute("PRAGMA user_version=1;");
}

void Database::Execute(const std::string& sql) {
  char* error = nullptr;
  if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
    std::string message = error ? error : "unknown sqlite error";
    sqlite3_free(error);
    throw std::runtime_error("sqlite3_exec: " + message);
  }
}

void Database::SetJson(const std::string& key, const nlohmann::json& value) {
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
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "SELECT value FROM kv_store WHERE key=?1 LIMIT 1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    ThrowSqlite(db_, "sqlite3_prepare_v2 GetJson");
  }
  BindText(stmt, 1, key);
  const int rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    auto parsed = nlohmann::json::parse(text ? text : "null", nullptr, false);
    sqlite3_finalize(stmt);
    if (parsed.is_discarded()) return std::nullopt;
    return parsed;
  }
  sqlite3_finalize(stmt);
  return std::nullopt;
}

void Database::PutApiCache(
    const std::string& key,
    const nlohmann::json& value,
    std::int64_t expiresAt) {
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
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT response_json FROM api_cache WHERE cache_key=?1 AND expires_at>?2 LIMIT 1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    ThrowSqlite(db_, "sqlite3_prepare_v2 GetApiCache");
  }
  BindText(stmt, 1, key);
  sqlite3_bind_int64(stmt, 2, now);
  const int rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    auto parsed = nlohmann::json::parse(text ? text : "null", nullptr, false);
    sqlite3_finalize(stmt);
    if (parsed.is_discarded()) return std::nullopt;
    return parsed;
  }
  sqlite3_finalize(stmt);
  return std::nullopt;
}

void Database::PruneExpiredApiCache(std::int64_t now) {
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

void Database::FlushFallback() const {
  if (path_.empty()) return;
  std::ofstream file(path_, std::ios::trunc);
  file << fallback_.dump(2);
}

void Database::SetJson(const std::string& key, const nlohmann::json& value) {
  fallback_["kv_store"][key] = {{"value", value}, {"updated_at", NowSeconds()}};
  FlushFallback();
}

std::optional<nlohmann::json> Database::GetJson(const std::string& key) const {
  const auto& store = fallback_.at("kv_store");
  if (!store.contains(key)) return std::nullopt;
  return store.at(key).value("value", nlohmann::json{});
}

void Database::PutApiCache(
    const std::string& key,
    const nlohmann::json& value,
    std::int64_t expiresAt) {
  fallback_["api_cache"][key] = {
      {"response_json", value},
      {"expires_at", expiresAt},
      {"created_at", NowSeconds()},
  };
  FlushFallback();
}

std::optional<nlohmann::json> Database::GetApiCache(
    const std::string& key,
    std::int64_t now) const {
  const auto& cache = fallback_.at("api_cache");
  if (!cache.contains(key)) return std::nullopt;
  const auto& record = cache.at(key);
  if (record.value("expires_at", 0LL) <= now) return std::nullopt;
  return record.value("response_json", nlohmann::json{});
}

void Database::PruneExpiredApiCache(std::int64_t now) {
  auto& cache = fallback_["api_cache"];
  for (auto it = cache.begin(); it != cache.end();) {
    if (it.value().value("expires_at", 0LL) <= now) {
      it = cache.erase(it);
    } else {
      ++it;
    }
  }
  FlushFallback();
}

#endif

}  // namespace echo::storage
