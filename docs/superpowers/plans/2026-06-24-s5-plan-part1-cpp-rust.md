# S5 Implementation Plan — Part 1: C++ Data Layer + Rust FFI

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the C++ PlayStatsService + SQLite schema migration + C API exports + Rust FFI commands + DeepSeek AI analysis.

**Architecture:** C++ PlayStatsService writes/queries play events in SQLite. 6 C API exports bridge to Rust. Rust adds 6 Tauri commands + 1 AI analysis command (DeepSeek API via reqwest).

**Tech Stack:** C++17, SQLite, nlohmann-json, Rust, Tauri 2.0, reqwest.

## Global Constraints

- **C++17 minimum.** MSVC 14.51, /std:c++17, /EHsc.
- **SQLite via vcpkg.** Triplet `x64-windows`.
- **ECHO_LOG(component, message)** for all C++ diagnostics.
- **JSON via nlohmann-json** for all C++ → Rust data exchange.
- **C API returns heap-allocated `char*`**, freed by caller via `EchoFreeString`.
- **Rust commands access `CAPI_HANDLE` static** (same pattern as S4 playback commands).
- **DeepSeek model**: `deepseek-v4-flash`. Endpoint: `https://api.deepseek.com/v1/chat/completions`.
- **API key**: user-provided, stored in localStorage, passed per-call to Rust.
- **No new C++ or frontend dependencies.** reqwest is already a transitive dep via Tauri.

## File Map

| File | Responsibility |
|---|---|
| `native/include/echo/stats/PlayStatsService.h` | PlayStatsService class declaration |
| `native/stats/PlayStatsService.cpp` | Implementation: record + query |
| `native/storage/Database.cpp` | Add migration + ExecuteQuery method |
| `native/include/echo/storage/Database.h` | Add ExecuteQuery declaration |
| `native/include/echo/core/C_API.h` | 6 EchoStats* declarations |
| `native/core/C_API.cpp` | 6 EchoStats* implementations + g_stats |
| `native/CMakeLists.txt` | Add PlayStatsService source + test target |
| `native/tests/play_stats_test.cpp` | CTest contract test |
| `ui/src-tauri/src/backend_api.rs` | CApiHandle: 6 stats fn pointers |
| `ui/src-tauri/src/stats.rs` | 6 Tauri commands + AI analysis command |
| `ui/src-tauri/src/lib.rs` | Register stats module + commands |
| `ui/src-tauri/Cargo.toml` | Add reqwest feature if needed |

---

### Task 1: Database schema migration

**Files:**
- Modify: `native/storage/Database.cpp` — add `play_history_v2` table + migration
- Modify: `native/include/echo/storage/Database.h` — add `ExecuteQuery` method

**Interfaces:**
- Produces: `Database::ExecuteQuery(sql)` returns rows; `play_history_v2` table exists

- [ ] **Step 1: Add migration to Database::InitializeSchema**

In `Database.cpp`, find the existing `CREATE TABLE IF NOT EXISTS play_history` block. After it, add:

```cpp
// S5: Migrate play_history to play_history_v2 with rich schema
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
```

- [ ] **Step 2: Add ExecuteQuery method to Database**

In `Database.h`, add to the public interface:

```cpp
// Execute a SELECT query. Returns rows as vector of column-value maps.
// Each row is a vector<string> in column order. Caller must know column names.
std::vector<std::vector<std::string>> ExecuteQuery(const std::string& sql);
```

In `Database.cpp`, implement:

```cpp
std::vector<std::vector<std::string>> Database::ExecuteQuery(const std::string& sql) {
    std::vector<std::vector<std::string>> rows;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return rows;
    }
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
```

- [ ] **Step 3: Build and verify**

