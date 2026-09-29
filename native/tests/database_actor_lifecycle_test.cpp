// Storage Actor lifecycle + concurrency contract tests (P1 #1).
// Requires actor serialization and lock-held Close/Submit protocol (design r2).

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "echo/storage/Database.h"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

std::filesystem::path MakeDbDir(const char* name) {
  static std::atomic<unsigned> next{0};
  const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                      "-" + std::to_string(next.fetch_add(1));
  auto dir = std::filesystem::temp_directory_path() / (std::string(name) + "-" + unique);
  std::filesystem::create_directories(dir);
  return dir;
}

void ExpectNotAccepting(echo::storage::Database& db) {
  bool threw = false;
  try {
    db.SetJson("after-close", nlohmann::json{{"x", 1}});
  } catch (const std::runtime_error& e) {
    threw = true;
    const std::string msg = e.what();
    assert(msg.find("database_not_accepting") != std::string::npos);
  }
  assert(threw);
}

std::string ReadAllBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
}

void WriteAllBytes(const std::filesystem::path& path, const std::string& bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << bytes;
}

#if defined(_WIN32)
// Rename-failure seam: allow reads/writes but deny FILE_SHARE_DELETE. This is
// the same failure class the audit's directory-collision probe produced, with
// none of its timing dependence.
struct RenameDeniedHandle {
  HANDLE handle;
  explicit RenameDeniedHandle(const std::filesystem::path& path) {
    handle = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  }
  ~RenameDeniedHandle() {
    if (handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle);
  }
  bool ok() const { return handle != INVALID_HANDLE_VALUE; }
};

// A healthy database can be unreadable while Windows still permits rename.
// Such a sharing violation must never be interpreted as an invalid header.
struct RenameAllowedReadDeniedHandle {
  HANDLE handle;
  explicit RenameAllowedReadDeniedHandle(const std::filesystem::path& path) {
    handle = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  }
  ~RenameAllowedReadDeniedHandle() {
    if (handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle);
  }
  bool ok() const { return handle != INVALID_HANDLE_VALUE; }
};
#endif

bool MessageContains(const std::exception& error, const char* needle) {
  return std::string(error.what()).find(needle) != std::string::npos;
}

// TEST HOOK (B04 sidecar slice): passes every rename through to the real
// filesystem EXCEPT renames whose destination is a WAL/SHM sidecar backup
// ("...-wal" / "...-shm"), which fail deterministically. This injects a
// sidecar-move failure after the primary database rename already succeeded —
// the exact fault class the production recovery path must roll back.
bool FailSidecarTargetRenameHook(const std::filesystem::path& from,
                                 const std::filesystem::path& to,
                                 std::error_code& ec) {
  const std::wstring name = to.filename().wstring();
  if (name.size() >= 4 && (name.rfind(L"-wal") == name.size() - 4 ||
                           name.rfind(L"-shm") == name.size() - 4)) {
    ec = std::make_error_code(std::errc::device_or_resource_busy);
    return false;
  }
  std::filesystem::rename(from, to, ec);
  return !ec;
}

// TEST HOOK (B04 rollback-failure slice): the first N calls pass through to
// the real filesystem; every later call fails. Function-pointer hooks cannot
// capture state, so the counters live here and are reset per scenario.
std::atomic<int> g_failAfterHookCalls{0};
std::atomic<int> g_failAfterHookSucceedFirst{1};

bool FailAfterFirstCallRenameHook(const std::filesystem::path& from,
                                  const std::filesystem::path& to,
                                  std::error_code& ec) {
  if (g_failAfterHookCalls.fetch_add(1) >=
      g_failAfterHookSucceedFirst.load(std::memory_order_relaxed)) {
    ec = std::make_error_code(std::errc::device_or_resource_busy);
    return false;
  }
  std::filesystem::rename(from, to, ec);
  return !ec;
}

void ResetFailAfterFirstCallHook() {
  g_failAfterHookCalls.store(0, std::memory_order_relaxed);
  g_failAfterHookSucceedFirst.store(1, std::memory_order_relaxed);
}

void AssertNoQuarantineLeftovers(const std::filesystem::path& dir) {
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    const std::string name = entry.path().filename().string();
    assert(name.find(".invalid-") == std::string::npos);
  }
}

}  // namespace

