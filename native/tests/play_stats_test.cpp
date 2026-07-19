// EchoPlayStatsTest — Contract test for the PlayStatsService C API.
// Exercises EchoStatsRecordPlay + all five EchoStatsGet* query functions,
// verifying field correctness, ordering, range filtering, and pagination.

#include <cassert>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "echo/core/C_API.h"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace {

std::filesystem::path TestDirPath(const wchar_t* name) {
  auto path = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

void RecordPlay(const nlohmann::json& j) {
  std::string s = j.dump();
  EchoStatsRecordPlay(s.c_str());
}

nlohmann::json ParseAndFree(const char* result) {
  assert(result != nullptr);
  nlohmann::json j = nlohmann::json::parse(result);
  EchoFreeString(const_cast<char*>(result));
  return j;
}

}  // namespace

int main() {
  std::cout << "[PlayStatsTest] started" << std::endl;
#if defined(_MSC_VER)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

  const auto invalidDataDir =
      std::filesystem::temp_directory_path() / L"bottlemusic-init-failure-file";
  std::filesystem::remove_all(invalidDataDir);
  {
    std::ofstream marker(invalidDataDir);
    marker << "not-a-directory";
  }

  const auto invalidPathUtf8 = invalidDataDir.string();
  const int invalidInitStatus = EchoInitializeWithPathsV2(invalidPathUtf8.c_str());
  assert(invalidInitStatus != 0);
  char* initError = EchoGetLastError();
  assert(initError != nullptr);
  assert(std::string(initError).find("initialize") != std::string::npos);
  EchoFreeString(initError);
  const int failedInitShutdownStatus = EchoShutdown();
  assert(failedInitShutdownStatus == 0);

  const auto testDir = TestDirPath(L"bottlemusic-playstats-test");
  std::cout << "[PlayStatsTest] test dir: " << testDir.string() << std::endl;

  const int initStatus = EchoInitializeWithPathsV2(testDir.string().c_str());
  assert(initStatus == 0);

  // ── Seed data ────────────────────────────────────────────────────────
  // 6 counted plays across 2 days, 3 songs, 2 artists, 2 albums.
  //
  //   Song A (Artist X, Album One) — 3 plays, all completed, 240s each
  //   Song B (Artist X, Album One) — 2 plays, not completed, 90s each
  //   Song C (Artist Y, Album Two) — 1 play,  completed, 300s
  //
  // total_listened = 240*3 + 90*2 + 300 = 1200
  // completion_rate = (3+1)/6 = 4/6

  const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  const long long day1 = nowMs - 86400000;
  const long long day2 = nowMs;

  // Distinct timestamps so GetRecent ordering is deterministic.
  const long long t1 = day1;
  const long long t2 = day1 + 1000;
  const long long t3 = day1 + 2000;
  const long long t4 = day2 - 2000;
  const long long t5 = day2 - 1000;
  const long long t6 = day2;
  const long long t7 = day2 + 1000;

  auto makeRecord = [](const std::string& hash, const std::string& name,
                       const std::string& singer, const std::string& albumId,
                       const std::string& album, const std::string& cover,
                       double duration, bool completed, double listened,
                       const std::string& quality, long long playedAt) {
    return nlohmann::json{
        {"song_hash", hash},         {"song_name", name},
        {"singer_name", singer},     {"album_id", albumId},
        {"album_name", album},       {"cover_url", cover},
        {"duration_seconds", duration}, {"completed", completed},
        {"listened_seconds", listened}, {"quality", quality},
        {"played_at", playedAt}};
  };

  // Song A — 3 plays (album "album-1")
  RecordPlay(makeRecord("hashA", "Song A", "Artist X", "album-1", "Album One",
                        "http://img.example/a.jpg", 240.0, true, 240.0, "128", t1));
  RecordPlay(makeRecord("hashA", "Song A", "Artist X", "album-1", "Album One",
                        "http://img.example/a.jpg", 240.0, true, 240.0, "128", t2));
  RecordPlay(makeRecord("hashA", "Song A", "Artist X", "album-1", "Album One",
                        "http://img.example/a.jpg", 240.0, true, 240.0, "128", t4));

  // Song B — 2 plays (album "album-1")
  RecordPlay(makeRecord("hashB", "Song B", "Artist X", "album-1", "Album One",
                        "http://img.example/b.jpg", 180.0, false, 90.0, "128", t3));
  RecordPlay(makeRecord("hashB", "Song B", "Artist X", "album-1", "Album One",
                        "http://img.example/b.jpg", 180.0, false, 90.0, "128", t5));

  // Song C — 1 play (album "album-2", same display name "Album One" would
  // wrongly merge with the above if grouping by name — proves the album_id fix)
  RecordPlay(makeRecord("hashC", "Song C", "Artist Y", "album-2", "Album One",
                        "http://img.example/c.jpg", 300.0, true, 300.0, "sq", t6));

  // Plays of one minute or less are too short to count toward stats.
  RecordPlay(makeRecord("hashShort", "Short Song", "Artist Z", "album-short", "Short Album",
                        "http://img.example/short.jpg", 240.0, false, 60.0, "128", t7));

  // ── GetSummary ("all") ───────────────────────────────────────────────
  std::cout << "[PlayStatsTest] Testing GetSummary(all)..." << std::endl;
  {
    auto j = ParseAndFree(EchoStatsGetSummary("all"));
    std::cout << "  " << j.dump() << std::endl;
    assert(j["total_plays"] == 6);
    assert(j["unique_songs"] == 3);
    assert(j["unique_artists"] == 2);
    assert(j["range"] == "all");
    assert(j["total_listened_seconds"] == 1200.0);
    assert(std::abs(j["completion_rate"].get<double>() - (4.0 / 6.0)) < 0.001);
  }

  // ── GetSummary ("7d") ────────────────────────────────────────────────
  std::cout << "[PlayStatsTest] Testing GetSummary(7d)..." << std::endl;
  {
    auto j = ParseAndFree(EchoStatsGetSummary("7d"));
    std::cout << "  " << j.dump() << std::endl;
    assert(j["range"] == "7d");
    assert(j["total_plays"] == 6);
  }

  // ── GetTop (song) ────────────────────────────────────────────────────
  std::cout << "[PlayStatsTest] Testing GetTop(song)..." << std::endl;
  {
    auto j = ParseAndFree(EchoStatsGetTop("song", "all", 10));
    std::cout << "  " << j.dump() << std::endl;
    assert(j["dim"] == "song");
    assert(j["items"].size() == 3);
    assert(j["items"][0]["name"] == "Song A");
    assert(j["items"][0]["song_hash"] == "hashA");
    assert(j["items"][0]["cover_url"] == "http://img.example/a.jpg");
    assert(j["items"][0]["play_count"] == 3);
    assert(j["items"][0]["singer"] == "Artist X");
    assert(j["items"][1]["name"] == "Song B");
    assert(j["items"][1]["song_hash"] == "hashB");
    assert(j["items"][1]["play_count"] == 2);
    assert(j["items"][2]["name"] == "Song C");
    assert(j["items"][2]["song_hash"] == "hashC");
    assert(j["items"][2]["play_count"] == 1);
  }

  // ── GetTop (artist) ──────────────────────────────────────────────────
  std::cout << "[PlayStatsTest] Testing GetTop(artist)..." << std::endl;
  {
    auto j = ParseAndFree(EchoStatsGetTop("artist", "all", 10));
    std::cout << "  " << j.dump() << std::endl;
    assert(j["dim"] == "artist");
    assert(j["items"].size() == 2);
    assert(j["items"][0]["name"] == "Artist X");
    assert(j["items"][0]["cover_url"] == "http://img.example/a.jpg");
    assert(j["items"][0]["play_count"] == 5);
    assert(j["items"][1]["name"] == "Artist Y");
    assert(j["items"][1]["cover_url"] == "http://img.example/c.jpg");
    assert(j["items"][1]["play_count"] == 1);
  }

  // ── GetTop (album) ───────────────────────────────────────────────────
  // Seeded with TWO distinct album_ids both named "Album One" — grouping by
  // album_id (not name) must keep them separate. Old code merged by name.
  std::cout << "[PlayStatsTest] Testing GetTop(album)..." << std::endl;
  {
    auto j = ParseAndFree(EchoStatsGetTop("album", "all", 10));
    std::cout << "  " << j.dump() << std::endl;
    assert(j["dim"] == "album");
    assert(j["items"].size() == 2);
    // album-1 (5 plays: 3× Song A + 2× Song B) must outrank album-2 (1 play)
    assert(j["items"][0]["name"] == "Album One");
    assert(j["items"][0]["album_id"] == "album-1");
    assert(j["items"][0]["play_count"] == 5);
    assert(j["items"][1]["name"] == "Album One");
    assert(j["items"][1]["album_id"] == "album-2");
    assert(j["items"][1]["play_count"] == 1);
  }

  // ── GetTimeline ──────────────────────────────────────────────────────
  std::cout << "[PlayStatsTest] Testing GetTimeline..." << std::endl;
  {
    auto j = ParseAndFree(EchoStatsGetTimeline("all"));
    std::cout << "  " << j.dump() << std::endl;
    assert(j["items"].is_array());
    assert(j["items"].size() == 2);  // 2 distinct days
    int totalFromTimeline = 0;
    for (const auto& item : j["items"]) {
      totalFromTimeline += item["count"].get<int>();
    }
    assert(totalFromTimeline == 6);
  }

  // ── GetRecent (limit 3, offset 0) ────────────────────────────────────
  std::cout << "[PlayStatsTest] Testing GetRecent(3, 0)..." << std::endl;
  {
    auto j = ParseAndFree(EchoStatsGetRecent(3, 0));
    std::cout << "  " << j.dump() << std::endl;
    assert(j["items"].size() == 3);
    // Ordered by played_at DESC: t6 (hashC), t5 (hashB), t4 (hashA)
    assert(j["items"][0]["song_hash"] == "hashC");
    assert(j["items"][1]["song_hash"] == "hashB");
    assert(j["items"][2]["song_hash"] == "hashA");
  }

  // ── GetRecent (limit 2, offset 2) ────────────────────────────────────
  std::cout << "[PlayStatsTest] Testing GetRecent(2, 2)..." << std::endl;
  {
    auto j = ParseAndFree(EchoStatsGetRecent(2, 2));
    std::cout << "  " << j.dump() << std::endl;
    assert(j["items"].size() == 2);
    // Offset 2: skip hashC(t6) and hashB(t5), take hashA(t4) and hashB(t3)
    assert(j["items"][0]["song_hash"] == "hashA");
    assert(j["items"][1]["song_hash"] == "hashB");
  }

  // ── GetRecent (limit 10, offset 0 — all records) ─────────────────────
  std::cout << "[PlayStatsTest] Testing GetRecent(10, 0)..." << std::endl;
  {
    auto j = ParseAndFree(EchoStatsGetRecent(10, 0));
    std::cout << "  " << j.dump() << std::endl;
    assert(j["items"].size() == 6);
    // Verify descending order
    long long prev = -1;
    for (const auto& item : j["items"]) {
      long long ts = item["played_at"].get<long long>();
      if (prev != -1) assert(ts <= prev);
      prev = ts;
    }
  }

  // ── GetRecommendations ───────────────────────────────────────────────
  std::cout << "[PlayStatsTest] Testing GetRecommendations..." << std::endl;
  {
    auto j = ParseAndFree(EchoStatsGetRecommendations(5));
    std::cout << "  " << j.dump() << std::endl;
    assert(j["items"].is_array());
    assert(!j["items"].empty());
    assert(j["items"][0]["singer"] == "Artist X");
    assert(j["items"][0]["play_count"] == 5);
  }

  // ── Edge case: null input to RecordPlay is a safe no-op ──────────────
  std::cout << "[PlayStatsTest] Testing null RecordPlay..." << std::endl;
  {
    EchoStatsRecordPlay(nullptr);
    auto j = ParseAndFree(EchoStatsGetSummary("all"));
    assert(j["total_plays"] == 6);  // unchanged
  }

  // ── Edge case: invalid JSON to RecordPlay is a safe no-op ────────────
  std::cout << "[PlayStatsTest] Testing invalid JSON RecordPlay..." << std::endl;
  {
    EchoStatsRecordPlay("not valid json");
    auto j = ParseAndFree(EchoStatsGetSummary("all"));
    assert(j["total_plays"] == 6);  // unchanged
  }

  // Bound SQL: special chars / emoji must round-trip (A1).
  // Names with ', ;, --, and UTF-8 emoji must not break INSERT/SELECT.
  std::cout << "[PlayStatsTest] Testing special-char / emoji song names..." << std::endl;
  {
    // U+1F3B5 musical note as UTF-8 (avoid source encoding issues on MSVC).
    const std::string specialName = std::string("O'Brien; DROP-- ") + "\xF0\x9F\x8E\xB5";
    const std::string specialSinger = "Artist's \"Quote\"";
    const long long tSpecial = day2 + 5000;
    RecordPlay(makeRecord(
        "hashSpecial",
        specialName,
        specialSinger,
        "album-special",
        "Album -- comments",
        "http://img.example/special.jpg",
        180.0, true, 180.0, "320", tSpecial));

    auto summary = ParseAndFree(EchoStatsGetSummary("all"));
    std::cout << "  summary after special: " << summary.dump() << std::endl;
    if (summary["total_plays"] != 7) {
      std::cerr << "FAIL total_plays want 7 got " << summary.dump() << std::endl;
      return 1;
    }

    auto recent = ParseAndFree(EchoStatsGetRecent(5, 0));
    std::cout << "  recent after special: " << recent.dump() << std::endl;
    if (!recent["items"].is_array() || recent["items"].empty()) {
      std::cerr << "FAIL recent empty" << std::endl;
      return 1;
    }
    // Newest first - special record has latest played_at.
    // GetRecent maps song_name -> name, singer_name -> singer.
    if (recent["items"][0]["song_hash"] != "hashSpecial") {
      std::cerr << "FAIL song_hash " << recent["items"][0].dump() << std::endl;
      return 1;
    }
    if (recent["items"][0]["name"] != specialName) {
      std::cerr << "FAIL name want=[" << specialName << "] got=["
                << recent["items"][0].value("name", std::string()) << "]" << std::endl;
      return 1;
    }
    if (recent["items"][0]["singer"] != specialSinger) {
      std::cerr << "FAIL singer want=[" << specialSinger << "] got=["
                << recent["items"][0].value("singer", std::string()) << "]" << std::endl;
      return 1;
    }
    std::cout << "  special-char round-trip ok" << std::endl;
  }

  EchoShutdown();
  std::error_code ec;
  std::filesystem::remove_all(testDir, ec);
  if (ec) {
    std::cerr << "WARN remove_all: " << ec.message() << std::endl;
  }

  std::cout << "[PlayStatsTest] all assertions passed" << std::endl;
  return 0;
}
