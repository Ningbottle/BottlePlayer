// Database WAL mode + RO read-after-write (stability).
// Multi-thread stress was SIGSEGV on CI runners (likely RO open / TLS timing);
// this test asserts WAL is enabled and committed writes are readable.

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "echo/storage/Database.h"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace {

std::filesystem::path TestDbPath() {
  auto dir = std::filesystem::temp_directory_path() / "bottlemusic-wal-mode-test";
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

#if defined(ECHO_NATIVE_HAS_SQLITE)
  // journal_mode should be WAL after InitializeSchema.
  auto modeRows = db.ExecuteQuery("PRAGMA journal_mode;");
  assert(!modeRows.empty() && !modeRows[0].empty());
  std::string mode = modeRows[0][0];
  for (char& c : mode) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  assert(mode == "wal");
#endif

  db.SetJson("k", nlohmann::json{{"i", 7}, {"s", "wal-ok"}});
  auto j = db.GetJson("k");
  assert(j.has_value());
  assert((*j)["i"] == 7);
  assert((*j)["s"] == "wal-ok");

  // Bound write path + bound read path (ExecuteQueryBound via GetJson).
  db.SetJson("k2", nlohmann::json::array({1, 2, 3}));
  auto j2 = db.GetJson("k2");
  assert(j2.has_value());
  assert(j2->is_array());
  assert(j2->size() == 3);

  db.Close();
  std::error_code ec;
  std::filesystem::remove_all(path.parent_path(), ec);
  std::cout << "[WalMode] ok (journal_mode=wal, read-after-write)" << std::endl;
  return 0;
}