int main() {
#if defined(_MSC_VER)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _set_error_mode(_OUT_TO_STDERR);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

  // ── 1) Close is idempotent ──
  {
    const auto dir = MakeDbDir("bm-actor-close-idempotent");
    echo::storage::Database db;
    db.Open(dir / "t.db");
    db.Initialize();
    db.SetJson("k", nlohmann::json{{"v", 1}});
    db.Close();
    db.Close();
    db.Close();
    ExpectNotAccepting(db);
    std::cout << "[ActorLifecycle] close_idempotent ok\n";
  }

  // ── 2) After Close, public writes fail without crash ──
  {
    const auto dir = MakeDbDir("bm-actor-after-close");
    echo::storage::Database db;
    db.Open(dir / "t.db");
    db.Initialize();
    db.Close();
    ExpectNotAccepting(db);
    std::cout << "[ActorLifecycle] after_close_reject ok\n";
  }

  // ── 3) Open/Close cycle × 100 ──
  {
    const auto dir = MakeDbDir("bm-actor-open-close-100");
    const auto path = dir / "t.db";
    for (int i = 0; i < 100; ++i) {
      echo::storage::Database db;
      db.Open(path);
      db.Initialize();
      db.SetJson("i", nlohmann::json{{"n", i}});
      auto j = db.GetJson("i");
      assert(j.has_value());
      assert((*j)["n"] == i);
      db.Close();
    }
    std::cout << "[ActorLifecycle] open_close_100 ok\n";
  }

  // ── 4) Multi-thread 1000 R/W (serializable consistency) ──
  {
    const auto dir = MakeDbDir("bm-actor-mt-1000");
    echo::storage::Database db;
    db.Open(dir / "t.db");
    db.Initialize();

    constexpr int kThreads = 8;
    constexpr int kPerThread = 125;  // 8 * 125 = 1000
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    std::atomic<int> failures{0};

    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&, t] {
        for (int i = 0; i < kPerThread; ++i) {
          const std::string key = "k-" + std::to_string(t) + "-" + std::to_string(i);
          try {
            db.SetJson(key, nlohmann::json{{"t", t}, {"i", i}});
            auto j = db.GetJson(key);
            if (!j.has_value() || (*j)["t"] != t || (*j)["i"] != i) {
              failures.fetch_add(1);
            }
          } catch (...) {
            failures.fetch_add(1);
          }
        }
      });
    }
    for (auto& th : threads) th.join();
    assert(failures.load() == 0);
    db.Close();
    std::cout << "[ActorLifecycle] mt_1000_rw ok\n";
  }

  // ── 5) Concurrent Submit + Close (TOCTOU / no use-after-close) ──
  {
    const auto dir = MakeDbDir("bm-actor-submit-close-race");
    echo::storage::Database db;
    db.Open(dir / "t.db");
    db.Initialize();

    std::atomic<bool> start{false};
    std::atomic<int> write_ok{0};
    std::atomic<int> write_rejected{0};
    std::atomic<int> write_other_error{0};

    std::vector<std::thread> writers;
    for (int t = 0; t < 4; ++t) {
      writers.emplace_back([&, t] {
        while (!start.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }
        for (int i = 0; i < 200; ++i) {
          try {
            db.SetJson("race-" + std::to_string(t) + "-" + std::to_string(i),
                       nlohmann::json{{"i", i}});
            write_ok.fetch_add(1, std::memory_order_relaxed);
          } catch (const std::runtime_error& e) {
            const std::string msg = e.what();
            if (msg.find("database_not_accepting") != std::string::npos) {
              write_rejected.fetch_add(1, std::memory_order_relaxed);
            } else {
              write_other_error.fetch_add(1, std::memory_order_relaxed);
            }
          } catch (...) {
            write_other_error.fetch_add(1, std::memory_order_relaxed);
          }
        }
      });
    }

    std::thread closer([&] {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      db.Close();
    });

    start.store(true, std::memory_order_release);
    for (auto& th : writers) th.join();
    closer.join();

    assert(write_other_error.load() == 0);
    // At least some work happened or was cleanly rejected; never hang/crash.
    assert(write_ok.load() + write_rejected.load() == 4 * 200);
    ExpectNotAccepting(db);
    std::cout << "[ActorLifecycle] submit_close_race ok (ok=" << write_ok.load()
              << " rejected=" << write_rejected.load() << ")\n";
  }

  // ── 6) Re-open after Close works ──
  {
    const auto dir = MakeDbDir("bm-actor-reopen");
    const auto path = dir / "t.db";
    echo::storage::Database db;
    db.Open(path);
    db.Initialize();
    db.SetJson("persist", nlohmann::json{{"a", 1}});
    db.Close();
    db.Open(path);
    db.Initialize();
    auto j = db.GetJson("persist");
    assert(j.has_value());
    assert((*j)["a"] == 1);
    db.Close();
    std::cout << "[ActorLifecycle] reopen ok\n";
  }

  // ── 7) Failed Open never publishes an accepting actor ──
  {
    const auto invalidParent =
        std::filesystem::temp_directory_path() / "bm-actor-invalid-parent";
    std::error_code ec;
    std::filesystem::remove_all(invalidParent, ec);
    {
      std::ofstream marker(invalidParent);
      marker << "not-a-directory";
    }

    echo::storage::Database db;
    bool openFailed = false;
    try {
      db.Open(invalidParent / "t.db");
    } catch (...) {
      openFailed = true;
    }
    assert(openFailed);
    ExpectNotAccepting(db);

    const auto validDir = MakeDbDir("bm-actor-recover-after-open-failure");
    db.Open(validDir / "t.db");
    db.Initialize();
    db.SetJson("recovered", nlohmann::json{{"ok", true}});
    assert(db.GetJson("recovered").has_value());
    db.Close();
    std::cout << "[ActorLifecycle] failed_open_not_published ok\n";
  }

  // ── 8) B04: quarantine rename failure keeps every byte of the original ──
  {
    const auto dir = MakeDbDir("bm-b04-quarantine-fail");
    const auto dbPath = dir / "corrupt.db";
    const auto walPath = dir / (dbPath.wstring() + L"-wal");
    const auto shmPath = dir / (dbPath.wstring() + L"-shm");

    const std::string originalDb = "definitely-not-a-sqlite-database-body-0123456789";
    const std::string originalWal = "wal-sidecar-bytes-abcdef";
    const std::string originalShm = "shm-sidecar-bytes-123456";
    WriteAllBytes(dbPath, originalDb);
    WriteAllBytes(walPath, originalWal);
    WriteAllBytes(shmPath, originalShm);

    RenameDeniedHandle locked(dbPath);
#if defined(_WIN32)
    assert(locked.ok());  // the rename-failure seam must actually hold
#endif

    echo::storage::Database db;
    bool failedWithRecoveryError = false;
    try {
      db.Open(dbPath);  // OpenLocked quarantines non-SQLite files -> rename fails
    } catch (const std::runtime_error& e) {
      failedWithRecoveryError = MessageContains(e, "sqlite_recovery_rename_failed");
    }
    assert(failedWithRecoveryError);
    ExpectNotAccepting(db);

    // Release the seam BEFORE reading: with share mode 0 nothing else (not
    // even this test's verification reads) can open the file.
    {
#if defined(_WIN32)
      ::CloseHandle(locked.handle);
      locked.handle = INVALID_HANDLE_VALUE;
#endif
    }

    // B04 contract: the original file AND its sidecars are byte-for-byte
    // intact, and no ".invalid-*" backup was created by the failed attempt.
    assert(ReadAllBytes(dbPath) == originalDb);
    assert(ReadAllBytes(walPath) == originalWal);
    assert(ReadAllBytes(shmPath) == originalShm);
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      const std::string name = entry.path().filename().string();
      assert(name == "corrupt.db" || name.find(".invalid-") != 0);
    }

    {
      echo::storage::Database db2;
      db2.Open(dbPath);
      db2.Initialize();
      db2.SetJson("fresh", nlohmann::json{{"ok", true}});
      assert(db2.GetJson("fresh").has_value());
      db2.Close();
    }
    bool sawBackupDb = false;
    bool sawBackupWal = false;
    bool sawBackupShm = false;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      const std::string name = entry.path().filename().string();
      if (name.rfind("corrupt.db.invalid-", 0) != 0) continue;
      if (name.find("-wal") != std::string::npos) {
        sawBackupWal = true;
        assert(ReadAllBytes(entry.path()) == originalWal);
      } else if (name.find("-shm") != std::string::npos) {
        sawBackupShm = true;
        assert(ReadAllBytes(entry.path()) == originalShm);
      } else {
        sawBackupDb = true;
        assert(ReadAllBytes(entry.path()) == originalDb);
      }
    }
    assert(sawBackupDb && sawBackupWal && sawBackupShm);
    assert(ReadAllBytes(dbPath) != originalDb);  // live path now holds a real db
    std::cout << "[ActorLifecycle] b04_quarantine_failure_keeps_bytes ok\n";
  }

  // ── 9) B04: back-to-back quarantines never clobber a previous backup ──
  {
    const auto dir = MakeDbDir("bm-b04-quarantine-collision");
    const auto dbPath = dir / "collide.db";
    const std::string first = "first-corrupt-payload-aaaaaaaa";
    const std::string second = "second-corrupt-payload-bbbbbbbb";

    WriteAllBytes(dbPath, first);
    {
      echo::storage::Database db;
      db.Open(dbPath);
      db.Close();  // quarantines `first`
    }
    WriteAllBytes(dbPath, second);
    {
      echo::storage::Database db;
      db.Open(dbPath);
      db.Close();  // quarantines `second` — must not replace the first backup
    }

    std::vector<std::string> backups;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      const std::string name = entry.path().filename().string();
      if (name.rfind("collide.db.invalid-", 0) == 0) {
        const bool sidecar = name.find("-wal") != std::string::npos ||
                             name.find("-shm") != std::string::npos;
        if (!sidecar) backups.push_back(name);
      }
    }
    assert(backups.size() == 2);
    bool sawFirst = false;
    bool sawSecond = false;
    for (const auto& name : backups) {
      const std::string bytes = ReadAllBytes(dir / name);
      sawFirst = sawFirst || bytes == first;
      sawSecond = sawSecond || bytes == second;
    }
    assert(sawFirst && sawSecond);
    std::cout << "[ActorLifecycle] b04_back_to_back_quarantines_both_preserved ok\n";
  }

  // ── 10) B04: InitializeLocked recovery failure also keeps every byte ──
  {
    const auto dir = MakeDbDir("bm-b04-init-recovery-fail");
    const auto dbPath = dir / "headeronly.db";
    // Valid 16-byte SQLite header (so the Open-path header check passes) with
    // an invalid page size at offset 16 -> InitializeSchema reports
    // "file is not a database" and the recovery path tries to quarantine.
    const std::string headerOnly = std::string("SQLite format 3\0", 16) + "GARBAGEGARBAGE";
    WriteAllBytes(dbPath, headerOnly);

    // File-op seam: force the quarantine rename to fail with a sharing
    // violation, the same error class a second process holding the file
    // open produces. (An in-process exclusive handle cannot be used here:
    // sqlite's own open handle already grants read/write sharing, so the
    // exclusive open is rejected before Initialize even runs.)
    echo::storage::SetQuarantineRenameHookForTest(
        [](const std::filesystem::path&, const std::filesystem::path&,
           std::error_code& ec) {
          ec = std::make_error_code(std::errc::device_or_resource_busy);
          return false;
        });

    echo::storage::Database db;
    db.Open(dbPath);  // header check passes; the database opens lazily
    bool failedWithRecoveryError = false;
    try {
      db.Initialize();
    } catch (const std::runtime_error& e) {
      failedWithRecoveryError = MessageContains(e, "sqlite_recovery_rename_failed");
    }
    echo::storage::SetQuarantineRenameHookForTest(nullptr);
    assert(failedWithRecoveryError);
    assert(ReadAllBytes(dbPath) == headerOnly);  // original untouched

    // B03/B04: recovery failed and closed the sqlite handle, but the actor
    // is still Open — every subsequent operation must throw a recognizable
    // not-open/recovery error, never return an empty (or partial) result.
    bool queryThrewNotOpen = false;
    try {
      (void)db.GetJson("anything");
    } catch (const std::runtime_error& e) {
      queryThrewNotOpen = MessageContains(e, "database_not_open");
    }
    assert(queryThrewNotOpen);
    bool writeThrewNotOpen = false;
    try {
      db.SetJson("anything", nlohmann::json{{"x", 1}});
    } catch (const std::runtime_error& e) {
      writeThrewNotOpen = MessageContains(e, "database_not_open");
    }
    assert(writeThrewNotOpen);
    bool rawQueryThrewNotOpen = false;
    try {
      (void)db.ExecuteQuery("SELECT 1;");
    } catch (const std::runtime_error& e) {
      rawQueryThrewNotOpen = MessageContains(e, "database_not_open");
    }
    assert(rawQueryThrewNotOpen);

    // Recovery succeeds once the seam is removed (Close/Open resets the
    // half-recovered state), and the corrupt file is preserved as a complete
    // backup.
    db.Close();
    db.Open(dbPath);
    db.Initialize();
    db.SetJson("recovered", nlohmann::json{{"ok", true}});
    assert(db.GetJson("recovered").has_value());
    assert(ReadAllBytes(dbPath) != headerOnly);
    bool sawBackup = false;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      if (entry.path().filename().string().rfind("headeronly.db.invalid-", 0) == 0) {
        sawBackup = sawBackup || ReadAllBytes(entry.path()) == headerOnly;
      }
    }
    assert(sawBackup);
    db.Close();
    std::cout << "[ActorLifecycle] b04_init_recovery_failure_keeps_bytes ok\n";
  }

  // ── 11) B03: a failed query is an error, never an empty result ──
  {
    const auto dir = MakeDbDir("bm-b03-query-errors");
    echo::storage::Database db;
    db.Open(dir / "t.db");
    db.Initialize();

    // Missing table: prepare failure. Old behavior: silent empty rows.
    bool missingTableThrew = false;
    try {
      (void)db.ExecuteQueryBound("SELECT missing_column FROM missing_table", {});
    } catch (const std::runtime_error& e) {
      missingTableThrew = MessageContains(e, "missing_table");
    }
    assert(missingTableThrew);

    // Step error: abs() of the minimum int64 raises "integer overflow".
    bool overflowThrew = false;
    try {
      (void)db.ExecuteQueryBound("SELECT abs(-9223372036854775808);", {});
    } catch (const std::runtime_error& e) {
      overflowThrew = MessageContains(e, "sqlite3_step") ||
                      MessageContains(e, "integer overflow");
    }
    assert(overflowThrew);

    // Rows-then-failure: the rows collected before the error must NOT be
    // returned as a (partial) success.
    bool partialThrew = false;
    try {
      auto rows = db.ExecuteQueryBound(
          "SELECT CASE WHEN x < 3 THEN CAST(x AS TEXT) "
          "ELSE abs(-9223372036854775808) END "
          "FROM (VALUES (1),(2),(3),(4)) AS t(x);", {});
      (void)rows;
    } catch (const std::runtime_error&) {
      partialThrew = true;
    }
    assert(partialThrew);

    // Normal zero-row results remain normal.
    auto empty = db.ExecuteQueryBound("SELECT key FROM kv_store WHERE 0;", {});
    assert(empty.empty());

    // Normal multi-row results still work.
    db.SetJson("r1", nlohmann::json{{"v", 1}});
    auto one = db.ExecuteQueryBound("SELECT value FROM kv_store WHERE key='r1';", {});
    assert(one.size() == 1);

    db.Close();
    std::cout << "[ActorLifecycle] b03_failed_queries_surface_as_errors ok\n";
  }

  // ── 12) B02 primitive: UpdateJson is an atomic read-modify-write ──
  {
    const auto dir = MakeDbDir("bm-b02-update-json");
    echo::storage::Database db;
    db.Open(dir / "t.db");
    db.Initialize();

    // Reject (nullopt) leaves the key untouched.
    db.SetJson("u", nlohmann::json{{"gen", 5}, {"v", "keep"}});
    auto rejected = db.UpdateJson("u", [](std::optional<nlohmann::json> current) {
      return current->value("gen", 0) == 999
                 ? std::optional<nlohmann::json>(nlohmann::json{{"gen", 1000}})
                 : std::optional<nlohmann::json>(std::nullopt);
    });
    assert(!rejected.has_value());
    auto afterReject = db.GetJson("u");
    assert(afterReject.has_value() && (*afterReject)["gen"] == 5 &&
           (*afterReject)["v"] == "keep");

    // Accept writes the mutated value; the mutator sees the current state.
    auto applied = db.UpdateJson("u", [](std::optional<nlohmann::json> current) {
      auto next = *current;
      next["gen"] = next.value("gen", 0) + 1;
      return std::optional<nlohmann::json>(next);
    });
    assert(applied.has_value() && (*applied)["gen"] == 6);
    auto afterApply = db.GetJson("u");
    assert(afterApply.has_value() && (*afterApply)["gen"] == 6);

    // Clearing = returning an explicit empty object (matches Clear()).
    db.UpdateJson("u", [](std::optional<nlohmann::json>) {
      return std::optional<nlohmann::json>(nlohmann::json::object());
    });
    auto cleared = db.GetJson("u");
    assert(cleared.has_value() && cleared->empty());

    db.Close();
    std::cout << "[ActorLifecycle] b02_update_json_atomic_semantics ok\n";
  }

  // ── 13) B04: sidecar-move failure on the Open path rolls back the moved
  // database; every original byte stays at its live path, and no new live
  // database is created ──
  {
    const auto dir = MakeDbDir("bm-b04-sidecar-fail-open-path");
    const auto dbPath = dir / "sidecar.db";
    const auto walPath = dir / (dbPath.wstring() + L"-wal");
    const auto shmPath = dir / (dbPath.wstring() + L"-shm");

    const std::string originalDb = "sidecar-fail-corrupt-db-payload-0123";
    const std::string originalWal = "sidecar-fail-wal-bytes-abcdef";
    const std::string originalShm = "sidecar-fail-shm-bytes-123456";
    WriteAllBytes(dbPath, originalDb);
    WriteAllBytes(walPath, originalWal);
    WriteAllBytes(shmPath, originalShm);

    echo::storage::SetQuarantineRenameHookForTest(&FailSidecarTargetRenameHook);
    echo::storage::Database db;
    bool failedWithRollback = false;
    try {
      db.Open(dbPath);  // db rename succeeds, wal/shm sidecar moves fail
    } catch (const std::runtime_error& e) {
      failedWithRollback = MessageContains(e, "sqlite_recovery_quarantine_failed");
    }
    echo::storage::SetQuarantineRenameHookForTest(nullptr);
    assert(failedWithRollback);
    ExpectNotAccepting(db);

    // Rollback completed: db AND both sidecars are byte-for-byte intact at
    // their original live paths, and nothing was quarantined or removed.
    assert(ReadAllBytes(dbPath) == originalDb);
    assert(ReadAllBytes(walPath) == originalWal);
    assert(ReadAllBytes(shmPath) == originalShm);
    AssertNoQuarantineLeftovers(dir);

    // Recovery still possible via a fresh Open: quarantine now succeeds and
    // the fresh live database coexists with a COMPLETE backup set.
    bool sawBackupDb = false;
    bool sawBackupWal = false;
    bool sawBackupShm = false;
    {
      echo::storage::Database db2;
      db2.Open(dbPath);
      db2.Initialize();
      db2.SetJson("fresh", nlohmann::json{{"ok", true}});
      assert(db2.GetJson("fresh").has_value());
      db2.Close();
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      const std::string name = entry.path().filename().string();
      if (name.rfind("sidecar.db.invalid-", 0) != 0) continue;
      if (name.find("-wal") != std::string::npos) {
        sawBackupWal = true;
        assert(ReadAllBytes(entry.path()) == originalWal);
      } else if (name.find("-shm") != std::string::npos) {
        sawBackupShm = true;
        assert(ReadAllBytes(entry.path()) == originalShm);
      } else {
        sawBackupDb = true;
        assert(ReadAllBytes(entry.path()) == originalDb);
      }
    }
    assert(sawBackupDb && sawBackupWal && sawBackupShm);
    std::cout << "[ActorLifecycle] b04_sidecar_failure_open_path_rolls_back ok\n";
  }

  // ── 14) B04: when even the rollback fails, the error reports the exact
  // preserved backup paths and every byte remains on disk ──
  {
    const auto dir = MakeDbDir("bm-b04-rollback-fail");
    const auto dbPath = dir / "rollback.db";
    const auto walPath = dir / (dbPath.wstring() + L"-wal");
    const auto shmPath = dir / (dbPath.wstring() + L"-shm");

    // Use an actual committed WAL, not junk text: SQLite might replay valid
    // orphan frames if the next Open creates an empty main db.
    sqlite3* writer = nullptr;
    assert(sqlite3_open(dbPath.string().c_str(), &writer) == SQLITE_OK);
    assert(sqlite3_exec(writer,
                        "PRAGMA journal_mode=WAL; CREATE TABLE wal_payload(v TEXT); "
                        "INSERT INTO wal_payload VALUES('old-account-data');",
                        nullptr, nullptr, nullptr) == SQLITE_OK);
    const std::string originalWal = ReadAllBytes(walPath);
    const std::string originalShm = ReadAllBytes(shmPath);
    assert(!originalWal.empty() && !originalShm.empty());
    assert(sqlite3_close(writer) == SQLITE_OK);
    const std::string originalDb = "rollback-fail-db-payload-fedcba987654";
    WriteAllBytes(dbPath, originalDb);
    WriteAllBytes(walPath, originalWal);
    WriteAllBytes(shmPath, originalShm);

    // Call 1 (db -> backup) succeeds; every later rename (the wal sidecar
    // move AND the rollback of the db) fails.
    ResetFailAfterFirstCallHook();
    echo::storage::SetQuarantineRenameHookForTest(&FailAfterFirstCallRenameHook);
    echo::storage::Database db;
    std::string failureMessage;
    try {
      db.Open(dbPath);
    } catch (const std::runtime_error& e) {
      failureMessage = e.what();
    }
    echo::storage::SetQuarantineRenameHookForTest(nullptr);
    assert(failureMessage.find("sqlite_recovery_rollback_failed") != std::string::npos);

    // The error must name the exact backup path where the moved database
    // bytes are preserved; that path must exist and hold the original bytes.
    const std::size_t atPos = failureMessage.find("preserved at: ");
    assert(atPos != std::string::npos);
    const std::size_t start = atPos + std::strlen("preserved at: ");
    const std::size_t end = failureMessage.find(';', start);
    assert(end != std::string::npos);
    const std::filesystem::path reportedBackup = failureMessage.substr(start, end - start);
    assert(std::filesystem::exists(reportedBackup));
    assert(ReadAllBytes(reportedBackup) == originalDb);

    // Sidecars were never moved and never removed: still byte-for-byte
    // intact at their live paths. The live db path is empty only because the
    // db bytes now live at the reported backup path (nothing was destroyed).
    assert(ReadAllBytes(walPath) == originalWal);
    assert(ReadAllBytes(shmPath) == originalShm);
    assert(!std::filesystem::exists(dbPath));

    // A later Open MUST NOT create a blank main db while valid orphan WAL
    // frames remain live. Fail closed, preserve all bytes, name both paths.
    {
      echo::storage::Database db2;
      std::string message;
      try {
        db2.Open(dbPath);
      } catch (const std::runtime_error& e) {
        message = e.what();
      }
      assert(message.find("sqlite_recovery_orphaned_sidecars") != std::string::npos);
      assert(message.find(walPath.string()) != std::string::npos);
      assert(message.find(shmPath.string()) != std::string::npos);
      ExpectNotAccepting(db2);
    }
    assert(!std::filesystem::exists(dbPath));
    assert(ReadAllBytes(reportedBackup) == originalDb);
    assert(ReadAllBytes(walPath) == originalWal);
    assert(ReadAllBytes(shmPath) == originalShm);

    // Explicit, operator-controlled repair: put the intact sidecars beside
    // the reported original backup, then create a fresh live db. Do not auto-
    // discard or auto-replay orphan WAL bytes on the failed Open.
    const auto backupWal = std::filesystem::path(reportedBackup.wstring() + L"-wal");
    const auto backupShm = std::filesystem::path(reportedBackup.wstring() + L"-shm");
    std::filesystem::rename(walPath, backupWal);
    std::filesystem::rename(shmPath, backupShm);
    {
      echo::storage::Database db2;
      db2.Open(dbPath);
      db2.Initialize();
      assert(db2.ExecuteQuery("SELECT name FROM sqlite_master WHERE name='wal_payload';").empty());
      db2.Close();
    }
    assert(ReadAllBytes(reportedBackup) == originalDb);
    assert(ReadAllBytes(backupWal) == originalWal);
    assert(ReadAllBytes(backupShm) == originalShm);
    std::cout << "[ActorLifecycle] b04_rollback_failure_orphan_wal_guard ok\n";
  }

  // ── 15) B04: Initialize recovery with junk WAL/SHM sidecars present at
  // Open time. SQLite can change or remove sidecars while first probing the
  // corrupt main file; this fixture does NOT prove preservation of their
  // pre-open bytes. Tests 13/14 inject sidecar failures on the pre-open path
  // through the same QuarantineWithSidecars implementation. Here we assert
  // only that initialize-time recovery preserves the corrupt main database
  // byte-for-byte and produces a working live database. Other real WAL and
  // concurrent-handle scenarios still require separate validation.
  {
    const auto dir = MakeDbDir("bm-b04-sidecar-fail-init-path");
    const auto dbPath = dir / "initsidecar.db";
    const auto walPath = dir / (dbPath.wstring() + L"-wal");
    const auto shmPath = dir / (dbPath.wstring() + L"-shm");

    const std::string headerOnly =
        std::string("SQLite format 3\0", 16) + "GARBAGEGARBAGE";
    WriteAllBytes(dbPath, headerOnly);
    WriteAllBytes(walPath, "init-path-wal-bytes-aabbcc");
    WriteAllBytes(shmPath, "init-path-shm-bytes-ddeeff");

    echo::storage::Database db;
    db.Open(dbPath);  // header check passes; corruption surfaces lazily
    db.Initialize();  // recovery quarantines the corrupt db, creates a fresh one
    db.SetJson("recovered", nlohmann::json{{"ok", true}});
    assert(db.GetJson("recovered").has_value());
    assert(ReadAllBytes(dbPath) != headerOnly);  // live path is a fresh db
    db.Close();

    bool sawBackupDb = false;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      const std::string name = entry.path().filename().string();
      if (name.rfind("initsidecar.db.invalid-", 0) != 0) continue;
      if (name.find("-wal") == std::string::npos &&
          name.find("-shm") == std::string::npos) {
        sawBackupDb = ReadAllBytes(entry.path()) == headerOnly;
      }
    }
    assert(sawBackupDb);  // corrupt db preserved byte-for-byte in quarantine
    std::cout << "[ActorLifecycle] b04_init_path_quarantines_db_sidecars_sqlite_managed ok\n";
  }