Run: `cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug --target EchoCore'`
Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add native/storage/Database.cpp native/include/echo/storage/Database.h
git commit -m "feat(s5): add play_history_v2 schema + ExecuteQuery method"
```

---

### Task 2: PlayStatsService class

**Files:**
- Create: `native/include/echo/stats/PlayStatsService.h`
- Create: `native/stats/PlayStatsService.cpp`
- Modify: `native/CMakeLists.txt` — add new source

**Interfaces:**
- Consumes: `Database::Execute` + `Database::ExecuteQuery` (Task 1)
- Produces: `PlayStatsService` with RecordPlay, GetSummary, GetTop, GetTimeline, GetRecent, GetRecommendations

- [ ] **Step 1: Create header**

```cpp
// native/include/echo/stats/PlayStatsService.h
#pragma once
#include <string>
#include "echo/storage/Database.h"

namespace echo::stats {

struct PlayRecord {
  std::string songHash;
  std::string songName;
  std::string singerName;
  std::string albumId;
  std::string albumName;
  std::string coverUrl;
  double durationSeconds = 0;
  bool completed = false;
  double listenedSeconds = 0;
  std::string quality;
  long long playedAtMs = 0;
};

class PlayStatsService {
 public:
  explicit PlayStatsService(echo::storage::Database& db);

  bool RecordPlay(const PlayRecord& record);
  std::string GetSummary(const std::string& range);
  std::string GetTop(const std::string& dim, const std::string& range, int limit);
  std::string GetTimeline(const std::string& range);
  std::string GetRecent(int limit, int offset);
  std::string GetRecommendations(int limit);

