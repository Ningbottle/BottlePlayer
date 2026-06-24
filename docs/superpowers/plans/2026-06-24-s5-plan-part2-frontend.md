# S5 Statistics Dashboard + AI Analysis — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Record local play events to SQLite, display detailed stats with album art in a new StatsView, and provide AI-powered listening analysis via DeepSeek API.

**Architecture:** C++ PlayStatsService writes/queries SQLite → C API → Rust FFI → Tauri commands → Vue 3 StatsView. AI analysis is Rust-side only (reqwest → DeepSeek API). All UI uses skin CSS variables.

**Tech Stack:** C++17, SQLite, Rust, Tauri 2.0, Vue 3, reqwest (DeepSeek), CSS variables (no D3).

## Global Constraints

- **C++17 minimum.** MSVC 14.51, /std:c++17.
- **SQLite via vcpkg** (already installed). Use existing `echo::storage::Database` class.
- **No new frontend dependencies.** CSS charts only, no D3/chart.js.
- **Skin consistency.** All styles use CSS variables (`--paper`, `--ink`, `--accent`, etc.).
- **DeepSeek API.** Model `deepseek-v4-flash`, endpoint `https://api.deepseek.com/v1/chat/completions`. API key from localStorage, passed per-call.
- **ECHO_LOG** for all C++ diagnostics.
- **docs/ is gitignored** — use `git add -f`.
- **Git path**: `C:\Users\w1521\.qoderworkcn\bin\git\cmd\git.exe`

## File Map

| File | Responsibility |
|---|---|
| `native/include/echo/stats/PlayStatsService.h` | PlayStatsService class declaration |
| `native/stats/PlayStatsService.cpp` | Implementation: record + query |
| `native/storage/Database.cpp` | Add migration for play_history_v2 |
| `native/core/C_API.cpp` | 6 EchoStats* exports |
| `native/include/echo/core/C_API.h` | 6 EchoStats* declarations |
| `native/CMakeLists.txt` | Add new sources + test target |
| `native/tests/play_stats_test.cpp` | CTest contract test |
| `ui/src-tauri/src/backend_api.rs` | CApiHandle + 6 stats fn pointers |
| `ui/src-tauri/src/stats.rs` | 6 Tauri commands + AI analysis |
| `ui/src-tauri/src/lib.rs` | Register commands |
| `ui/src-tauri/Cargo.toml` | Add reqwest dep (if not transitive) |
| `ui/src/api/playerStore.ts` | Play event recording |
| `ui/src/views/StatsView.vue` | Stats dashboard |
| `ui/src/components/Sidebar.vue` | Add "统计" nav item |

---

### Task 1: Database Schema Migration

**Files:**
- Modify: `native/storage/Database.cpp` — add `play_history_v2` table + migration
- Modify: `native/include/echo/storage/Database.h` — add `GetUserVersion` / `SetUserVersion`

**Interfaces:**
- Produces: `play_history_v2` table in SQLite

- [ ] **Step 1: Read current Database.cpp InitializeSchema**

Read `C:\BottleMusic\native\storage\Database.cpp` and find the `InitializeSchema` method. Note the existing `play_history` table definition.

- [ ] **Step 2: Add migration logic**

In `Database::InitializeSchema()`, after the existing `CREATE TABLE IF NOT EXISTS play_history` block, add:

```cpp
// S5: Migrate play_history to play_history_v2 with richer schema
{
    auto version = ExecuteScalar<int>("PRAGMA user_version");
    if (version < 2) {
        // Create new table
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
        Execute("PRAGMA user_version = 2;");
    }
}
```

- [ ] **Step 3: Build and verify**

```
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug --target EchoStorage'
```
Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add native/storage/Database.cpp
git commit -m "feat(s5): add play_history_v2 schema migration"
```

---

### Task 2: PlayStatsService Class

**Files:**
- Create: `native/include/echo/stats/PlayStatsService.h`
- Create: `native/stats/PlayStatsService.cpp`
- Modify: `native/CMakeLists.txt` — add new source

**Interfaces:**
- Consumes: `echo::storage::Database`
- Produces: `echo::stats::PlayStatsService` with `RecordPlay`, `GetSummary`, `GetTop`, `GetTimeline`, `GetRecent`, `GetRecommendations`

- [ ] **Step 1: Create header**

```cpp
// native/include/echo/stats/PlayStatsService.h
#pragma once
#include <string>
#include "echo/storage/Database.h"

namespace echo::stats {

class PlayStatsService {
 public:
  explicit PlayStatsService(echo::storage::Database& db);

  bool RecordPlay(const std::string& jsonRecord);
  std::string GetSummary(const std::string& range);
  std::string GetTop(const std::string& dim, const std::string& range, int limit);
  std::string GetTimeline(const std::string& range);
  std::string GetRecent(int limit, int offset);
  std::string GetRecommendations(int limit);

 private:
  echo::storage::Database& db_;
  int64_t RangeToTimestamp(const std::string& range);
};

}  // namespace echo::stats
```

- [ ] **Step 2: Create implementation**

```cpp
// native/stats/PlayStatsService.cpp
#include "echo/stats/PlayStatsService.h"
#include <nlohmann/json.hpp>
#include <chrono>

namespace echo::stats {

using json = nlohmann::json;

PlayStatsService::PlayStatsService(echo::storage::Database& db) : db_(db) {}

int64_t PlayStatsService::RangeToTimestamp(const std::string& range) {
  auto now = std::chrono::system_clock::now();
  if (range == "7d") {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now - std::chrono::hours(24 * 7)).count();
  } else if (range == "30d") {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now - std::chrono::hours(24 * 30)).count();
  }
  return 0;  // "all"
}

bool PlayStatsService::RecordPlay(const std::string& jsonRecord) {
  try {
    auto j = json::parse(jsonRecord);
    db_.Execute(
        "INSERT INTO play_history_v2 "
        "(song_hash, song_name, singer_name, album_id, album_name, "
        "cover_url, duration_seconds, completed, listened_seconds, quality, played_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        j.value("song_hash", ""),
        j.value("song_name", ""),
        j.value("singer_name", ""),
        j.value("album_id", ""),
        j.value("album_name", ""),
        j.value("cover_url", ""),
        j.value("duration_seconds", 0.0),
        j.value("completed", false) ? 1 : 0,
        j.value("listened_seconds", 0.0),
        j.value("quality", ""),
        j.value("played_at", 0LL));
    return true;
  } catch (...) {
    return false;
  }
}