#if defined(_WIN32)
  // ── 16) A transient header-read failure must preserve a healthy database. ──
  {
    const auto dir = MakeDbDir("bm-header-read-sharing-failure");
    const auto path = dir / "healthy.db";
    {
      echo::storage::Database db;
      db.Open(path);
      db.Initialize();
      db.SetJson("preserved", nlohmann::json{{"value", 42}});
      db.Close();
    }
    const auto originalBytes = ReadAllBytes(path);
    assert(originalBytes.substr(0, 16) == std::string("SQLite format 3\0", 16));
    bool rejected = false;
    {
      RenameAllowedReadDeniedHandle held(path);
      assert(held.ok());
      echo::storage::Database db;
      try {
        db.Open(path);
      } catch (const std::runtime_error& error) {
        rejected = MessageContains(error, "sqlite_header_read_failed");
      }
      db.Close();
    }
    std::cerr << "[ActorLifecycle] healthy_header_read_fault rejected=" << rejected
              << " original_bytes=" << originalBytes.size()
              << " live_bytes=" << ReadAllBytes(path).size() << '\n';
    assert(rejected && "unreadable healthy header must fail without quarantine");
    assert(ReadAllBytes(path) == originalBytes);
    AssertNoQuarantineLeftovers(dir);
    {
      echo::storage::Database reopened;
      reopened.Open(path);
      reopened.Initialize();
      const auto saved = reopened.GetJson("preserved");
      assert(saved.has_value() && saved->at("value") == 42);
      reopened.Close();
    }
    std::cout << "[ActorLifecycle] healthy_header_read_failure_preserves_database ok\n";
  }