 private:
  echo::storage::Database& db_;
  long long RangeToTimestamp(const std::string& range);
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

using nlohmann::json;

PlayStatsService::PlayStatsService(echo::storage::Database& db) : db_(db) {}

long long PlayStatsService::RangeToTimestamp(const std::string& range) {
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  if (range == "7d") return now - 7LL * 24 * 3600 * 1000;
  if (range == "30d") return now - 30LL * 24 * 3600 * 1000;
  return 0; // "all"
}

bool PlayStatsService::RecordPlay(const PlayRecord& r) {
  std::string sql = "INSERT INTO play_history_v2 "
      "(song_hash, song_name, singer_name, album_id, album_name, cover_url, "
      "duration_seconds, completed, listened_seconds, quality, played_at) VALUES (" +
      "'" + r.songHash + "','" + r.songName + "','" + r.singerName + "','" +
      r.albumId + "','" + r.albumName + "','" + r.coverUrl + "'," +
      std::to_string(r.durationSeconds) + "," +
      (r.completed ? "1" : "0") + "," +
      std::to_string(r.listenedSeconds) + ",'" + r.quality + "'," +
      std::to_string(r.playedAtMs) + ")";
  return db_.Execute(sql);
}

std::string PlayStatsService::GetSummary(const std::string& range) {
  long long since = RangeToTimestamp(range);
  std::string where = since > 0 ? "WHERE played_at >= " + std::to_string(since) : "";
  auto rows = db_.ExecuteQuery(
      "SELECT COUNT(*), COALESCE(SUM(listened_seconds),0), "
      "COUNT(DISTINCT song_hash), COUNT(DISTINCT singer_name), "
      "CAST(SUM(CASE WHEN completed=1 THEN 1 ELSE 0 END) AS FLOAT) / COUNT(*) "
      "FROM play_history_v2 " + where);
  json j;
  if (!rows.empty()) {
    auto& r = rows[0];
    j["total_plays"] = std::stoi(r[0]);
    j["total_listened_seconds"] = std::stod(r[1]);
    j["unique_songs"] = std::stoi(r[2]);
    j["unique_artists"] = std::stoi(r[3]);
    j["completion_rate"] = std::stod(r[4]);
  } else {
    j["total_plays"] = 0;
    j["total_listened_seconds"] = 0;
    j["unique_songs"] = 0;
    j["unique_artists"] = 0;
    j["completion_rate"] = 0;
  }
  j["range"] = range;
  return j.dump();
}

std::string PlayStatsService::GetTop(const std::string& dim, const std::string& range, int limit) {
  long long since = RangeToTimestamp(range);
  std::string where = since > 0 ? "WHERE played_at >= " + std::to_string(since) : "";
  std::string groupCol, nameCol;
  if (dim == "song") { groupCol = "song_hash"; nameCol = "song_name, singer_name, album_name, cover_url"; }
  else if (dim == "artist") { groupCol = "singer_name"; nameCol = "singer_name"; }
  else { groupCol = "album_name"; nameCol = "album_name, singer_name, cover_url"; }

  std::string sql = "SELECT " + nameCol + ", COUNT(*) as cnt, "
      "COALESCE(SUM(listened_seconds),0) as total_sec "
      "FROM play_history_v2 " + where + " GROUP BY " + groupCol +
      " ORDER BY cnt DESC LIMIT " + std::to_string(limit);
  auto rows = db_.ExecuteQuery(sql);
  json items = json::array();
  for (auto& r : rows) {
    json item;
    if (dim == "song") {
      item["name"] = r[0]; item["singer"] = r[1]; item["album"] = r[2]; item["cover_url"] = r[3];
      item["play_count"] = std::stoi(r[4]); item["total_listened_seconds"] = std::stod(r[5]);
    } else if (dim == "artist") {
      item["name"] = r[0]; item["play_count"] = std::stoi(r[1]); item["total_listened_seconds"] = std::stod(r[2]);
    } else {
      item["name"] = r[0]; item["singer"] = r[1]; item["cover_url"] = r[2];
      item["play_count"] = std::stoi(r[3]); item["total_listened_seconds"] = std::stod(r[4]);
    }
    items.push_back(item);
  }
  json j;
  j["dim"] = dim;
  j["items"] = items;
  return j.dump();
}

std::string PlayStatsService::GetTimeline(const std::string& range) {
  long long since = RangeToTimestamp(range);
  std::string where = since > 0 ? "WHERE played_at >= " + std::to_string(since) : "";
  // Group by day: played_at / 86400000 = day number
  auto rows = db_.ExecuteQuery(
      "SELECT played_at / 86400000 as day, COUNT(*) as cnt "
      "FROM play_history_v2 " + where +
      " GROUP BY day ORDER BY day");
  json items = json::array();
  for (auto& r : rows) {
    json item;
    item["day"] = std::stoll(r[0]);
    item["play_count"] = std::stoi(r[1]);
    items.push_back(item);
  }
  json j;
  j["items"] = items;
  return j.dump();
}

std::string PlayStatsService::GetRecent(int limit, int offset) {
  auto rows = db_.ExecuteQuery(
      "SELECT song_hash, song_name, singer_name, album_name, cover_url, "
      "duration_seconds, listened_seconds, completed, quality, played_at "
      "FROM play_history_v2 ORDER BY played_at DESC LIMIT " +
      std::to_string(limit) + " OFFSET " + std::to_string(offset));
  json items = json::array();
  for (auto& r : rows) {
    json item;
    item["song_hash"] = r[0]; item["name"] = r[1]; item["singer"] = r[2];
    item["album"] = r[3]; item["cover_url"] = r[4];
    item["duration_seconds"] = std::stod(r[5]);
    item["listened_seconds"] = std::stod(r[6]);
    item["completed"] = r[7] == "1";
    item["quality"] = r[8];
    item["played_at"] = std::stoll(r[9]);
    items.push_back(item);
  }
  json j;
  j["items"] = items;
  return j.dump();
}

std::string PlayStatsService::GetRecommendations(int limit) {
  // Top artists by play count
  auto rows = db_.ExecuteQuery(
      "SELECT singer_name, COUNT(*) as cnt FROM play_history_v2 "
      "GROUP BY singer_name ORDER BY cnt DESC LIMIT " + std::to_string(limit));
  json items = json::array();
  for (auto& r : rows) {
    json item;
    item["singer"] = r[0];
    item["play_count"] = std::stoi(r[1]);
    items.push_back(item);
  }
  json j;
  j["items"] = items;
  return j.dump();
}

}  // namespace echo::stats
```

- [ ] **Step 3: Update CMakeLists.txt**

Add `stats/PlayStatsService.cpp` to EchoCore sources (or create a new EchoStats lib — simpler to add to EchoCore since it depends on Database which is in EchoStorage, already linked to EchoCore):

```cmake
# In the EchoCore source list, add:
stats/PlayStatsService.cpp
```

- [ ] **Step 4: Build**

Run: `cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug --target EchoCore'`
Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add native/include/echo/stats/PlayStatsService.h native/stats/PlayStatsService.cpp native/CMakeLists.txt
git commit -m "feat(s5): add PlayStatsService with record + query methods"
```

---

### Task 3: C API exports

**Files:**
- Modify: `native/include/echo/core/C_API.h` — 6 declarations
- Modify: `native/core/C_API.cpp` — 6 implementations + g_stats

**Interfaces:**
- Consumes: PlayStatsService (Task 2)
- Produces: 6 EchoStats* C API exports

- [ ] **Step 1: Add declarations to C_API.h**

After the playback section, add:

```cpp
// ── Stats C API ────────────────────────────────────
ECHO_C_API void EchoStatsRecordPlay(const char* json_record);
ECHO_C_API const char* EchoStatsGetSummary(const char* range);
ECHO_C_API const char* EchoStatsGetTop(const char* dim, const char* range, int limit);
ECHO_C_API const char* EchoStatsGetTimeline(const char* range);
ECHO_C_API const char* EchoStatsGetRecent(int limit, int offset);
ECHO_C_API const char* EchoStatsGetRecommendations(int limit);
```

- [ ] **Step 2: Add g_stats and implementations to C_API.cpp**

Add at the top (after g_playback):

```cpp
#include "echo/stats/PlayStatsService.h"
static std::unique_ptr<echo::stats::PlayStatsService> g_stats;
```

In `EchoInitializeWithPaths` (after g_api is created), add:

```cpp
g_stats = std::make_unique<echo::stats::PlayStatsService>(*g_db);
```

In `EchoShutdown` (after g_api.reset()), add:

```cpp
g_stats.reset();
```

At the bottom, add the 6 implementations:

```cpp
ECHO_C_API void EchoStatsRecordPlay(const char* json_record) {
    if (!g_stats || !json_record) return;
    try {
        auto j = nlohmann::json::parse(json_record);
        echo::stats::PlayRecord r;
        r.songHash = j.value("song_hash", "");
        r.songName = j.value("song_name", "");
        r.singerName = j.value("singer_name", "");
        r.albumId = j.value("album_id", "");
        r.albumName = j.value("album_name", "");
        r.coverUrl = j.value("cover_url", "");
        r.durationSeconds = j.value("duration_seconds", 0.0);
        r.completed = j.value("completed", false);
        r.listenedSeconds = j.value("listened_seconds", 0.0);
        r.quality = j.value("quality", "");
        r.playedAtMs = j.value("played_at", 0LL);
        g_stats->RecordPlay(r);
    } catch (...) {}
}

ECHO_C_API const char* EchoStatsGetSummary(const char* range) {
    if (!g_stats || !range) return _dup_str(R"({"total_plays":0})");
    return _dup_str(g_stats->GetSummary(range).c_str());
}

ECHO_C_API const char* EchoStatsGetTop(const char* dim, const char* range, int limit) {
    if (!g_stats || !dim || !range) return _dup_str(R"({"items":[]})");
    return _dup_str(g_stats->GetTop(dim, range, limit).c_str());
}

ECHO_C_API const char* EchoStatsGetTimeline(const char* range) {
    if (!g_stats || !range) return _dup_str(R"({"items":[]})");
    return _dup_str(g_stats->GetTimeline(range).c_str());
}

ECHO_C_API const char* EchoStatsGetRecent(int limit, int offset) {
    if (!g_stats) return _dup_str(R"({"items":[]})");
    return _dup_str(g_stats->GetRecent(limit, offset).c_str());
}

ECHO_C_API const char* EchoStatsGetRecommendations(int limit) {
    if (!g_stats) return _dup_str(R"({"items":[]})");
    return _dup_str(g_stats->GetRecommendations(limit).c_str());
}
```

Helper `_dup_str` (add near top if not already present):
```cpp
static const char* _dup_str(const char* s) {
    char* out = new char[std::strlen(s) + 1];
    std::strcpy(out, s);
    return out;
}
```

- [ ] **Step 3: Build EchoCAPI**

Run: `cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug --target EchoCAPI'`
Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add native/include/echo/core/C_API.h native/core/C_API.cpp
git commit -m "feat(s5): add 6 EchoStats C API exports"
```

---

### Task 4: CTest contract test

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
using echo::stats::PlayRecord;

static int g_passed = 0, g_failed = 0;
#define CHECK(cond, msg) do { if (cond) { std::cout << "  [ok] " << msg << "\n"; ++g_passed; } else { std::cerr << "  [FAIL] " << msg << "\n"; ++g_failed; } } while(0)

int main() {
  Database db;
  db.Open(":memory:");
  db.Initialize();

  PlayStatsService stats(db);

  // Record 10 plays
  for (int i = 0; i < 10; ++i) {
    PlayRecord r;
    r.songHash = "hash" + std::to_string(i % 3);
    r.songName = "Song " + std::to_string(i % 3);
    r.singerName = (i % 3 == 0) ? "Artist A" : (i % 3 == 1) ? "Artist B" : "Artist C";
    r.albumName = "Album " + std::to_string(i % 3);
    r.coverUrl = "http://example.com/cover.jpg";
    r.durationSeconds = 240.0;
    r.completed = (i % 2 == 0);
    r.listenedSeconds = 180.0;
    r.quality = "128";
    r.playedAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    CHECK(stats.RecordPlay(r), "RecordPlay " + std::to_string(i));
  }

  // Test summary
  std::string summary = stats.GetSummary("all");
  std::cout << "Summary: " << summary << "\n";
  CHECK(summary.find("\"total_plays\":10") != std::string::npos, "summary total_plays=10");
  CHECK(summary.find("\"unique_songs\":3") != std::string::npos, "summary unique_songs=3");

  // Test top songs
  std::string topSongs = stats.GetTop("song", "all", 5);
  std::cout << "Top songs: " << topSongs << "\n";
  CHECK(topSongs.find("Song 0") != std::string::npos, "top songs contains Song 0");

  // Test top artists
  std::string topArtists = stats.GetTop("artist", "all", 5);
  CHECK(topArtists.find("Artist A") != std::string::npos, "top artists contains Artist A");

  // Test timeline
  std::string timeline = stats.GetTimeline("all");
  CHECK(timeline.find("\"items\"") != std::string::npos, "timeline has items");

  // Test recent
  std::string recent = stats.GetRecent(5, 0);
  CHECK(recent.find("Song") != std::string::npos, "recent contains songs");

  // Test recommendations
  std::string recs = stats.GetRecommendations(5);
  CHECK(recs.find("Artist") != std::string::npos, "recommendations contains artists");

  std::cout << "Passed: " << g_passed << "  Failed: " << g_failed << "\n";
  return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Register in CMakeLists.txt**

```cmake
add_executable(EchoPlayStatsTest tests/play_stats_test.cpp)
target_include_directories(EchoPlayStatsTest PRIVATE include)
target_link_libraries(EchoPlayStatsTest PRIVATE EchoCore EchoStorage)
add_test(NAME EchoPlayStatsTest COMMAND EchoPlayStatsTest)
```

- [ ] **Step 3: Build and run**

Run: `cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug --target EchoPlayStatsTest && set PATH=C:\BottleMusic\native\vcpkg_installed\x64-windows\bin;%PATH% && ctest --test-dir C:\BottleMusic\native\out\bottlemusic-check -R EchoPlayStatsTest --output-on-failure'`
Expected: all checks pass.

- [ ] **Step 4: Commit**

```bash
git add native/tests/play_stats_test.cpp native/CMakeLists.txt
git commit -m "test(s5): add PlayStatsService contract test"
```

---

### Task 5: Rust CApiHandle extension

**Files:**
- Modify: `ui/src-tauri/src/backend_api.rs` — add 6 stats fn pointers + loading

- [ ] **Step 1: Add 6 fn pointer fields to CApiHandle**

After the playback fields, add:

```rust
// Stats
stats_record_play: unsafe extern "C" fn(*const c_char),
stats_get_summary: unsafe extern "C" fn(*const c_char) -> *mut c_char,
stats_get_top: unsafe extern "C" fn(*const c_char, *const c_char, c_int) -> *mut c_char,
stats_get_timeline: unsafe extern "C" fn(*const c_char) -> *mut c_char,
stats_get_recent: unsafe extern "C" fn(c_int, c_int) -> *mut c_char,
stats_get_recommendations: unsafe extern "C" fn(c_int) -> *mut c_char,
```

- [ ] **Step 2: Load symbols in init_with_paths**

After the playback symbol loading, add:

```rust
let stats_record_play_ptr = {
    let sym = lib.get(b"EchoStatsRecordPlay")
        .map_err(|e| format!("EchoStatsRecordPlay: {}", e))?;
    *sym
};
// ... repeat for all 6 ...
```

- [ ] **Step 3: Assign to CApiHandle struct**

```rust
stats_record_play: stats_record_play_ptr,
stats_get_summary: stats_get_summary_ptr,
stats_get_top: stats_get_top_ptr,
stats_get_timeline: stats_get_timeline_ptr,
stats_get_recent: stats_get_recent_ptr,
stats_get_recommendations: stats_get_recommendations_ptr,
```

- [ ] **Step 4: Build**

Run: `cargo build --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml`
Expected: clean build (only dead_code warnings).

- [ ] **Step 5: Commit**

```bash
git add ui/src-tauri/src/backend_api.rs
git commit -m "feat(s5): extend CApiHandle with 6 stats fn pointers"
```

---

### Task 6: Rust Tauri commands for stats

**Files:**
- Create: `ui/src-tauri/src/stats.rs`
- Modify: `ui/src-tauri/src/lib.rs` — register module + commands

- [ ] **Step 1: Create stats.rs**

```rust
// ui/src-tauri/src/stats.rs
use std::ffi::{CStr, CString};
use std::os::raw::c_int;
use crate::backend_api::api_handle;

#[tauri::command]
pub fn stats_record_play(record: String) -> Result<(), String> {
    let c = CString::new(record).map_err(|e| e.to_string())?;
    unsafe { (api_handle().stats_record_play)(c.as_ptr()) };
    Ok(())
}

#[tauri::command]
pub fn stats_get_summary(range: String) -> Result<String, String> {
    let c = CString::new(range).map_err(|e| e.to_string())?;
    let ptr = unsafe { (api_handle().stats_get_summary)(c.as_ptr()) };
    if ptr.is_null() { return Err("null".into()); }
    let s = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
    unsafe { (api_handle().free_str)(ptr) };
    Ok(s)
}

#[tauri::command]
pub fn stats_get_top(dim: String, range: String, limit: i32) -> Result<String, String> {
    let d = CString::new(dim).map_err(|e| e.to_string())?;
    let r = CString::new(range).map_err(|e| e.to_string())?;
    let ptr = unsafe { (api_handle().stats_get_top)(d.as_ptr(), r.as_ptr(), limit as c_int) };
    if ptr.is_null() { return Err("null".into()); }
    let s = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
    unsafe { (api_handle().free_str)(ptr) };
    Ok(s)
}

#[tauri::command]
pub fn stats_get_timeline(range: String) -> Result<String, String> {
    let c = CString::new(range).map_err(|e| e.to_string())?;
    let ptr = unsafe { (api_handle().stats_get_timeline)(c.as_ptr()) };
    if ptr.is_null() { return Err("null".into()); }
    let s = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
    unsafe { (api_handle().free_str)(ptr) };
    Ok(s)
}

#[tauri::command]
pub fn stats_get_recent(limit: i32, offset: i32) -> Result<String, String> {
    let ptr = unsafe { (api_handle().stats_get_recent)(limit as c_int, offset as c_int) };
    if ptr.is_null() { return Err("null".into()); }
    let s = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
    unsafe { (api_handle().free_str)(ptr) };
    Ok(s)
}

#[tauri::command]
pub fn stats_get_recommendations(limit: i32) -> Result<String, String> {
    let ptr = unsafe { (api_handle().stats_get_recommendations)(limit as c_int) };
    if ptr.is_null() { return Err("null".into()); }
    let s = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
    unsafe { (api_handle().free_str)(ptr) };
    Ok(s)
}
```

- [ ] **Step 2: Register in lib.rs**

Add `mod stats;` at top. Add to invoke_handler:

```rust
stats::stats_record_play,
stats::stats_get_summary,
stats::stats_get_top,
stats::stats_get_timeline,
stats::stats_get_recent,
stats::stats_get_recommendations,
```

- [ ] **Step 3: Build**

Run: `cargo build --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml`
Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add ui/src-tauri/src/stats.rs ui/src-tauri/src/lib.rs
git commit -m "feat(s5): add 6 stats Tauri commands"
```

---

### Task 7: DeepSeek AI analysis command

**Files:**
- Modify: `ui/src-tauri/src/stats.rs` — add `ai_analyze` command
- Modify: `ui/src-tauri/Cargo.toml` — ensure reqwest with json feature

- [ ] **Step 1: Check reqwest dependency**

In `Cargo.toml`, ensure reqwest is available (Tauri depends on it transitively, but we need the `json` feature). Add if not present:

```toml
[dependencies]
reqwest = { version = "0.12", features = ["json"] }
```

- [ ] **Step 2: Add ai_analyze command**

```rust
// In stats.rs, add:
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
        "你是一个音乐分析助手。基于以下用户的听歌统计数据，请用中文给出简短的分析：\n\
        1. 听歌习惯总结（2-3句话）\n\
        2. 音乐品味画像（2-3句话）\n\
        3. 一个有趣的发现或建议\n\n\
        统计数据：\n- 总播放次数: {}\n- Top 歌曲: {}\n- Top 歌手: {}\n- 播放时间线: {}\n\n\
        请控制在200字以内，语气友好轻松。",
        stats_summary, top_songs, top_artists, timeline
    );

    let req = DeepSeekRequest {
        model: "deepseek-v4-flash".to_string(),
        messages: vec![DeepSeekMessage {
            role: "user".to_string(),
            content: prompt,
        }],
        max_tokens: 500,
    };

    let client = reqwest::Client::new();
    let resp = client
        .post("https://api.deepseek.com/v1/chat/completions")
        .header("Authorization", format!("Bearer {}", api_key))
        .json(&req)
        .timeout(std::time::Duration::from_secs(30))
        .send()
        .await
        .map_err(|e| format!("DeepSeek request failed: {}", e))?;

    if !resp.status().is_success() {
        let status = resp.status();
        let body = resp.text().await.unwrap_or_default();
        return Err(format!("DeepSeek API error {}: {}", status, body));
    }

    let data: DeepSeekResponse = resp
        .json()
        .await
        .map_err(|e| format!("Failed to parse DeepSeek response: {}", e))?;

    data.choices
        .first()
        .map(|c| c.message.content.clone())
        .ok_or("No choices in DeepSeek response".into())
}
```

- [ ] **Step 3: Register ai_analyze in lib.rs invoke_handler**

Add `stats::ai_analyze,` to the invoke_handler list.

- [ ] **Step 4: Build**

Run: `cargo build --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml`
Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add ui/src-tauri/src/stats.rs ui/src-tauri/src/lib.rs ui/src-tauri/Cargo.toml
git commit -m "feat(s5): add DeepSeek AI analysis command"
```