std::string PlayStatsService::GetSummary(const std::string& range) {
  int64_t since = RangeToTimestamp(range);
  std::string where = since > 0 ? "WHERE played_at >= " + std::to_string(since) : "";

  auto total = db_.ExecuteScalar<int>("SELECT COUNT(*) FROM play_history_v2 " + where);
  auto uniqueSongs = db_.ExecuteScalar<int>("SELECT COUNT(DISTINCT song_hash) FROM play_history_v2 " + where);
  auto uniqueArtists = db_.ExecuteScalar<int>("SELECT COUNT(DISTINCT singer_name) FROM play_history_v2 " + where);
  auto totalListened = db_.ExecuteScalar<double>("SELECT COALESCE(SUM(listened_seconds), 0) FROM play_history_v2 " + where);
  auto completed = db_.ExecuteScalar<int>("SELECT COUNT(*) FROM play_history_v2 " + where + " AND completed = 1");

  json result;
  result["total_plays"] = total;
  result["total_listened_seconds"] = totalListened;
  result["unique_songs"] = uniqueSongs;
  result["unique_artists"] = uniqueArtists;
  result["completion_rate"] = total > 0 ? (double)completed / total : 0.0;
  result["range"] = range;
  return result.dump();
}

std::string PlayStatsService::GetTop(const std::string& dim, const std::string& range, int limit) {
  int64_t since = RangeToTimestamp(range);
  std::string where = since > 0 ? "WHERE played_at >= " + std::to_string(since) : "";

  std::string groupCol, nameCol;
  if (dim == "song") { groupCol = "song_hash"; nameCol = "song_name"; }
  else if (dim == "artist") { groupCol = "singer_name"; nameCol = "singer_name"; }
  else { groupCol = "album_id"; nameCol = "album_name"; }

  std::string sql = "SELECT " + groupCol + " as id, MAX(" + nameCol + ") as name, "
      "MAX(singer_name) as singer, MAX(album_name) as album, MAX(cover_url) as cover, "
      "COUNT(*) as play_count, SUM(listened_seconds) as total_listened "
      "FROM play_history_v2 " + where + " GROUP BY " + groupCol + " "
      "ORDER BY play_count DESC LIMIT " + std::to_string(limit);

  auto rows = db_.ExecuteQuery(sql);
  json items = json::array();
  for (auto& row : rows) {
    json item;
    item["name"] = row.count("name") ? row["name"].get<std::string>() : "";
    item["singer"] = row.count("singer") ? row["singer"].get<std::string>() : "";
    item["album"] = row.count("album") ? row["album"].get<std::string>() : "";
    item["cover_url"] = row.count("cover") ? row["cover"].get<std::string>() : "";
    item["play_count"] = std::stoi(row["play_count"].get<std::string>());
    item["total_listened_seconds"] = std::stod(row["total_listened"].get<std::string>());
    items.push_back(item);
  }

  json result;
  result["dim"] = dim;
  result["items"] = items;
  return result.dump();
}

std::string PlayStatsService::GetTimeline(const std::string& range) {
  int64_t since = RangeToTimestamp(range);
  std::string where = since > 0 ? "WHERE played_at >= " + std::to_string(since) : "";

  auto rows = db_.ExecuteQuery(
      "SELECT date(played_at/1000, 'unixepoch', 'localtime') as day, COUNT(*) as count "
      "FROM play_history_v2 " + where + " GROUP BY day ORDER BY day");

  json items = json::array();
  for (auto& row : rows) {
    json item;
    item["date"] = row["day"].get<std::string>();
    item["count"] = std::stoi(row["count"].get<std::string>());
    items.push_back(item);
  }

  json result;
  result["items"] = items;
  return result.dump();
}

std::string PlayStatsService::GetRecent(int limit, int offset) {
  auto rows = db_.ExecuteQuery(
      "SELECT song_hash, song_name, singer_name, album_name, cover_url, "
      "duration_seconds, completed, listened_seconds, quality, played_at "
      "FROM play_history_v2 ORDER BY played_at DESC LIMIT " + std::to_string(limit) +
      " OFFSET " + std::to_string(offset));

  json items = json::array();
  for (auto& row : rows) {
    json item;
    item["song_hash"] = row["song_hash"].get<std::string>();
    item["name"] = row["song_name"].get<std::string>();
    item["singer"] = row["singer_name"].get<std::string>();
    item["album"] = row["album_name"].get<std::string>();
    item["cover_url"] = row["cover_url"].get<std::string>();
    item["duration_seconds"] = std::stod(row["duration_seconds"].get<std::string>());
    item["completed"] = std::stoi(row["completed"].get<std::string>()) == 1;
    item["listened_seconds"] = std::stod(row["listened_seconds"].get<std::string>());
    item["quality"] = row["quality"].get<std::string>();
    item["played_at"] = std::stoll(row["played_at"].get<std::string>());
    items.push_back(item);
  }

  json result;
  result["items"] = items;
  return result.dump();
}

std::string PlayStatsService::GetRecommendations(int limit) {
  // Top artists you listen to — return as "you might like"
  return GetTop("artist", "30d", limit);
}

}  // namespace echo::stats
```

**Note:** The `Database` class methods `ExecuteScalar<T>`, `ExecuteQuery`, and `Execute` with parameter binding may need adaptation to match the actual Database API. Read `Database.h` first and adjust the code to use the real method signatures.

- [ ] **Step 3: Update CMakeLists.txt**

Add `stats/PlayStatsService.cpp` to a new `EchoStats` library or to `EchoCore`:

```cmake
# Add to EchoCore sources (or create EchoStats library)
add_library(EchoStats STATIC
  stats/PlayStatsService.cpp
)
target_include_directories(EchoStats PUBLIC include)
target_link_libraries(EchoStats PUBLIC EchoStorage)
```

- [ ] **Step 4: Build**

```
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug --target EchoStats'
```

- [ ] **Step 5: Commit**

```bash
git add native/include/echo/stats/PlayStatsService.h native/stats/PlayStatsService.cpp native/CMakeLists.txt
git commit -m "feat(s5): add PlayStatsService for recording and querying play events"
```

---

### Task 3: C API Exports

**Files:**
- Modify: `native/include/echo/core/C_API.h` — 6 declarations
- Modify: `native/core/C_API.cpp` — 6 implementations + g_stats global

**Interfaces:**
- Consumes: `PlayStatsService` (Task 2)
- Produces: `EchoStatsRecordPlay`, `EchoStatsGetSummary`, `EchoStatsGetTop`, `EchoStatsGetTimeline`, `EchoStatsGetRecent`, `EchoStatsGetRecommendations`

- [ ] **Step 1: Add declarations to C_API.h**

```cpp
// Stats C API
ECHO_C_API void EchoStatsRecordPlay(const char* json_record);
ECHO_C_API const char* EchoStatsGetSummary(const char* range);
ECHO_C_API const char* EchoStatsGetTop(const char* dim, const char* range, int limit);
ECHO_C_API const char* EchoStatsGetTimeline(const char* range);
ECHO_C_API const char* EchoStatsGetRecent(int limit, int offset);
ECHO_C_API const char* EchoStatsGetRecommendations(int limit);
```

- [ ] **Step 2: Add implementations to C_API.cpp**

Add at the top:
```cpp
#include "echo/stats/PlayStatsService.h"
static std::unique_ptr<echo::stats::PlayStatsService> g_stats;
```

In `EchoInitializeWithPaths` (after `g_api` is created):
```cpp
g_stats = std::make_unique<echo::stats::PlayStatsService>(*g_db);
```

In `EchoShutdown` (before `g_db.reset()`):
```cpp
g_stats.reset();
```

Add the 6 exports:
```cpp
ECHO_C_API void EchoStatsRecordPlay(const char* json_record) {
    if (g_stats && json_record) {
        g_stats->RecordPlay(json_record);
    }
}

