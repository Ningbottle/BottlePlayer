// WAL multi-thread read: after a committed write on the writer connection,
// other threads' TLS RO connections can open and read the key.
// Avoids concurrent write+read stress that can fault if SQLite is built
// single-threaded or if RO open races with heavy writers on CI.

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "echo/storage/Database.h"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace {

std::filesystem::path TestDbPath() {
  auto dir = std::filesystem::temp_directory_path() / "bottlemusic-wal-concurrency";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  return dir / "test.db";
}

}  // namespace

int main() {
#if defined(_MSC_VER)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

  const auto path = TestDbPath();
  echo::storage::Database db;
  db.Open(path);
  db.Initialize();

  // Committed write on main/writer path first.
  db.SetJson("k", nlohmann::json{{"i", 42}, {"msg", "hello-wal"}});

  std::atomic<int> ok_count{0};
  std::vector<std::thread> readers;
  readers.reserve(4);
  for (int r = 0; r < 4; ++r) {
    readers.emplace_back([&db, &ok_count]() {
      // Each thread gets its own TLS RO connection via ReadDb().
      for (int attempt = 0; attempt < 50; ++attempt) {
        auto j = db.GetJson("k");
        if (j.has_value() && (*j)["i"] == 42 && (*j)["msg"] == "hello-wal") {
          ok_count.fetch_add(1, std::memory_order_relaxed);
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    });
  }
  for (auto& t : readers) {
    t.join();
  }

  assert(ok_count.load() == 4);

  // Additional write then single-thread read (same TLS as main).
  db.SetJson("k2", nlohmann::json{{"ok", true}});
  auto j2 = db.GetJson("k2");
  assert(j2.has_value());
  assert((*j2)["ok"] == true);

  db.Close();
  std::error_code ec;
  std::filesystem::remove_all(path.parent_path(), ec);
  std::cout << "[WalConcurrency] ok (4 readers saw committed write)" << std::endl;
  return 0;
}