---

### Task 8: Rust integration test

**Files:**
- Create: `ui/src-tauri/tests/stats_ffi_test.rs`

- [ ] **Step 1: Create test**

```rust
// ui/src-tauri/tests/stats_ffi_test.rs
use std::ffi::{CStr, CString};

#[test]
fn test_stats_record_and_query() {
    // Load DLL (same pattern as playback_ffi_test)
    let candidates: Vec<String> = {
        let mut v = std::env::var("ECHO_CAPI_DLL").ok().into_iter().collect::<Vec<_>>();
        v.push("../../../native/out/bottlemusic-check/EchoCAPI.dll".into());
        v.push(format!("{}/target/debug/EchoCAPI.dll", env!("CARGO_MANIFEST_DIR")));
        v
    };
    let dll_path = candidates.iter().find(|p| std::path::Path::new(p).exists())
        .expect("Could not find EchoCAPI.dll");

    unsafe {
        let lib = libloading::Library::new(dll_path).expect("Failed to load DLL");
        let init: libloading::Symbol<unsafe extern "C" fn(*const std::os::raw::c_char)> =
            lib.get(b"EchoInitializeWithPaths").unwrap();
        let tmp = std::env::temp_dir().join("bottlemusic_stats_test");
        std::fs::create_dir_all(&tmp).unwrap();
        let c_dir = CString::new(tmp.to_string_lossy().to_string()).unwrap();
        init(c_dir.as_ptr());

        let record_play: libloading::Symbol<unsafe extern "C" fn(*const std::os::raw::c_char)> =
            lib.get(b"EchoStatsRecordPlay").unwrap();
        let get_summary: libloading::Symbol<unsafe extern "C" fn(*const std::os::raw::c_char) -> *mut std::os::raw::c_char> =
            lib.get(b"EchoStatsGetSummary").unwrap();
        let free_str: libloading::Symbol<unsafe extern "C" fn(*mut std::os::raw::c_char)> =
            lib.get(b"EchoFreeString").unwrap();

        // Record a play
        let json = CString::new(r#"{"song_hash":"test1","song_name":"Test Song","singer_name":"Test Artist","duration_seconds":240.0,"completed":true,"listened_seconds":240.0,"quality":"128","played_at":1782289763000}"#).unwrap();
        record_play(json.as_ptr());

        // Query summary
        let range = CString::new("all").unwrap();
        let ptr = get_summary(range.as_ptr());
        assert!(!ptr.is_null());
        let summary = CStr::from_ptr(ptr).to_string_lossy().into_owned();
        free_str(ptr);

        println!("Summary: {}", summary);
        assert!(summary.contains("total_plays"));

        let shutdown: libloading::Symbol<unsafe extern "C" fn()> =
            lib.get(b"EchoShutdown").unwrap();
        shutdown();
    }
}
```

- [ ] **Step 2: Build and run**

Run: `cargo test --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml --test stats_ffi_test`
Expected: test passes.

- [ ] **Step 3: Commit**

```bash
git add ui/src-tauri/tests/stats_ffi_test.rs
git commit -m "test(s5): add stats FFI integration test"
```

---

**End of Part 1. Continue with Part 2 (Frontend + Polish).**
