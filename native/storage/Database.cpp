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

// TEST SEAM (B04) — see Database.h. Default: real filesystem rename.
QuarantineRenameHook g_quarantineRenameHook = nullptr;

bool RenameForQuarantine(const std::filesystem::path& from,
                         const std::filesystem::path& to,
                         std::error_code& ec) {
  if (g_quarantineRenameHook) {
    return g_quarantineRenameHook(from, to, ec);
  }
  std::filesystem::rename(from, to, ec);
  return !ec;
}

#if defined(ECHO_NATIVE_HAS_SQLITE)
// B04: a file that is not a SQLite database (or a database whose header
// schema check fails) must be QUARANTINED, never destroyed. The recovery
// paths below therefore:
//   - never remove the original database file or its WAL/SHM sidecars when a
//     rename fails — a failed quarantine keeps every byte of user data on
//     disk and fails loudly instead of silently recreating an empty database;
//   - pick a collision-proof backup name (the epoch-second suffix can
//     repeat within the same second and this environment's rename overwrites
//     an existing plain-file target, which would replace a previous
//     quarantine instead of failing);
//   - move WAL/SHM to matching backup names on success, because a stale
//     sidecar left next to the freshly created database would be applied to
//     it by SQLite's recovery logic.
// Returns the backup path of the quarantined database.
std::filesystem::path QuarantinePathFor(const std::filesystem::path& path) {
  const auto base = path.wstring() + L".invalid-";
  const auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  for (std::uint64_t attempt = 0;; ++attempt) {
    std::filesystem::path candidate =
        base + std::to_wstring(epoch) + L"-" + std::to_wstring(attempt);
    // exists() covers both file and directory targets: renaming onto either
    // is either a clobber (plain file) or a failure (directory) — skip both.
    if (!std::filesystem::exists(std::filesystem::path(candidate))) {
      return candidate;
    }
  }
}

// Move the database file and its WAL/SHM sidecars to quarantine backup
// names. Fail-closed contract (B04):
//   - every move (including sidecar moves and rollback moves) goes through
//     RenameForQuarantine, so the test hook injects failures at any point;
//   - nothing is ever removed: if a sidecar move fails after the database
//     was already moved, the already-moved pieces are renamed back to their
//     live paths (best effort, reverse order) and the original error is
//     rethrown — no new live database may be created on this path;
//   - if the rollback itself fails, the thrown error lists the exact backup
//     paths where every moved byte is preserved for forensics.
// Returns the backup path of the quarantined database.
std::filesystem::path QuarantineWithSidecars(const std::filesystem::path& liveDb,
                                             const std::filesystem::path& backupDb) {
  // (backupPath, livePath): rollback direction, pushed after each success.
  std::vector<std::pair<std::filesystem::path, std::filesystem::path>> movedBack;

  std::error_code ec;
  if (!RenameForQuarantine(liveDb, backupDb, ec)) {
    throw std::runtime_error("sqlite_recovery_rename_failed: " + liveDb.string() +
                             " -> " + backupDb.string() + ": " + ec.message());
  }
  movedBack.emplace_back(backupDb, liveDb);

  for (const wchar_t* suffix : {L"-wal", L"-shm"}) {
    const auto liveSidecar = std::filesystem::path(liveDb.wstring() + suffix);
    if (!std::filesystem::exists(liveSidecar)) continue;
    const auto backupSidecar = std::filesystem::path(backupDb.wstring() + suffix);
    ec.clear();
    if (!RenameForQuarantine(liveSidecar, backupSidecar, ec)) {
      const std::string failure = "sqlite_recovery_sidecar_rename_failed: " +
                                  liveSidecar.string() + " -> " + backupSidecar.string() +
                                  ": " + ec.message();
      // Best-effort rollback in reverse move order. Never remove: a piece
      // that cannot be moved back stays at its backup path, intact.
      std::vector<std::string> stuck;
      for (auto it = movedBack.rbegin(); it != movedBack.rend(); ++it) {
        std::error_code rollbackEc;
        if (!RenameForQuarantine(it->first, it->second, rollbackEc)) {
          stuck.push_back(it->first.string());
        }
      }
      if (!stuck.empty()) {
        std::string paths;
        for (const auto& p : stuck) {
          if (!paths.empty()) paths += "; ";
          paths += p;
        }
        throw std::runtime_error(
            "sqlite_recovery_rollback_failed: original bytes preserved at: " + paths +
            "; (" + failure + ")");
      }
      throw std::runtime_error("sqlite_recovery_quarantine_failed: " + failure +
                               "; original files restored in place");
    }
    movedBack.emplace_back(backupSidecar, liveSidecar);
  }
  return backupDb;
}

