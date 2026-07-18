// Storage Actor lifecycle + concurrency contract tests (P1 #1).
// Requires actor serialization and lock-held Close/Submit protocol (design r2).

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "echo/storage/Database.h"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace {

std::filesystem::path MakeDbDir(const char* name) {
  auto dir = std::filesystem::temp_directory_path() / name;
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
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

}  // namespace

int main() {
#if defined(_MSC_VER)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
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

  std::cout << "[ActorLifecycle] all ok\n";
  return 0;
}
