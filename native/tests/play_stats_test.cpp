// EchoPlayStatsTest — Contract test for the PlayStatsService C API.
// Exercises EchoStatsRecordPlay + all five EchoStatsGet* query functions,
// verifying field correctness, ordering, range filtering, and pagination.

#include <cassert>
#include <cmath>
#include <chrono>
#include <filesystem>
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

  const auto testDir = TestDirPath(L"bottlemusic-playstats-test");
  std::cout << "[PlayStatsTest] test dir: " << testDir.string() << std::endl;

  EchoInitializeWithPaths(testDir.string().c_str());

  // ── Seed data ────────────────────────────────────────────────────────
  // 6 plays across 2 days, 3 songs, 2 artists, 2 albums.
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

  auto makeRecord = [](const std::string& hash, const std::string& name,
                       const std::string& singer, const std::string& album,
                       const std::string& cover, double duration,
                       bool completed, double listened,
                       const std::string& quality, long long playedAt) {
    return nlohmann::json{
        {"song_hash", hash},         {"song_name", name},
        {"singer_name", singer},     {"album_id", "1"},
        {"album_name", album},       {"cover_url", cover},
        {"duration_seconds", duration}, {"completed", completed},
        {"listened_seconds", listened}, {"quality", quality},
        {"played_at", playedAt}};
  };

  // Song A — 3 plays
  RecordPlay(makeRecord("hashA", "Song A", "Artist X", "Album One",
                        "http://img.example/a.jpg", 240.0, true, 240.0, "128", t1));
  RecordPlay(makeRecord("hashA", "Song A", "Artist X", "Album One",
                        "http://img.example/a.jpg", 240.0, true, 240.0, "128", t2));
  RecordPlay(makeRecord("hashA", "Song A", "Artist X", "Album One",
                        "http://img.example/a.jpg", 240.0, true, 240.0, "128", t4));

  // Song B — 2 plays
  RecordPlay(makeRecord("hashB", "Song B", "Artist X", "Album One",
                        "http://img.example/b.jpg", 180.0, false, 90.0, "128", t3));
  RecordPlay(makeRecord("hashB", "Song B", "Artist X", "Album One",
                        "http://img.example/b.jpg", 180.0, false, 90.0, "128", t5));

  // Song C — 1 play
  RecordPlay(makeRecord("hashC", "Song C", "Artist Y", "Album Two",
                        "http://img.example/c.jpg", 300.0, true, 300.0, "sq", t6));

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
    assert(j["items"][0]["play_count"] == 3);
    assert(j["items"][0]["singer"] == "Artist X");
    assert(j["items"][1]["name"] == "Song B");
    assert(j["items"][1]["play_count"] == 2);
    assert(j["items"][2]["name"] == "Song C");
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
    assert(j["items"][0]["play_count"] == 5);
    assert(j["items"][1]["name"] == "Artist Y");
    assert(j["items"][1]["play_count"] == 1);
  }

  // ── GetTop (album) ───────────────────────────────────────────────────
  std::cout << "[PlayStatsTest] Testing GetTop(album)..." << std::endl;
  {
    auto j = ParseAndFree(EchoStatsGetTop("album", "all", 10));
    std::cout << "  " << j.dump() << std::endl;
    assert(j["dim"] == "album");
    assert(j["items"].size() == 2);
    assert(j["items"][0]["name"] == "Album One");
    assert(j["items"][0]["play_count"] == 5);
    assert(j["items"][1]["name"] == "Album Two");
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
      totalFromTimeline += item["play_count"].get<int>();
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

  EchoShutdown();
  std::filesystem::remove_all(testDir);

  std::cout << "[PlayStatsTest] all assertions passed" << std::endl;
  return 0;
}
