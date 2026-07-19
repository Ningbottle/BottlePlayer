#include "echo/storage/Database.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

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
#endif

}  // namespace

Database::Database() = default;

Database::~Database() {
  Close();
}

void Database::StartActor() {
  std::promise<void> started;
  std::future<void> started_fut;
  bool created = false;

  {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    if (state_ == ActorState::Open) {
      return;
    }
    if (state_ == ActorState::Starting) {
      queue_cv_.wait(lock, [this] {
        return state_ == ActorState::Open || state_ == ActorState::Failed ||
               state_ == ActorState::Closed;
      });
      if (state_ != ActorState::Open) {
        throw std::runtime_error("database_not_accepting");
      }
      return;
    }
    if (state_ == ActorState::Closing) {
      // Wait for in-flight Close to finish (state becomes Closed).
      queue_cv_.wait(lock, [this] { return state_ == ActorState::Closed; });
    }
    // Closed or Failed (Failed without a live thread).
    if (state_ == ActorState::Failed) {
      if (actor_.joinable()) {
        lock.unlock();
        actor_.join();
        lock.lock();
      }
      state_ = ActorState::Closed;
      actor_tid_ = {};
    }

    state_ = ActorState::Starting;
    started_fut = started.get_future();
    try {
      // promise set after actor_tid_ + Open; StartActor waits before returning.
      actor_ = std::thread([this, p = std::move(started)]() mutable {
        {
          std::lock_guard<std::mutex> lk(queue_mutex_);
          actor_tid_ = std::this_thread::get_id();
        }
        queue_cv_.notify_all();
        try {
          p.set_value();
        } catch (...) {
          // already satisfied
        }
        ActorLoop();
      });
      created = true;
    } catch (...) {
      state_ = ActorState::Failed;
      actor_tid_ = {};
      queue_cv_.notify_all();
      throw;
    }
  }

  if (created) {
    started_fut.get();
  }
}

void Database::ActorLoop() {
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(lock, [this] {
        return !task_queue_.empty() || state_ == ActorState::Closing ||
               state_ == ActorState::Failed;
      });
      if (task_queue_.empty()) {
        // Closing/Failed and fully drained — exit actor thread.
        return;
      }
      task = std::move(task_queue_.front());
      task_queue_.pop();
    }
    task();
  }
}

void Database::Close() {
  std::shared_ptr<std::promise<void>> done;
  bool peer_closing = false;

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (state_ == ActorState::Closed) {
      return;
    }
    if (state_ == ActorState::Closing) {
      // Another Close owns shutdown; wait for Closed below (no double-join).
      peer_closing = true;
    } else if (state_ == ActorState::Failed) {
      // No accepting work; join any stray thread and mark Closed below.
    } else if (state_ == ActorState::Starting || state_ == ActorState::Open) {
      // Same lock: switch to Closing before enqueue so Submit cannot race past.
      state_ = ActorState::Closing;
      done = std::make_shared<std::promise<void>>();
      auto fut_holder = done;
      task_queue_.emplace([this, fut_holder] {
        try {
          CloseLocked();
          fut_holder->set_value();
        } catch (...) {
          try {
            fut_holder->set_exception(std::current_exception());
          } catch (...) {
          }
        }
      });
    }
  }
  queue_cv_.notify_all();

  if (peer_closing) {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    queue_cv_.wait(lock, [this] { return state_ == ActorState::Closed; });
    return;
  }

  if (done) {
    try {
      done->get_future().get();
    } catch (...) {
      // Still complete shutdown even if CloseLocked threw.
    }
  }

  // Ensure loop observes Closing + empty and exits (CloseLocked already ran).
  queue_cv_.notify_all();

  if (actor_.joinable()) {
    actor_.join();
  }

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    // Drain any leftover (should be empty by invariant).
    while (!task_queue_.empty()) {
      task_queue_.pop();
    }
    state_ = ActorState::Closed;
    actor_tid_ = {};
  }
  queue_cv_.notify_all();
}

void Database::Open(const std::filesystem::path& path) {
  Close();
  StartActor();
  const auto p = path;
  auto opened = std::make_shared<std::promise<void>>();
  auto openedFuture = opened->get_future();
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (state_ != ActorState::Starting) {
      throw std::runtime_error("database_not_accepting");
    }
    task_queue_.emplace([this, p, opened] {
      try {
        OpenLocked(p);
        {
          std::lock_guard<std::mutex> lock(queue_mutex_);
          if (state_ != ActorState::Starting) {
            throw std::runtime_error("database_not_accepting");
          }
          state_ = ActorState::Open;
        }
        opened->set_value();
      } catch (...) {
        try {
          CloseLocked();
        } catch (...) {
        }
        {
          std::lock_guard<std::mutex> lock(queue_mutex_);
          if (state_ == ActorState::Starting) {
            state_ = ActorState::Failed;
          }
        }
        try {
          opened->set_exception(std::current_exception());
        } catch (...) {
        }
        queue_cv_.notify_all();
      }
    });
  }
  queue_cv_.notify_one();
  openedFuture.get();
}