#endif

  // ── 17) JSON encoding failure must not retain a prepared statement/DB. ──
  {
    const nlohmann::json invalidUtf8 = {{"bad", std::string(1, '\xff')}};
    auto memoryAfterFailure = [&](bool cache) {
      const auto before = sqlite3_memory_used();
      echo::storage::Database db;
      db.Open(MakeDbDir("bm-json-encoding-failure") / "encoding.db");
      db.Initialize();
      assert(sqlite3_memory_used() > before && "SQLite allocation counter must be active");
      bool rejected = false;
      try {
        if (cache) {
          db.PutApiCache("invalid", invalidUtf8, 1234567890);
        } else {
          db.SetJson("invalid", invalidUtf8);
        }
      } catch (const nlohmann::json::type_error& error) {
        rejected = error.id == 316;
      }
      assert(rejected && "invalid UTF-8 must be rejected by JSON serialization");
      // A rejected value must not write a row or break later valid writes.
      if (cache) {
        assert(!db.GetApiCache("invalid", 0).has_value());
        db.PutApiCache("valid", nlohmann::json{{"ok", true}}, 1234567890);
        assert(db.GetApiCache("valid", 0)->at("ok") == true);
      } else {
        assert(!db.GetJson("invalid").has_value());
        db.SetJson("valid", nlohmann::json{{"ok", true}});
        assert(db.GetJson("valid")->at("ok") == true);
      }
      db.Close();
      return sqlite3_memory_used() - before;
    };
    const auto setJsonRetained = memoryAfterFailure(false);
    const auto cacheRetained = memoryAfterFailure(true);
    std::cerr << "[ActorLifecycle] json_encoding_failure retained_bytes set_json="
              << setJsonRetained << " api_cache=" << cacheRetained << '\n';
    assert(setJsonRetained == 0 && cacheRetained == 0);
    std::cout << "[ActorLifecycle] json_encoding_failure_releases_statements ok\n";
  }

  std::cout << "[ActorLifecycle] all ok\n";
  return 0;
}