std::filesystem::path QuarantineInvalidSqliteFile(const std::filesystem::path& path) {
  if (path.empty() || !std::filesystem::exists(path) || std::filesystem::is_directory(path)) {
    return {};
  }

  const auto size = std::filesystem::file_size(path);
  if (size == 0) {
    return {};
  }

  char header[16] = {};
  {
    std::ifstream file(path, std::ios::binary);
    const auto expectedBytes = static_cast<std::streamsize>(
        std::min<std::uintmax_t>(size, sizeof(header)));
    if (!file.is_open()) {
      throw std::runtime_error("sqlite_header_read_failed: " + path.string());
    }
    file.read(header, expectedBytes);
    if (file.gcount() != expectedBytes || file.bad()) {
      // Inaccessibility is not corruption. Windows can deny a read while
      // still allowing rename; quarantining here would replace healthy data.
      throw std::runtime_error("sqlite_header_read_failed: " + path.string());
    }
  }

  constexpr char sqliteHeader[16] = {
      'S', 'Q', 'L', 'i', 't', 'e', ' ', 'f', 'o', 'r', 'm', 'a', 't', ' ', '3', '\0'};
  if (std::equal(std::begin(header), std::end(header), std::begin(sqliteHeader))) {
    return {};
  }

  const auto backupPath = QuarantinePathFor(path);
  QuarantineWithSidecars(path, backupPath);
  return backupPath;
}

void ThrowSqlite(sqlite3* db, const std::string& context) {
  throw std::runtime_error(context + ": " + sqlite3_errmsg(db));
}

// B03/B04: a null handle is a "not open / recovery failed" state (a failed
// InitializeLocked closes db_ while the actor can still be Open), never a
// source of empty results. Every locked operation must fail loudly on it.
void EnsureSqliteOpen(sqlite3* db) {
  if (!db) throw std::runtime_error("database_not_open");
}

void BindParams(sqlite3_stmt* stmt, const std::vector<BindValue>& params) {
  for (size_t i = 0; i < params.size(); ++i) {
    const int idx = static_cast<int>(i + 1);
    const int rc = std::visit(
        [&](const auto& v) -> int {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, std::int64_t>) {
            return sqlite3_bind_int64(stmt, idx, v);
          } else if constexpr (std::is_same_v<T, double>) {
            return sqlite3_bind_double(stmt, idx, v);
          } else {
            return sqlite3_bind_text64(stmt, idx, v.data(),
                                       static_cast<sqlite3_uint64>(v.size()),
                                       SQLITE_TRANSIENT, SQLITE_UTF8);
          }
        },
        params[i]);
    if (rc != SQLITE_OK) {
      ThrowSqlite(sqlite3_db_handle(stmt), "sqlite3_bind ExecuteBound");
    }
  }
}

// RAII finalize: a thrown step/bind error must not leak the statement handle.
struct StmtGuard {
  sqlite3_stmt* stmt;
  explicit StmtGuard(sqlite3_stmt* s) : stmt(s) {}
  ~StmtGuard() {
    if (stmt) sqlite3_finalize(stmt);
  }
  StmtGuard(const StmtGuard&) = delete;
  StmtGuard& operator=(const StmtGuard&) = delete;
};
#endif

}  // namespace

void SetQuarantineRenameHookForTest(QuarantineRenameHook hook) {
  g_quarantineRenameHook = hook;
}

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

std::optional<nlohmann::json> Database::UpdateJson(
    const std::string& key,
    const std::function<std::optional<nlohmann::json>(std::optional<nlohmann::json>)>& mutator) {
  return Submit([this, key, &mutator] { return UpdateJsonLocked(key, mutator); });
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
  // A failed quarantine rollback can leave the original db at its backup
  // path while WAL/SHM remain live. Opening an absent (or empty) main file
  // here would let SQLite create a fresh db and potentially replay the old
  // WAL into it. Preserve all bytes and require explicit recovery instead.
  const bool missingOrEmpty = !std::filesystem::exists(path_) ||
      (std::filesystem::is_regular_file(path_) && std::filesystem::file_size(path_) == 0);
  if (missingOrEmpty) {
    std::string orphanPaths;
    for (const wchar_t* suffix : {L"-wal", L"-shm"}) {
      const auto sidecar = std::filesystem::path(path_.wstring() + suffix);
      if (std::filesystem::exists(sidecar)) {
        if (!orphanPaths.empty()) orphanPaths += "; ";
        orphanPaths += sidecar.string();
      }
    }
    if (!orphanPaths.empty()) {
      throw std::runtime_error("sqlite_recovery_orphaned_sidecars: " + path_.string() +
                               "; preserved sidecars: " + orphanPaths +
                               "; restore the matching database or quarantine the sidecars before retrying");
    }
  }
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

    // B04: quarantine the unreadable database instead of destroying it.
    // QuarantineWithSidecars keeps every original byte intact on any move
    // failure (with best-effort rollback of already-moved pieces) and never
    // removes data; the error aborts recovery before a new database can be
    // created at the live path.
    const auto backupPath = QuarantinePathFor(path_);
    QuarantineWithSidecars(path_, backupPath);

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
  EnsureSqliteOpen(db_);
  char* error = nullptr;
  if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
    std::string message = error ? error : "unknown sqlite error";
    sqlite3_free(error);
    throw std::runtime_error("sqlite3_exec: " + message);
  }
}

