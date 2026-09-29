// EchoPlayStatsTest — Contract test for the PlayStatsService C API.
// Exercises EchoStatsRecordPlay + all five EchoStatsGet* query functions,
// verifying field correctness, ordering, range filtering, and pagination.

#include <cassert>
#include <io.h>
#include <process.h>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

#include "echo/core/C_API.h"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace {

std::filesystem::path TestDirPath(const wchar_t* name) {
  // Unique per run: a wedged leftover process holding locks inside a
  // fixed-name directory wedged every later run's remove_all (2026-09-27
  // incident). A per-run suffix removes that failure mode; each run cleans
  // only its own directory.
  const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  std::wstring unique = std::wstring(name) + L"-" + std::to_wstring(::_getpid()) +
                        L"-" + std::to_wstring(stamp);
  auto path = std::filesystem::temp_directory_path() / unique;
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

int RecordPlayStatus(const nlohmann::json& j) {
  std::string s = j.dump();
  return EchoStatsRecordPlay(s.c_str());
}

void RecordPlay(const nlohmann::json& j) { RecordPlayStatus(j); }

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

  // ── B06/B10: EchoStatsRecordPlay outcomes are diagnosable ────────────
  std::cout << "[PlayStatsTest] Testing RecordPlay status codes..." << std::endl;
  {
    auto base = ParseAndFree(EchoStatsGetSummary("all"));
    const long long playsBefore = base["total_plays"].get<long long>();

    // Valid record -> kEchoStatsRecorded (0).
    const int okCode = RecordPlayStatus(makeRecord(
        "hashB06", "B06 Song", "Artist X", "album-1", "Album One",
        "", 240.0, true, 240.0, "128", day2 + 9000));
    if (okCode != kEchoStatsRecorded) {
      std::cerr << "FAIL valid record status=" << okCode << std::endl;
      return 1;
    }

    // Below-threshold listen -> kEchoStatsRecordBelowThreshold (1), normal.
    const int shortCode = RecordPlayStatus(makeRecord(
        "hashB06s", "B06 Short", "Artist X", "album-1", "Album One",
        "", 240.0, false, 45.0, "128", day2 + 9001));
    if (shortCode != kEchoStatsRecordBelowThreshold) {
      std::cerr << "FAIL below-threshold status=" << shortCode << std::endl;
      return 1;
    }

    // Invalid inputs -> kEchoStatsInvalidRecord (2), nothing inserted.
    const nlohmann::json invalidChecks[] = {
        makeRecord("", "No Identity", "A", "album-1", "Album", "", 240.0, true, 240.0, "128", day2 + 9002),
        makeRecord("hashB06n", "Neg Duration", "A", "album-1", "Album", "", -5.0, true, 240.0, "128", day2 + 9003),
        makeRecord("hashB06p", "Neg Time", "A", "album-1", "Album", "", 240.0, true, 240.0, "128", -1),
    };
    for (const auto& invalid : invalidChecks) {
      const int code = RecordPlayStatus(invalid);
      if (code != kEchoStatsInvalidRecord) {
        std::cerr << "FAIL invalid record accepted, status=" << code << " record="
                  << invalid.dump() << std::endl;
        return 1;
      }
    }

    // A non-finite float cannot survive JSON (nlohmann serializes it to
    // null): the FFI must classify that payload as malformed, not storage.
    const int infCode = RecordPlayStatus(makeRecord(
        "hashB06i", "Inf Listen", "A", "album-1", "Album", "", 240.0, true,
        std::numeric_limits<double>::infinity(), "128", day2 + 9004));
    if (infCode != kEchoStatsBadJson) {
      std::cerr << "FAIL non-finite payload status=" << infCode << std::endl;
      return 1;
    }

    // Unparseable JSON -> kEchoStatsBadJson (3).
    if (EchoStatsRecordPlay("not valid json") != kEchoStatsBadJson) {
      std::cerr << "FAIL bad json status" << std::endl;
      return 1;
    }
    auto wrongStringType = makeRecord(
        "hash-type", "Wrong Type", "A", "album-1", "Album", "",
        240.0, true, 240.0, "128", day2 + 9005);
    wrongStringType["song_hash"] = 17;
    auto wrongBoolType = makeRecord(
        "hash-type-2", "Wrong Bool", "A", "album-1", "Album", "",
        240.0, true, 240.0, "128", day2 + 9006);
    wrongBoolType["completed"] = "yes";
    auto wrongTimestampType = makeRecord(
        "hash-type-3", "Wrong Timestamp", "A", "album-1", "Album", "",
        240.0, true, 240.0, "128", day2 + 9007);
    wrongTimestampType["played_at"] = "now";
    for (const auto& malformed : {wrongStringType, wrongBoolType, wrongTimestampType}) {
      if (RecordPlayStatus(malformed) != kEchoStatsBadJson) {
        std::cerr << "FAIL malformed field type was not classified as bad JSON: "
                  << malformed.dump() << std::endl;
        return 1;
      }
    }

    // played_at == 0 degrades to "now" and records (no epoch-zero poisoning).
    const int zeroTimeCode = RecordPlayStatus(makeRecord(
        "hashB06z", "Zero Time", "Artist X", "album-1", "Album One",
        "", 240.0, true, 240.0, "128", 0));
    if (zeroTimeCode != kEchoStatsRecorded) {
      std::cerr << "FAIL zero played_at status=" << zeroTimeCode << std::endl;
      return 1;
    }

    auto after = ParseAndFree(EchoStatsGetSummary("all"));
    const long long playsAfter = after["total_plays"].get<long long>();
    // +1 valid +1 zero-time-recorded; short/invalid inserted nothing.
    if (playsAfter != playsBefore + 2) {
      std::cerr << "FAIL plays after B06 section: before=" << playsBefore
                << " after=" << playsAfter << std::endl;
      return 1;
    }
    std::cout << "  record status codes ok" << std::endl;
  }

  // ── B09: song top keeps album identity; unknown albums do not merge ──
  std::cout << "[PlayStatsTest] Testing song album_id + unknown album groups..." << std::endl;
  {
    auto songTop = ParseAndFree(EchoStatsGetTop("song", "all", 50));
    bool sawB06 = false;
    for (const auto& item : songTop["items"]) {
      if (item.value("song_hash", "") == "hashB06") {
        sawB06 = true;
        if (item.value("album_id", "MISSING") != "album-1") {
          std::cerr << "FAIL song top lost album_id: " << item.dump() << std::endl;
          return 1;
        }
      }
    }
    if (!sawB06) {
      std::cerr << "FAIL hashB06 missing from song top" << std::endl;
      return 1;
    }

    // Audit-probe scenario: two distinct albums, both WITHOUT an album_id,
    // different display names — they must stay two groups. Previously both
    // collapsed into the album_id='' group and the group name was an
    // arbitrary row's album_name.
    const long long tUnknown = day2 + 10000;
    RecordPlay(makeRecord("hashUA", "Unknown A", "Artist U", "", "Album A",
                          "", 240.0, true, 240.0, "128", tUnknown));
    RecordPlay(makeRecord("hashUB", "Unknown B", "Artist U", "", "Album B",
                          "", 240.0, true, 240.0, "128", tUnknown + 1));
    auto albumTop = ParseAndFree(EchoStatsGetTop("album", "all", 50));
    int namedUnknownGroups = 0;
    for (const auto& item : albumTop["items"]) {
      const std::string name = item.value("name", "");
      if (name == "Album A") ++namedUnknownGroups;
      if (name == "Album B") ++namedUnknownGroups;
      if (name == "Album A" && item.value("album_id", "x") != "") {
        std::cerr << "FAIL unknown-album group exposed a synthetic id: "
                  << item.dump() << std::endl;
        return 1;
      }
    }
    if (namedUnknownGroups != 2) {
      std::cerr << "FAIL unknown albums merged; groups found=" << namedUnknownGroups
                << " in " << albumTop.dump() << std::endl;
      return 1;
    }
    std::cout << "  song album_id + unknown album groups ok" << std::endl;
  }

  // ── Input boundary: a negative LIMIT must not disable the row bound. ──
  // SQLite interprets LIMIT -1 as "no limit" (and a negative OFFSET as an
  // error). These "top N / paged" queries must stay bounded regardless of
  // what an FFI/UI caller passes: non-positive limits resolve to zero rows
  // and negative offsets clamp to zero. Temp data only.
  std::cout << "[PlayStatsTest] Testing non-positive limit / negative offset..." << std::endl;
  {
    auto negTop = ParseAndFree(EchoStatsGetTop("song", "all", -1));
    if (!negTop["items"].is_array() || negTop["items"].size() != 0) {
      std::cerr << "FAIL negative top limit must be bounded to 0 rows, got "
                << negTop.dump() << std::endl;
      return 1;
    }
    auto zeroTop = ParseAndFree(EchoStatsGetTop("artist", "all", 0));
    if (zeroTop["items"].size() != 0) {
      std::cerr << "FAIL zero top limit must return 0 rows, got "
                << zeroTop.dump() << std::endl;
      return 1;
    }
    auto invalidDim = ParseAndFree(EchoStatsGetTop("not-a-dimension", "all", 10));
    if (invalidDim["items"].size() != 0) {
      std::cerr << "FAIL invalid stats dimension must return 0 rows, got "
                << invalidDim.dump() << std::endl;
      return 1;
    }
    auto zeroRecent = ParseAndFree(EchoStatsGetRecent(0, 2));
    if (zeroRecent["items"].size() != 0) {
      std::cerr << "FAIL zero recent limit must return 0 rows, got "
                << zeroRecent.dump() << std::endl;
      return 1;
    }
    auto negRecent = ParseAndFree(EchoStatsGetRecent(-1, 0));
    if (negRecent["items"].size() != 0) {
      std::cerr << "FAIL negative recent limit must be bounded to 0 rows, got "
                << negRecent.dump() << std::endl;
      return 1;
    }
    // Negative OFFSET clamps to 0; a positive limit still yields rows and
    // must not degrade into an error payload.
    auto negOffset = ParseAndFree(EchoStatsGetRecent(3, -1));
    if (negOffset["items"].size() != 3) {
      std::cerr << "FAIL negative offset must clamp to 0, wanted 3 rows, got "
                << negOffset.dump() << std::endl;
      return 1;
    }
    auto negRecs = ParseAndFree(EchoStatsGetRecommendations(-1));
    if (negRecs["items"].size() != 0) {
      std::cerr << "FAIL negative recommendations limit must be bounded to 0 rows, got "
                << negRecs.dump() << std::endl;
      return 1;
    }
    auto zeroRecs = ParseAndFree(EchoStatsGetRecommendations(0));
    if (zeroRecs["items"].size() != 0) {
      std::cerr << "FAIL zero recommendations limit must return 0 rows, got "
                << zeroRecs.dump() << std::endl;
      return 1;
    }

    // A huge positive LIMIT is also attacker-controlled at the C boundary.
    // Seed just over the documented cap and prove every row-returning query
    // stays bounded rather than allocating an INT_MAX-sized response.
    for (int i = 0; i < ECHO_C_API_MAX_STATS_ROWS + 1; ++i) {
      const auto suffix = std::to_string(i);
      const int code = RecordPlayStatus(makeRecord(
          "hash-cap-" + suffix, "Cap Song " + suffix,
          "Cap Artist " + suffix, "album-cap-" + suffix,
          "Cap Album " + suffix, "", 180.0, true, 120.0, "128",
          day2 + 20000 + i));
      if (code != kEchoStatsRecorded) {
        std::cerr << "FAIL cap fixture insert status=" << code << " at " << i
                  << std::endl;
        return 1;
      }
    }
    const int huge = std::numeric_limits<int>::max();
    auto cappedRecent = ParseAndFree(EchoStatsGetRecent(huge, 0));
    auto cappedTop = ParseAndFree(EchoStatsGetTop("song", "all", huge));
    auto cappedRecs = ParseAndFree(EchoStatsGetRecommendations(huge));
    if (cappedRecent["items"].size() != ECHO_C_API_MAX_STATS_ROWS ||
        cappedTop["items"].size() != ECHO_C_API_MAX_STATS_ROWS ||
        cappedRecs["items"].size() != ECHO_C_API_MAX_STATS_ROWS) {
      std::cerr << "FAIL huge positive stats limits were not capped: recent="
                << cappedRecent["items"].size() << " top="
                << cappedTop["items"].size() << " recommendations="
                << cappedRecs["items"].size() << std::endl;
      return 1;
    }
    std::cout << "  non-positive limit / negative offset bounded ok" << std::endl;
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