void Database::Initialize() {
  Submit([this] { InitializeLocked(); });
}

void Database::Execute(const std::string& sql) {
#if defined(ECHO_NATIVE_HAS_SQLITE)
  Submit([this, sql] { ExecuteLocked(sql); });
#else
  (void)sql;
  Submit([] {});
#endif
}

void Database::ExecuteBound(const std::string& sql, const std::vector<BindValue>& params) {
#if defined(ECHO_NATIVE_HAS_SQLITE)
  Submit([this, sql, params] { ExecuteBoundLocked(sql, params); });
#else
  (void)sql;
  (void)params;
  Submit([] {});
#endif
}

std::vector<std::vector<std::string>> Database::ExecuteQuery(const std::string& sql) const {
  return ExecuteQueryBound(sql, {});
}

std::vector<std::vector<std::string>> Database::ExecuteQueryBound(
    const std::string& sql, const std::vector<BindValue>& params) const {
#if defined(ECHO_NATIVE_HAS_SQLITE)
  return Submit([this, sql, params] { return ExecuteQueryBoundLocked(sql, params); });
#else
  (void)sql;
  (void)params;
  return Submit([] { return std::vector<std::vector<std::string>>{}; });
#endif
}

void Database::SetJson(const std::string& key, const nlohmann::json& value) {
  Submit([this, key, value] { SetJsonLocked(key, value); });
}

std::optional<nlohmann::json> Database::GetJson(const std::string& key) const {
  return Submit([this, key] { return GetJsonLocked(key); });
}

void Database::PutApiCache(
    const std::string& key,
    const nlohmann::json& value,
    std::int64_t expiresAt) {
  Submit([this, key, value, expiresAt] { PutApiCacheLocked(key, value, expiresAt); });
}

std::optional<nlohmann::json> Database::GetApiCache(
    const std::string& key,
    std::int64_t now) const {
  return Submit([this, key, now] { return GetApiCacheLocked(key, now); });
}

void Database::PruneExpiredApiCache(std::int64_t now) {
  Submit([this, now] { PruneExpiredApiCacheLocked(now); });
}

#if defined(ECHO_NATIVE_HAS_SQLITE)

void Database::ApplyBusyTimeout(sqlite3* db) const {
  if (!db) return;
  char* error = nullptr;
  sqlite3_exec(db, "PRAGMA busy_timeout=5000;", nullptr, nullptr, &error);
  if (error) sqlite3_free(error);
}

void Database::OpenLocked(std::filesystem::path path) {
  path_ = std::move(path);
  schema_ready_ = false;
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
  std::filesystem::create_directories(path_.parent_path());
  QuarantineInvalidSqliteFile(path_);
  if (sqlite3_open16(path_.c_str(), &db_) != SQLITE_OK) {
    ThrowSqlite(db_, "sqlite3_open16");
  }
  ApplyBusyTimeout(db_);
}

void Database::CloseLocked() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
  schema_ready_ = false;
  path_.clear();
}