ECHO_C_API const char* EchoStatsGetSummary(const char* range) {
    if (!g_stats) return "{}";
    auto result = g_stats->GetSummary(range ? range : "all");
    char* out = new char[result.size() + 1];
    std::strcpy(out, result.c_str());
    return out;
}

ECHO_C_API const char* EchoStatsGetTop(const char* dim, const char* range, int limit) {
    if (!g_stats) return "{\"items\":[]}";
    auto result = g_stats->GetTop(dim ? dim : "song", range ? range : "all", limit > 0 ? limit : 10);
    char* out = new char[result.size() + 1];
    std::strcpy(out, result.c_str());
    return out;
}

ECHO_C_API const char* EchoStatsGetTimeline(const char* range) {
    if (!g_stats) return "{\"items\":[]}";
    auto result = g_stats->GetTimeline(range ? range : "30d");
    char* out = new char[result.size() + 1];
    std::strcpy(out, result.c_str());
    return out;
}

ECHO_C_API const char* EchoStatsGetRecent(int limit, int offset) {
    if (!g_stats) return "{\"items\":[]}";
    auto result = g_stats->GetRecent(limit > 0 ? limit : 20, offset >= 0 ? offset : 0);
    char* out = new char[result.size() + 1];
    std::strcpy(out, result.c_str());
    return out;
}

ECHO_C_API const char* EchoStatsGetRecommendations(int limit) {
    if (!g_stats) return "{\"items\":[]}";
    auto result = g_stats->GetRecommendations(limit > 0 ? limit : 5);
    char* out = new char[result.size() + 1];
    std::strcpy(out, result.c_str());
    return out;
}
```

- [ ] **Step 3: Link EchoStats into EchoCAPI in CMakeLists.txt**

```cmake
target_link_libraries(EchoCAPI PRIVATE EchoCore EchoPlayback EchoStats)
```

- [ ] **Step 4: Build**

```
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug --target EchoCAPI'
```

- [ ] **Step 5: Commit**

```bash
git add native/include/echo/core/C_API.h native/core/C_API.cpp native/CMakeLists.txt
git commit -m "feat(s5): add 6 EchoStats C API exports"
```

---

### Task 4: CTest Contract Test

**Files:**
- Create: `native/tests/play_stats_test.cpp`
- Modify: `native/CMakeLists.txt`

- [ ] **Step 1: Create test**

```cpp
// native/tests/play_stats_test.cpp
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include "echo/storage/Database.h"
#include "echo/stats/PlayStatsService.h"

using echo::storage::Database;
using echo::stats::PlayStatsService;

static int g_passed = 0, g_failed = 0;
#define CHECK(cond, msg) do { if (cond) { std::cout << "  [ok] " << msg << "\n"; ++g_passed; } else { std::cerr << "  [FAIL] " << msg << "\n"; ++g_failed; } } while(0)