void Database::ExecuteBoundLocked(const std::string& sql, const std::vector<BindValue>& params) {
  EnsureSqliteOpen(db_);
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    ThrowSqlite(db_, "sqlite3_prepare_v2 ExecuteBound");
  }
  StmtGuard guard(stmt);
  BindParams(stmt, params);
  const int rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
    ThrowSqlite(db_, "sqlite3_step ExecuteBound");
  }
}

std::vector<std::vector<std::string>> Database::ExecuteQueryBoundLocked(
    const std::string& sql, const std::vector<BindValue>& params) const {
  std::vector<std::vector<std::string>> rows;
  // B03: a failed query must surface as an error, never as an empty (or
  // partial) result. This includes the not-open state left by a failed
  // Initialize recovery (B04): the old `if (!db_) return rows;` turned it
  // into a silent empty result. The old loop treated prepare failure as "no rows" and
  // stopped stepping on anything that was not SQLITE_ROW — so missing
  // tables, SQL errors, locks, and I/O faults all looked like "no data",
  // and a mid-iteration failure returned the rows collected so far as if
  // the result were complete.
  EnsureSqliteOpen(db_);
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    ThrowSqlite(db_, "sqlite3_prepare_v2 ExecuteQuery");
  }
  StmtGuard guard(stmt);
  BindParams(stmt, params);
  const int colCount = sqlite3_column_count(stmt);
  for (;;) {
    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      std::vector<std::string> row;
      row.reserve(static_cast<std::size_t>(colCount));
      for (int i = 0; i < colCount; ++i) {
        const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
        row.push_back(val ? val : "");
      }
      rows.push_back(std::move(row));
      continue;
    }
    if (rc == SQLITE_DONE) {
      break;
    }
    ThrowSqlite(db_, "sqlite3_step ExecuteQuery");
  }
  return rows;
}

void Database::SetJsonLocked(const std::string& key, const nlohmann::json& value) {
  EnsureSqliteOpen(db_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO kv_store(key, value, updated_at) VALUES(?1, ?2, ?3) "
      "ON CONFLICT(key) DO UPDATE SET value=excluded.value, updated_at=excluded.updated_at;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    ThrowSqlite(db_, "sqlite3_prepare_v2 SetJson");
  }
  StmtGuard guard(stmt);
  const auto payload = value.dump();
  BindParams(stmt, {key, payload, NowSeconds()});
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    ThrowSqlite(db_, "sqlite3_step SetJson");
  }
}

std::optional<nlohmann::json> Database::GetJsonLocked(const std::string& key) const {
  auto rows = ExecuteQueryBoundLocked("SELECT value FROM kv_store WHERE key=?1 LIMIT 1;", {key});
  if (rows.empty() || rows[0].empty()) return std::nullopt;
  auto parsed = nlohmann::json::parse(rows[0][0], nullptr, false);
  if (parsed.is_discarded()) return std::nullopt;
  return parsed;
}

std::optional<nlohmann::json> Database::UpdateJsonLocked(
    const std::string& key,
    const std::function<std::optional<nlohmann::json>(std::optional<nlohmann::json>)>& mutator) {
  // Runs entirely on the actor thread: no other operation can interleave
  // between the read and the write, so the mutator's decision is based on
  // the value it actually wrote against.
  auto current = GetJsonLocked(key);
  auto next = mutator(std::move(current));
  if (next.has_value()) {
    SetJsonLocked(key, *next);
  }
  return next;
}

void Database::PutApiCacheLocked(
    const std::string& key,
    const nlohmann::json& value,
    std::int64_t expiresAt) {
  EnsureSqliteOpen(db_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO api_cache(cache_key, response_json, expires_at, created_at) "
      "VALUES(?1, ?2, ?3, ?4) "
      "ON CONFLICT(cache_key) DO UPDATE SET response_json=excluded.response_json, "
      "expires_at=excluded.expires_at, created_at=excluded.created_at;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    ThrowSqlite(db_, "sqlite3_prepare_v2 PutApiCache");
  }
  StmtGuard guard(stmt);
  const auto payload = value.dump();
  BindParams(stmt, {key, payload, expiresAt, NowSeconds()});
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    ThrowSqlite(db_, "sqlite3_step PutApiCache");
  }
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
  EnsureSqliteOpen(db_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "DELETE FROM api_cache WHERE expires_at<=?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    ThrowSqlite(db_, "sqlite3_prepare_v2 PruneExpiredApiCache");
  }
  StmtGuard guard(stmt);
  BindParams(stmt, {now});
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    ThrowSqlite(db_, "sqlite3_step PruneExpiredApiCache");
  }
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

std::optional<nlohmann::json> Database::UpdateJsonLocked(
    const std::string& key,
    const std::function<std::optional<nlohmann::json>(std::optional<nlohmann::json>)>& mutator) {
  auto current = GetJsonLocked(key);
  auto next = mutator(std::move(current));
  if (next.has_value()) {
    SetJsonLocked(key, *next);
  }
  return next;
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
