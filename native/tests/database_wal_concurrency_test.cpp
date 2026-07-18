// WAL concurrent read after write: writers serialize; readers use TLS RO.
// Documents that after a write completes, a barrier + fresh read sees data
// (same process; RO connections opened after commit observe the commit).

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
  std::filesystem::remove_all(dir);
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

  // Seed key from main thread.
  db.SetJson("seed", nlohmann::json{{"v", 1}});
  auto seed = db.GetJson("seed");
  assert(seed.has_value());
  assert((*seed)["v"] == 1);

  std::atomic<int> writes{0};
  std::atomic<int> visible{0};

  // Writer thread
  std::thread writer([&]() {
    for (int i = 0; i < 20; ++i) {
      db.SetJson("k", nlohmann::json{{"i", i}});
      writes.fetch_add(1, std::memory_order_release);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  });

  // Readers: after observing write count, GetJson should eventually see key.
  std::vector<std::thread> readers;
  for (int r = 0; r < 4; ++r) {
    readers.emplace_back([&]() {
      for (int attempt = 0; attempt < 100; ++attempt) {
        if (writes.load(std::memory_order_acquire) > 0) {
          auto j = db.GetJson("k");
          if (j.has_value() && j->contains("i")) {
            visible.fetch_add(1, std::memory_order_relaxed);
            return;
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    });
  }

  writer.join();
  for (auto& t : readers) t.join();

  // All readers eventually observe at least one committed write.
  assert(visible.load() == 4);

  // Final value is last write.
  auto final = db.GetJson("k");
  assert(final.has_value());
  assert((*final)["i"] == 19);

  db.Close();
  std::filesystem::remove_all(path.parent_path());
  std::cout << "[WalConcurrency] ok (writes=20, readers_saw=4)" << std::endl;
  return 0;
}