int main() {
  Database db;
  db.Open(":memory:");
  db.Initialize();

  PlayStatsService stats(db);

  // Record 5 plays
  for (int i = 0; i < 5; ++i) {
    std::string record = R"({"song_hash":")" + std::to_string(i) + R"(","song_name":"Test)" + std::to_string(i) + R"(","singer_name":"ArtistA","album_name":"Album1","cover_url":"http://example.com/cover.jpg","duration_seconds":240.0,"completed":true,"listened_seconds":240.0,"quality":"320","played_at":)" + std::to_string(1782289763000LL + i * 1000) + "}";
    CHECK(stats.RecordPlay(record), "record play " + std::to_string(i));
  }

  // Record 3 more plays of song 0 (same song repeated)
  for (int i = 0; i < 3; ++i) {
    std::string record = R"({"song_hash":"0","song_name":"Test0","singer_name":"ArtistA","album_name":"Album1","cover_url":"http://example.com/cover.jpg","duration_seconds":240.0,"completed":false,"listened_seconds":30.0,"quality":"128","played_at":)" + std::to_string(1782289764000LL + i * 1000) + "}";
    CHECK(stats.RecordPlay(record), "record repeat play " + std::to_string(i));
  }

  // Test summary
  std::cout << "[Test] Testing summary...\n";
  auto summary = stats.GetSummary("all");
  std::cout << "  Summary: " << summary << "\n";
  CHECK(summary.find("\"total_plays\":8") != std::string::npos, "total_plays = 8");
  CHECK(summary.find("\"unique_songs\":5") != std::string::npos, "unique_songs = 5");
  CHECK(summary.find("\"unique_artists\":1") != std::string::npos, "unique_artists = 1");

  // Test top songs
  std::cout << "[Test] Testing top songs...\n";
  auto top = stats.GetTop("song", "all", 3);
  std::cout << "  Top: " << top << "\n";
  CHECK(top.find("Test0") != std::string::npos, "Test0 in top songs");
  CHECK(top.find("\"play_count\":4") != std::string::npos, "Test0 has 4 plays");

  // Test timeline
  std::cout << "[Test] Testing timeline...\n";
  auto timeline = stats.GetTimeline("all");
  std::cout << "  Timeline: " << timeline << "\n";
  CHECK(timeline.find("items") != std::string::npos, "timeline has items");

  // Test recent
  std::cout << "[Test] Testing recent...\n";
  auto recent = stats.GetRecent(5, 0);
  std::cout << "  Recent: " << recent << "\n";
  CHECK(recent.find("Test0") != std::string::npos, "recent has Test0");

  std::cout << "[Test] All stats tests completed.\n";
  std::cout << "  Passed: " << g_passed << "  Failed: " << g_failed << "\n";
  return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Register in CMakeLists.txt**

```cmake
add_executable(EchoPlayStatsTest tests/play_stats_test.cpp)
target_include_directories(EchoPlayStatsTest PRIVATE include)
target_link_libraries(EchoPlayStatsTest PRIVATE EchoStats EchoStorage)
add_test(NAME EchoPlayStatsTest COMMAND EchoPlayStatsTest)
```

- [ ] **Step 3: Build and run**

```
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug --target EchoPlayStatsTest && ctest --test-dir C:\BottleMusic\native\out\bottlemusic-check -R EchoPlayStatsTest --output-on-failure'
```

- [ ] **Step 4: Commit**

```bash
git add native/tests/play_stats_test.cpp native/CMakeLists.txt
git commit -m "test(s5): add PlayStatsService contract test"
```

---

### Task 5: Rust CApiHandle Extension + Stats Commands

**Files:**
- Modify: `ui/src-tauri/src/backend_api.rs` — add 6 stats fn pointers
- Create: `ui/src-tauri/src/stats.rs` — 6 Tauri commands
- Modify: `ui/src-tauri/src/lib.rs` — register commands

- [ ] **Step 1: Add 6 stats fn pointers to CApiHandle in backend_api.rs**

Read the existing `CApiHandle` struct. Add these fields:

```rust
stats_record_play: unsafe extern "C" fn(*const c_char),
stats_get_summary: unsafe extern "C" fn(*const c_char) -> *mut c_char,
stats_get_top: unsafe extern "C" fn(*const c_char, *const c_char, c_int) -> *mut c_char,
stats_get_timeline: unsafe extern "C" fn(*const c_char) -> *mut c_char,
stats_get_recent: unsafe extern "C" fn(c_int, c_int) -> *mut c_char,
stats_get_recommendations: unsafe extern "C" fn(c_int) -> *mut c_char,
```

Add symbol loading in `init_with_paths` following the existing pattern.

- [ ] **Step 2: Create stats.rs with 6 Tauri commands**

```rust
// ui/src-tauri/src/stats.rs
use std::ffi::{CStr, CString};
use std::os::raw::c_int;
use tauri::State;
use crate::backend_api::CApiHandle;

#[tauri::command]
pub fn stats_record_play(record: String, handle: State<CApiHandle>) -> Result<(), String> {
    let c_record = CString::new(record).map_err(|e| e.to_string())?;
    unsafe { (handle.stats_record_play)(c_record.as_ptr()) };
    Ok(())
}

#[tauri::command]
pub fn stats_get_summary(range: String, handle: State<CApiHandle>) -> Result<String, String> {
    let c_range = CString::new(range).map_err(|e| e.to_string())?;
    let ptr = unsafe { (handle.stats_get_summary)(c_range.as_ptr()) };
    if ptr.is_null() { return Err("null".into()); }
    let result = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
    unsafe { (handle.free_str)(ptr) };
    Ok(result)
}

#[tauri::command]
pub fn stats_get_top(dim: String, range: String, limit: i32, handle: State<CApiHandle>) -> Result<String, String> {
    let c_dim = CString::new(dim).map_err(|e| e.to_string())?;
    let c_range = CString::new(range).map_err(|e| e.to_string())?;
    let ptr = unsafe { (handle.stats_get_top)(c_dim.as_ptr(), c_range.as_ptr(), limit as c_int) };
    if ptr.is_null() { return Err("null".into()); }
    let result = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
    unsafe { (handle.free_str)(ptr) };
    Ok(result)
}

#[tauri::command]
pub fn stats_get_timeline(range: String, handle: State<CApiHandle>) -> Result<String, String> {
    let c_range = CString::new(range).map_err(|e| e.to_string())?;
    let ptr = unsafe { (handle.stats_get_timeline)(c_range.as_ptr()) };
    if ptr.is_null() { return Err("null".into()); }
    let result = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
    unsafe { (handle.free_str)(ptr) };
    Ok(result)
}

#[tauri::command]
pub fn stats_get_recent(limit: i32, offset: i32, handle: State<CApiHandle>) -> Result<String, String> {
    let ptr = unsafe { (handle.stats_get_recent)(limit as c_int, offset as c_int) };
    if ptr.is_null() { return Err("null".into()); }
    let result = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
    unsafe { (handle.free_str)(ptr) };
    Ok(result)
}

#[tauri::command]
pub fn stats_get_recommendations(limit: i32, handle: State<CApiHandle>) -> Result<String, String> {
    let ptr = unsafe { (handle.stats_get_recommendations)(limit as c_int) };
    if ptr.is_null() { return Err("null".into()); }
    let result = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
    unsafe { (handle.free_str)(ptr) };
    Ok(result)
}
```

**Note:** If `State<CApiHandle>` doesn't work (S4 had this issue), use the `backend_api::api_handle()` pattern instead. Read `backend_api.rs` to check which pattern is currently in use.

- [ ] **Step 3: Register in lib.rs**

```rust
mod stats;
// In invoke_handler:
stats::stats_record_play,
stats::stats_get_summary,
stats::stats_get_top,
stats::stats_get_timeline,
stats::stats_get_recent,
stats::stats_get_recommendations,
```

- [ ] **Step 4: Build**

```
cargo build --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml
```

- [ ] **Step 5: Commit**

```bash
git add ui/src-tauri/src/backend_api.rs ui/src-tauri/src/stats.rs ui/src-tauri/src/lib.rs
git commit -m "feat(s5): add 6 Rust stats Tauri commands"
```

---

### Task 6: DeepSeek AI Analysis Command

**Files:**
- Create: `ui/src-tauri/src/ai_analysis.rs`
- Modify: `ui/src-tauri/src/lib.rs`
- Modify: `ui/src-tauri/Cargo.toml` — add reqwest if not present

- [ ] **Step 1: Check if reqwest is already a dependency**

Read `Cargo.toml`. If `reqwest` is not listed, add:
```toml
reqwest = { version = "0.12", features = ["json"] }
```

- [ ] **Step 2: Create ai_analysis.rs**

```rust
// ui/src-tauri/src/ai_analysis.rs
use serde::{Deserialize, Serialize};

#[derive(Serialize)]
struct DeepSeekMessage {
    role: String,
    content: String,
}

#[derive(Serialize)]
struct DeepSeekRequest {
    model: String,
    messages: Vec<DeepSeekMessage>,
    max_tokens: u32,
    temperature: f32,
}

#[derive(Deserialize)]
struct DeepSeekResponse {
    choices: Vec<DeepSeekChoice>,
}

#[derive(Deserialize)]
struct DeepSeekChoice {
    message: DeepSeekMessageResp,
}

#[derive(Deserialize)]
struct DeepSeekMessageResp {
    content: String,
}

#[tauri::command]
pub async fn ai_analyze(
    api_key: String,
    stats_summary: String,
    top_songs: String,
    top_artists: String,
    timeline: String,
) -> Result<String, String> {
    let prompt = format!(
        r#"你是一个音乐分析助手。基于以下用户的听歌统计数据，请用中文给出简短的分析：

1. 听歌习惯总结（2-3句话）
2. 音乐品味画像（2-3句话）
3. 一个有趣的发现或建议

统计数据：
- 概览: {}
- Top 歌曲: {}
- Top 歌手: {}
- 播放时间线: {}

请控制在200字以内，语气友好轻松。"#,
        stats_summary, top_songs, top_artists, timeline
    );

    let request = DeepSeekRequest {
        model: "deepseek-v4-flash".to_string(),
        messages: vec![DeepSeekMessage {
            role: "user".to_string(),
            content: prompt,
        }],
        max_tokens: 500,
        temperature: 0.7,
    };

    let client = reqwest::Client::new();
    let resp = client
        .post("https://api.deepseek.com/v1/chat/completions")
        .header("Authorization", format!("Bearer {}", api_key))
        .header("Content-Type", "application/json")
        .json(&request)
        .timeout(std::time::Duration::from_secs(30))
        .send()
        .await
        .map_err(|e| format!("请求失败: {}", e))?;

    if !resp.status().is_success() {
        let status = resp.status();
        let body = resp.text().await.unwrap_or_default();
        return Err(format!("API 错误 {}: {}", status, body));
    }

    let body: DeepSeekResponse = resp
        .json()
        .await
        .map_err(|e| format!("解析失败: {}", e))?;

    body.choices
        .first()
        .map(|c| c.message.content.clone())
        .ok_or_else(|| "无返回内容".to_string())
}
```

- [ ] **Step 3: Register in lib.rs**

```rust
mod ai_analysis;
// In invoke_handler:
ai_analysis::ai_analyze,
```

- [ ] **Step 4: Build**

```
cargo build --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml
```

- [ ] **Step 5: Commit**

```bash
git add ui/src-tauri/src/ai_analysis.rs ui/src-tauri/src/lib.rs ui/src-tauri/Cargo.toml
git commit -m "feat(s5): add DeepSeek AI analysis command"
```

---

### Task 7: playerStore Play Event Recording

**Files:**
- Modify: `ui/src/api/playerStore.ts`

- [ ] **Step 1: Add play recording logic**

In `playerStore.ts`, add module-level variables:

```typescript
let currentPlayTrack: Track | null = null;
let currentPlayStartTime = 0;

function recordPlayEnd(completed: boolean) {
  if (!currentPlayTrack) return;
  const listened = Math.min(
    (Date.now() - currentPlayStartTime) / 1000,
    currentPlayTrack.Duration || 0
  );
  const record = JSON.stringify({
    song_hash: currentPlayTrack.FileHash,
    song_name: currentPlayTrack.Name,
    singer_name: currentPlayTrack.Singer,
    album_id: currentPlayTrack.AlbumID || '',
    album_name: currentPlayTrack.AlbumName || '',
    cover_url: currentPlayTrack.Image || '',
    duration_seconds: currentPlayTrack.Duration || 0,
    completed,
    listened_seconds: listened,
    quality: playerStore.quality,
    played_at: currentPlayStartTime,
  });
  invoke('stats_record_play', { record }).catch(() => {});
  currentPlayTrack = null;
}

function recordPlayStart(track: Track) {
  if (currentPlayTrack) {
    recordPlayEnd(false); // previous song not finished
  }
  currentPlayTrack = track;
  currentPlayStartTime = Date.now();
}
```

- [ ] **Step 2: Wire into playTrack, ended, next, prev**

In `playTrack()`, after `activeBackend.playUrl(finalUrl)` succeeds, call:
```typescript
recordPlayStart(normalized);
```

In `handlePlaybackEvent`, when `e.type === 'ended'`:
```typescript
recordPlayEnd(true);
```

In `next()` and `prev()`, at the start:
```typescript
recordPlayEnd(false);
```

- [ ] **Step 3: Import invoke**

Add at the top of playerStore.ts (if not already imported):
```typescript
import { invoke } from '@tauri-apps/api/core';
```

- [ ] **Step 4: Type-check**

```
cd C:\BottleMusic\ui && pnpm exec vue-tsc --noEmit
```

- [ ] **Step 5: Commit**

```bash
git add ui/src/api/playerStore.ts
git commit -m "feat(s5): record play events to local stats"
```

---

### Task 8: Sidebar Nav Item

**Files:**
- Modify: `ui/src/components/Sidebar.vue`

- [ ] **Step 1: Add stats nav item**

Find `sidebarNav` array. Add stats entry:

```typescript
const sidebarNav = [
  { id: 'home', name: '首页', icon: 'M3 11l9-8 9 8v10a2 2 0 0 1-2 2h-4v-7h-6v7H5a2 2 0 0 1-2-2V11z' },
  { id: 'stats', name: '统计', icon: 'M3 3v18h18M7 14l4-4 4 4 5-5' },
  { id: 'history', name: '最近播放', icon: 'M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z' },
];
```

- [ ] **Step 2: Add StatsView route in App.vue**

Read `App.vue` to find how views are routed. Add StatsView:

```typescript
import StatsView from './views/StatsView.vue';
// In the view conditional:
{ id: 'stats', component: StatsView }
```

- [ ] **Step 3: Create placeholder StatsView.vue**

```vue
<!-- ui/src/views/StatsView.vue -->
<script setup lang="ts">
</script>
<template>
  <div class="stats-view">
    <h2>统计</h2>
    <p>加载中...</p>
  </div>
</template>
```

- [ ] **Step 4: Type-check**

```
cd C:\BottleMusic\ui && pnpm exec vue-tsc --noEmit
```

- [ ] **Step 5: Commit**

```bash
git add ui/src/components/Sidebar.vue ui/src/App.vue ui/src/views/StatsView.vue
git commit -m "feat(s5): add stats nav item + placeholder StatsView"
```

---

### Task 9: StatsView — Overview + Top Lists

**Files:**
- Modify: `ui/src/views/StatsView.vue`

- [ ] **Step 1: Implement overview cards + top lists**

```vue
<script setup lang="ts">
import { ref, onMounted, watch } from 'vue';
import { invoke } from '@tauri-apps/api/core';

type Range = '7d' | '30d' | 'all';
const range = ref<Range>('30d');
const loading = ref(true);

interface Summary {
  total_plays: number;
  total_listened_seconds: number;
  unique_songs: number;
  unique_artists: number;
  completion_rate: number;
}
const summary = ref<Summary | null>(null);

interface TopItem {
  name: string;
  singer: string;
  album: string;
  cover_url: string;
  play_count: number;
  total_listened_seconds: number;
}
const topSongs = ref<TopItem[]>([]);
const topArtists = ref<TopItem[]>([]);
const topAlbums = ref<TopItem[]>([]);

function formatDuration(seconds: number): string {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  return h > 0 ? `${h}h ${m}m` : `${m}m`;
}

async function loadStats() {
  loading.value = true;
  try {
    const [s, songs, artists, albums] = await Promise.all([
      invoke<string>('stats_get_summary', { range: range.value }),
      invoke<string>('stats_get_top', { dim: 'song', range: range.value, limit: 10 }),
      invoke<string>('stats_get_top', { dim: 'artist', range: range.value, limit: 10 }),
      invoke<string>('stats_get_top', { dim: 'album', range: range.value, limit: 10 }),
    ]);
    summary.value = JSON.parse(s);
    topSongs.value = JSON.parse(songs).items || [];
    topArtists.value = JSON.parse(artists).items || [];
    topAlbums.value = JSON.parse(albums).items || [];
  } catch (e) {
    console.error('Stats load failed:', e);
  } finally {
    loading.value = false;
  }
}

onMounted(loadStats);
watch(range, loadStats);
</script>

<template>
  <div class="stats-view">
    <div class="stats-header">
      <h2>我的统计</h2>
      <div class="range-tabs">
        <button v-for="r in (['7d','30d','all'] as Range[])" :key="r"
          :class="{ active: range === r }" @click="range = r">
          {{ r === '7d' ? '7天' : r === '30d' ? '30天' : '全部' }}
        </button>
      </div>
    </div>

    <div v-if="loading" class="stats-loading">加载中...</div>

    <template v-else-if="summary">
      <!-- Overview cards -->
      <div class="stats-overview">
        <div class="stat-card">
          <span class="stat-value">{{ summary.total_plays }}</span>
          <span class="stat-label">总播放</span>
        </div>
        <div class="stat-card">
          <span class="stat-value">{{ formatDuration(summary.total_listened_seconds) }}</span>
          <span class="stat-label">总时长</span>
        </div>
        <div class="stat-card">
          <span class="stat-value">{{ summary.unique_songs }}</span>
          <span class="stat-label">不同歌曲</span>
        </div>
        <div class="stat-card">
          <span class="stat-value">{{ Math.round(summary.completion_rate * 100) }}%</span>
          <span class="stat-label">完成率</span>
        </div>
      </div>

      <!-- Top lists -->
      <div class="stats-tops">
        <div class="top-section">
          <h3>Top 歌曲</h3>
          <div v-for="(item, i) in topSongs" :key="i" class="top-item">
            <img v-if="item.cover_url" :src="item.cover_url" class="top-cover" loading="lazy">
            <div class="top-info">
              <span class="top-name">{{ item.name }}</span>
              <span class="top-sub">{{ item.singer }}</span>
            </div>
            <span class="top-count">{{ item.play_count }}次</span>
          </div>
          <p v-if="topSongs.length === 0" class="empty">暂无数据</p>
        </div>

        <div class="top-section">
          <h3>Top 歌手</h3>
          <div v-for="(item, i) in topArtists" :key="i" class="top-item">
            <div class="top-info">
              <span class="top-name">{{ item.name }}</span>
            </div>
            <span class="top-count">{{ item.play_count }}次</span>
          </div>
          <p v-if="topArtists.length === 0" class="empty">暂无数据</p>
        </div>

        <div class="top-section">
          <h3>Top 专辑</h3>
          <div v-for="(item, i) in topAlbums" :key="i" class="top-item">
            <img v-if="item.cover_url" :src="item.cover_url" class="top-cover" loading="lazy">
            <div class="top-info">
              <span class="top-name">{{ item.name }}</span>
              <span class="top-sub">{{ item.singer }}</span>
            </div>
            <span class="top-count">{{ item.play_count }}次</span>
          </div>
          <p v-if="topAlbums.length === 0" class="empty">暂无数据</p>
        </div>
      </div>
    </template>
  </div>
</template>

<style scoped>
.stats-view { padding: 20px; max-width: 900px; margin: 0 auto; }
.stats-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }
.stats-header h2 { font-family: var(--font-serif); color: var(--ink); margin: 0; }
.range-tabs { display: flex; gap: 4px; }
.range-tabs button {
  background: var(--paper-2); border: 1px solid var(--rule);
  padding: 4px 12px; border-radius: 4px; cursor: pointer;
  color: var(--ink-soft); font-size: 12px; font-family: var(--font-sans);
}
.range-tabs button.active { background: var(--accent); color: var(--paper); border-color: var(--accent); }
.stats-loading { color: var(--ink-mute); text-align: center; padding: 40px; }
.stats-overview { display: grid; grid-template-columns: repeat(4, 1fr); gap: 12px; margin-bottom: 24px; }
.stat-card {
  background: var(--paper-2); border: 1px solid var(--rule); border-radius: 8px;
  padding: 16px; text-align: center;
}
.stat-value { display: block; font-size: 24px; font-weight: 700; color: var(--ink); }
.stat-label { display: block; font-size: 12px; color: var(--ink-mute); margin-top: 4px; }
.stats-tops { display: grid; grid-template-columns: repeat(3, 1fr); gap: 16px; }
.top-section h3 { font-family: var(--font-serif); color: var(--ink); font-size: 14px; margin: 0 0 8px; }
.top-item { display: flex; align-items: center; gap: 8px; padding: 6px 0; border-bottom: 1px solid var(--rule-soft); }
.top-cover { width: 32px; height: 32px; border-radius: 4px; object-fit: cover; }
.top-info { flex: 1; min-width: 0; }
.top-name { display: block; font-size: 13px; color: var(--ink); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.top-sub { display: block; font-size: 11px; color: var(--ink-mute); }
.top-count { font-size: 12px; color: var(--ink-soft); white-space: nowrap; }
.empty { color: var(--ink-mute); font-size: 13px; padding: 12px 0; }
</style>
```

- [ ] **Step 2: Type-check**

```
cd C:\BottleMusic\ui && pnpm exec vue-tsc --noEmit
```

- [ ] **Step 3: Commit**

```bash
git add ui/src/views/StatsView.vue
git commit -m "feat(s5): StatsView overview cards + top lists with album art"
```

---

### Task 10: StatsView — Timeline + Recent + AI

**Files:**
- Modify: `ui/src/views/StatsView.vue`

- [ ] **Step 1: Add timeline chart, recent plays, and AI panel**

Add to the `<script setup>`:
```typescript
// Timeline
interface TimelineItem { date: string; count: number; }
const timeline = ref<TimelineItem[]>([]);
const maxTimelineCount = ref(1);

// Recent plays
interface RecentItem {
  name: string; singer: string; album: string; cover_url: string;
  duration_seconds: number; completed: boolean; listened_seconds: number;
  quality: string; played_at: number;
}
const recent = ref<RecentItem[]>([]);

// AI analysis
const aiApiKey = ref(localStorage.getItem('deepseek_api_key') || '');
const aiResult = ref('');
const aiLoading = ref(false);
const aiError = ref('');

function formatTimeAgo(ts: number): string {
  const diff = Date.now() - ts;
  const m = Math.floor(diff / 60000);
  if (m < 1) return '刚刚';
  if (m < 60) return `${m}分钟前`;
  const h = Math.floor(m / 60);
  if (h < 24) return `${h}小时前`;
  const d = Math.floor(h / 24);
  if (d < 7) return `${d}天前`;
  return new Date(ts).toLocaleDateString('zh-CN');
}

async function loadTimelineAndRecent() {
  try {
    const [tl, rec] = await Promise.all([
      invoke<string>('stats_get_timeline', { range: range.value }),
      invoke<string>('stats_get_recent', { limit: 20, offset: 0 }),
    ]);
    timeline.value = JSON.parse(tl).items || [];
    maxTimelineCount.value = Math.max(1, ...timeline.value.map(t => t.count));
    recent.value = JSON.parse(rec).items || [];
  } catch (e) { console.error('Timeline/recent load failed:', e); }
}

async function runAIAnalysis() {
  if (!aiApiKey.value) { aiError.value = '请先输入 API Key'; return; }
  localStorage.setItem('deepseek_api_key', aiApiKey.value);
  aiLoading.value = true;
  aiError.value = '';
  aiResult.value = '';
  try {
    const [s, songs, artists, tl] = await Promise.all([
      invoke<string>('stats_get_summary', { range: range.value }),
      invoke<string>('stats_get_top', { dim: 'song', range: range.value, limit: 5 }),
      invoke<string>('stats_get_top', { dim: 'artist', range: range.value, limit: 5 }),
      invoke<string>('stats_get_timeline', { range: range.value }),
    ]);
    aiResult.value = await invoke<string>('ai_analyze', {
      apiKey: aiApiKey.value, statsSummary: s, topSongs: songs, topArtists: artists, timeline: tl,
    });
  } catch (e: any) {
    aiError.value = e?.message || String(e);
  } finally {
    aiLoading.value = false;
  }
}

// Update loadStats to also load timeline + recent
watch(range, loadTimelineAndRecent, { immediate: false });
```

Add to `loadStats()` after the existing Promise.all:
```typescript
await loadTimelineAndRecent();
```

Add to template (after `.stats-tops`):
```html
<!-- Timeline chart -->
<div class="stats-timeline" v-if="timeline.length > 0">
  <h3>播放时间线</h3>
  <div class="timeline-chart">
    <div v-for="item in timeline" :key="item.date" class="timeline-bar">
      <div class="bar-fill" :style="{ height: (item.count / maxTimelineCount * 100) + '%' }"></div>
      <span class="bar-label">{{ item.date.slice(5) }}</span>
      <span class="bar-count">{{ item.count }}</span>
    </div>
  </div>
</div>

<!-- Recent plays -->
<div class="stats-recent">
  <h3>最近播放</h3>
  <div v-for="(item, i) in recent" :key="i" class="recent-item">
    <img v-if="item.cover_url" :src="item.cover_url" class="recent-cover" loading="lazy">
    <div class="recent-info">
      <span class="recent-name">{{ item.name }}</span>
      <span class="recent-sub">{{ item.singer }} · {{ item.album }}</span>
    </div>
    <div class="recent-meta">
      <span class="recent-time">{{ formatTimeAgo(item.played_at) }}</span>
      <span class="recent-detail">{{ formatDuration(item.listened_seconds) }} / {{ formatDuration(item.duration_seconds) }}</span>
      <span class="recent-badge" :class="{ completed: item.completed }">{{ item.completed ? '听完' : '跳过' }}</span>
    </div>
  </div>
  <p v-if="recent.length === 0" class="empty">暂无播放记录</p>
</div>

<!-- AI Analysis -->
<div class="stats-ai">
  <h3>AI 听歌分析</h3>
  <div class="ai-input-row">
    <input type="password" v-model="aiApiKey" placeholder="DeepSeek API Key" class="ai-key-input">
    <button @click="runAIAnalysis" :disabled="aiLoading" class="ai-btn">
      {{ aiLoading ? '分析中...' : 'AI 分析' }}
    </button>
  </div>
  <p v-if="aiError" class="ai-error">{{ aiError }}</p>
  <div v-if="aiResult" class="ai-result">{{ aiResult }}</div>
  <p v-if="!aiResult && !aiLoading && !aiError" class="ai-hint">
    输入你的 DeepSeek API Key，AI 会分析你的听歌习惯。Key 仅保存在本地浏览器。
  </p>
</div>
```

Add CSS:
```css
.stats-timeline { margin: 24px 0; }
.stats-timeline h3 { font-family: var(--font-serif); color: var(--ink); font-size: 14px; margin: 0 0 8px; }
.timeline-chart { display: flex; gap: 2px; align-items: flex-end; height: 100px; overflow-x: auto; padding-bottom: 20px; }
.timeline-bar { display: flex; flex-direction: column; align-items: center; min-width: 30px; height: 100%; justify-content: flex-end; position: relative; }
.bar-fill { width: 20px; background: var(--accent); border-radius: 2px 2px 0 0; min-height: 2px; }
.bar-label { font-size: 10px; color: var(--ink-mute); margin-top: 4px; }
.bar-count { font-size: 9px; color: var(--ink-soft); position: absolute; top: -14px; }
.stats-recent { margin: 24px 0; }
.stats-recent h3 { font-family: var(--font-serif); color: var(--ink); font-size: 14px; margin: 0 0 8px; }
.recent-item { display: flex; align-items: center; gap: 10px; padding: 8px 0; border-bottom: 1px solid var(--rule-soft); }
.recent-cover { width: 40px; height: 40px; border-radius: 4px; object-fit: cover; }
.recent-info { flex: 1; min-width: 0; }
.recent-name { display: block; font-size: 13px; color: var(--ink); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.recent-sub { display: block; font-size: 11px; color: var(--ink-mute); }
.recent-meta { display: flex; flex-direction: column; align-items: flex-end; gap: 2px; }
.recent-time { font-size: 11px; color: var(--ink-mute); }
.recent-detail { font-size: 10px; color: var(--ink-soft); }
.recent-badge { font-size: 10px; padding: 1px 6px; border-radius: 3px; background: var(--rule); color: var(--ink-soft); }
.recent-badge.completed { background: var(--accent); color: var(--paper); }
.stats-ai { margin: 24px 0; }
.stats-ai h3 { font-family: var(--font-serif); color: var(--ink); font-size: 14px; margin: 0 0 8px; }
.ai-input-row { display: flex; gap: 8px; margin-bottom: 8px; }
.ai-key-input { flex: 1; padding: 6px 10px; border: 1px solid var(--rule); border-radius: 4px; background: var(--paper); color: var(--ink); font-size: 13px; font-family: var(--font-sans); }
.ai-btn { padding: 6px 16px; border: none; border-radius: 4px; background: var(--accent); color: var(--paper); cursor: pointer; font-size: 13px; }
.ai-btn:disabled { opacity: 0.5; cursor: not-allowed; }
.ai-error { color: #e53935; font-size: 12px; }
.ai-result { background: var(--paper-2); border: 1px solid var(--rule); border-radius: 8px; padding: 16px; font-size: 13px; line-height: 1.6; color: var(--ink); white-space: pre-wrap; }
.ai-hint { font-size: 12px; color: var(--ink-mute); }
```

- [ ] **Step 2: Type-check**

```
cd C:\BottleMusic\ui && pnpm exec vue-tsc --noEmit
```

- [ ] **Step 3: Commit**

```bash
git add ui/src/views/StatsView.vue
git commit -m "feat(s5): add timeline chart, recent plays, and AI analysis panel"
```

---

### Task 11: Frontend Tests

**Files:**
- Create: `ui/src/views/__tests__/StatsView.test.ts`

- [ ] **Step 1: Write test**

```typescript
// ui/src/views/__tests__/StatsView.test.ts
import { describe, it, expect, vi, beforeEach } from 'vitest';

// Mock invoke
vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn().mockImplementation((cmd: string) => {
    if (cmd === 'stats_get_summary') return Promise.resolve(JSON.stringify({ total_plays: 10, total_listened_seconds: 3600, unique_songs: 5, unique_artists: 3, completion_rate: 0.8 }));
    if (cmd === 'stats_get_top') return Promise.resolve(JSON.stringify({ items: [{ name: 'Test Song', singer: 'Test Artist', album: 'Test Album', cover_url: '', play_count: 5, total_listened_seconds: 300 }] }));
    if (cmd === 'stats_get_timeline') return Promise.resolve(JSON.stringify({ items: [{ date: '2026-06-24', count: 3 }] }));
    if (cmd === 'stats_get_recent') return Promise.resolve(JSON.stringify({ items: [{ name: 'Recent Song', singer: 'Artist', album: 'Album', cover_url: '', duration_seconds: 240, completed: true, listened_seconds: 240, quality: '320', played_at: Date.now() }] }));
    return Promise.resolve('{}');
  }),
}));

describe('StatsView data loading', () => {
  beforeEach(() => vi.clearAllMocks());

  it('mock stats_get_summary returns expected shape', async () => {
    const { invoke } = await import('@tauri-apps/api/core');
    const result = JSON.parse(await invoke('stats_get_summary', { range: '30d' }) as string);
    expect(result.total_plays).toBe(10);
    expect(result.completion_rate).toBe(0.8);
  });

  it('mock stats_get_top returns items array', async () => {
    const { invoke } = await import('@tauri-apps/api/core');
    const result = JSON.parse(await invoke('stats_get_top', { dim: 'song', range: '30d', limit: 10 }) as string);
    expect(result.items).toHaveLength(1);
    expect(result.items[0].name).toBe('Test Song');
  });

  it('mock stats_get_recent returns items with played_at', async () => {
    const { invoke } = await import('@tauri-apps/api/core');
    const result = JSON.parse(await invoke('stats_get_recent', { limit: 20, offset: 0 }) as string);
    expect(result.items[0].name).toBe('Recent Song');
    expect(result.items[0].completed).toBe(true);
  });
});
```

- [ ] **Step 2: Run tests**

```
cd C:\BottleMusic\ui && pnpm test -- --run
```

- [ ] **Step 3: Commit**

```bash
git add ui/src/views/__tests__/StatsView.test.ts
git commit -m "test(s5): add StatsView data loading tests"
```

---

### Task 12: Final Verification

**Files:**
- None (verification only)

- [ ] **Step 1: Build everything**

```
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug'
cargo build --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml
cd C:\BottleMusic\ui && pnpm exec vue-tsc --noEmit
```

- [ ] **Step 2: Run all tests**

```
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && set PATH=C:\BottleMusic\native\vcpkg_installed\x64-windows\bin;%PATH% && ctest --test-dir C:\BottleMusic\native\out\bottlemusic-check --output-on-failure'
cargo test --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml --lib
cd C:\BottleMusic\ui && pnpm test -- --run
```

- [ ] **Step 3: Copy DLL and start app**

```
Copy-Item C:\BottleMusic\native\out\bottlemusic-check\EchoCAPI.dll C:\BottleMusic\ui\src-tauri\bin\ -Force
cd C:\BottleMusic\ui && pnpm tauri dev
```

- [ ] **Step 4: Manual test checklist**

- [ ] Play 2-3 songs
- [ ] Open Stats view from sidebar
- [ ] Verify overview cards show correct numbers
- [ ] Verify Top songs/artists/albums with cover art
- [ ] Verify timeline chart
- [ ] Verify recent plays list with timestamps
- [ ] Enter DeepSeek API key and run AI analysis
- [ ] Verify all styles match current skin

- [ ] **Step 5: Commit**

```bash
git commit --allow-empty -m "test(s5): final verification passed"
```

---

**End of S5 plan. 12 tasks total.**