void Database::InitializeLocked() {
  try {
    InitializeSchema();
  } catch (const std::runtime_error& error) {
    const std::string message = error.what();
    if (path_.empty() || message.find("file is not a database") == std::string::npos) {
      throw;
    }

    if (db_) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
    schema_ready_ = false;

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
  ExecuteLocked("PRAGMA journal_mode=WAL;");
  ExecuteLocked("PRAGMA synchronous=NORMAL;");
  ExecuteLocked("PRAGMA busy_timeout=5000;");
  ExecuteLocked("CREATE TABLE IF NOT EXISTS kv_store ("
                "key TEXT PRIMARY KEY,"
                "value TEXT NOT NULL,"
                "updated_at INTEGER NOT NULL"
                ");");
  ExecuteLocked("CREATE TABLE IF NOT EXISTS api_cache ("
                "cache_key TEXT PRIMARY KEY,"
                "response_json TEXT NOT NULL,"
                "expires_at INTEGER NOT NULL,"
                "created_at INTEGER NOT NULL"
                ");");
  ExecuteLocked("CREATE TABLE IF NOT EXISTS play_history ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "mix_song_id TEXT NOT NULL,"
                "played_at INTEGER NOT NULL,"
                "progress_seconds INTEGER NOT NULL DEFAULT 0"
                ");");
  ExecuteLocked("CREATE TABLE IF NOT EXISTS play_history_v2 ("
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
  ExecuteLocked("CREATE INDEX IF NOT EXISTS idx_ph2_played_at ON play_history_v2(played_at DESC);");
  ExecuteLocked("CREATE INDEX IF NOT EXISTS idx_ph2_song_hash ON play_history_v2(song_hash);");
  ExecuteLocked("CREATE INDEX IF NOT EXISTS idx_ph2_singer ON play_history_v2(singer_name);");
  ExecuteLocked("CREATE TABLE IF NOT EXISTS image_cache ("
                "url TEXT PRIMARY KEY,"
                "file_path TEXT NOT NULL,"
                "bytes INTEGER NOT NULL,"
                "last_access_at INTEGER NOT NULL,"
                "created_at INTEGER NOT NULL"
                ");");
  ExecuteLocked("PRAGMA user_version=1;");
  ExecuteLocked("CREATE INDEX IF NOT EXISTS idx_api_cache_expires ON api_cache(expires_at);");
  schema_ready_ = true;
}

void Database::ExecuteLocked(const std::string& sql) {
  char* error = nullptr;
  if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
    std::string message = error ? error : "unknown sqlite error";
    sqlite3_free(error);
    throw std::runtime_error("sqlite3_exec: " + message);
  }
}

void Database::ExecuteBoundLocked(const std::string& sql, const std::vector<BindValue>& params) {
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

std::vector<std::vector<std::string>> Database::ExecuteQueryBoundLocked(
    const std::string& sql, const std::vector<BindValue>& params) const {
  std::vector<std::vector<std::string>> rows;
  if (!db_) return rows;

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
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

void Database::SetJsonLocked(const std::string& key, const nlohmann::json& value) {
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

std::optional<nlohmann::json> Database::GetJsonLocked(const std::string& key) const {
  auto rows = ExecuteQueryBoundLocked("SELECT value FROM kv_store WHERE key=?1 LIMIT 1;", {key});
  if (rows.empty() || rows[0].empty()) return std::nullopt;
  auto parsed = nlohmann::json::parse(rows[0][0], nullptr, false);
  if (parsed.is_discarded()) return std::nullopt;
  return parsed;
}

void Database::PutApiCacheLocked(
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

std::optional<nlohmann::json> Database::GetApiCacheLocked(
    const std::string& key,
    std::int64_t now) const {
  auto rows = ExecuteQueryBoundLocked(
      "SELECT response_json FROM api_cache WHERE cache_key=?1 AND expires_at>?2 LIMIT 1;",
      {key, now});
  if (rows.empty() || rows[0].empty()) return std::nullopt;
  auto parsed = nlohmann::json::parse(rows[0][0], nullptr, false);
  if (parsed.is_discarded()) return std::nullopt;
  return parsed;
}

void Database::PruneExpiredApiCacheLocked(std::int64_t now) {
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

void Database::OpenLocked(std::filesystem::path path) {
  path_ = std::move(path);
  std::filesystem::create_directories(path_.parent_path());
  fallback_ = nlohmann::json{{"kv_store", nlohmann::json::object()},
                             {"api_cache", nlohmann::json::object()}};
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

void Database::CloseLocked() {
  if (!path_.empty()) {
    FlushFallback();
  }
  path_.clear();
}

void Database::InitializeLocked() {
  FlushFallback();
}

void Database::FlushFallback() const {
  if (path_.empty()) return;
  std::ofstream file(path_, std::ios::trunc);
  file << fallback_.dump(2);
}

void Database::SetJsonLocked(const std::string& key, const nlohmann::json& value) {
  fallback_["kv_store"][key] = {{"value", value}, {"updated_at", NowSeconds()}};
  FlushFallback();
}

std::optional<nlohmann::json> Database::GetJsonLocked(const std::string& key) const {
  const auto& store = fallback_.at("kv_store");
  if (!store.contains(key)) return std::nullopt;
  return store.at(key).value("value", nlohmann::json{});
}

void Database::PutApiCacheLocked(
    const std::string& key,
    const nlohmann::json& value,
    std::int64_t expiresAt) {
  fallback_["api_cache"][key] = {
      {"response_json", value},
      {"expires_at", expiresAt},
      {"created_at", NowSeconds()}};
  FlushFallback();
}

std::optional<nlohmann::json> Database::GetApiCacheLocked(
    const std::string& key,
    std::int64_t now) const {
  const auto& store = fallback_.at("api_cache");
  if (!store.contains(key)) return std::nullopt;
  const auto& entry = store.at(key);
  if (entry.value("expires_at", 0LL) <= now) return std::nullopt;
  return entry.value("response_json", nlohmann::json{});
}

void Database::PruneExpiredApiCacheLocked(std::int64_t now) {
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
