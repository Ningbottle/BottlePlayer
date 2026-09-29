#include "echo/stats/PlayStatsService.h"
#include "echo/core/C_API.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <nlohmann/json.hpp>

namespace echo::stats {

using nlohmann::json;
using echo::storage::BindValue;

constexpr double kMinCountedListenedSeconds = 60.0;

// Column names for GROUP BY / SELECT must stay on a switch whitelist —
// SQLite cannot bind identifiers, only values.
static const char* DimGroupCol(const std::string& dim) {
  if (dim == "song") return "song_hash";
  if (dim == "artist") return "singer_name";
  return "album_id";
}

static double SafeStod(const std::string& s) {
  if (s.empty()) return 0.0;
  try { return std::stod(s); } catch (...) { return 0.0; }
}

static int SafeStoi(const std::string& s) {
  if (s.empty()) return 0;
  try { return std::stoi(s); } catch (...) { return 0; }
}

static long long SafeStoll(const std::string& s) {
  if (s.empty()) return 0;
  try { return std::stoll(s); } catch (...) { return 0; }
}

PlayStatsService::PlayStatsService(echo::storage::Database& db) : db_(db) {}

long long PlayStatsService::RangeToTimestamp(const std::string& range) {
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();
  if (range == "1d") return now - 1LL * 24 * 3600 * 1000;
  if (range == "7d") return now - 7LL * 24 * 3600 * 1000;
  if (range == "30d") return now - 30LL * 24 * 3600 * 1000;
  return 0;  // "all"
}

RecordStatus PlayStatsService::RecordPlay(const PlayRecord& r) {
  // B10: identity and numeric bounds are validated here, at the boundary —
  // the backend cannot assume every caller pre-filters. Display fields
  // (names, album, cover, quality) may be empty: they degrade, they do not
  // invalidate the play identity.
  if (r.songHash.empty()) return RecordStatus::InvalidRecord;
  if (r.playedAtMs < 0) return RecordStatus::InvalidRecord;
  if (std::isnan(r.durationSeconds) || std::isinf(r.durationSeconds) ||
      r.durationSeconds < 0) {
    return RecordStatus::InvalidRecord;
  }
  if (std::isnan(r.listenedSeconds) || std::isinf(r.listenedSeconds) ||
      r.listenedSeconds < 0) {
    return RecordStatus::InvalidRecord;
  }
  if (r.listenedSeconds <= kMinCountedListenedSeconds) {
    return RecordStatus::BelowThreshold;
  }
  // playedAtMs == 0 means "no timestamp supplied" — the record exists because
  // the play just happened, so the event time is now. Negative stays invalid.
  long long playedAt = r.playedAtMs;
  if (playedAt == 0) {
    playedAt = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
  }

  static const char* kSql =
      "INSERT INTO play_history_v2 "
      "(song_hash, song_name, singer_name, album_id, album_name, cover_url, "
      "duration_seconds, completed, listened_seconds, quality, played_at) "
      "VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)";
  try {
    std::vector<BindValue> params = {
        r.songHash,
        r.songName,
        r.singerName,
        r.albumId,
        r.albumName,
        r.coverUrl,
        r.durationSeconds,
        static_cast<std::int64_t>(r.completed ? 1 : 0),
        r.listenedSeconds,
        r.quality,
        playedAt,
    };
    db_.ExecuteBound(kSql, params);
    return RecordStatus::Recorded;
  } catch (const std::exception&) {
    return RecordStatus::StorageError;
  }
}

std::string PlayStatsService::GetSummary(const std::string& range) {
  long long since = RangeToTimestamp(range);
  std::vector<std::vector<std::string>> rows;
  if (since > 0) {
    rows = db_.ExecuteQueryBound(
        "SELECT COUNT(*), COALESCE(SUM(listened_seconds),0), "
        "COUNT(DISTINCT song_hash), COUNT(DISTINCT singer_name), "
        "COALESCE(CAST(SUM(CASE WHEN completed=1 THEN 1 ELSE 0 END) AS FLOAT) / "
        "NULLIF(COUNT(*), 0), 0) "
        "FROM play_history_v2 WHERE listened_seconds > ?1 AND played_at >= ?2",
        {kMinCountedListenedSeconds, static_cast<std::int64_t>(since)});
  } else {
    rows = db_.ExecuteQueryBound(
        "SELECT COUNT(*), COALESCE(SUM(listened_seconds),0), "
        "COUNT(DISTINCT song_hash), COUNT(DISTINCT singer_name), "
        "COALESCE(CAST(SUM(CASE WHEN completed=1 THEN 1 ELSE 0 END) AS FLOAT) / "
        "NULLIF(COUNT(*), 0), 0) "
        "FROM play_history_v2 WHERE listened_seconds > ?1",
        {kMinCountedListenedSeconds});
  }
  json j;
  if (!rows.empty()) {
    auto& r = rows[0];
    j["total_plays"] = SafeStoi(r[0]);
    j["total_listened_seconds"] = SafeStod(r[1]);
    j["unique_songs"] = SafeStoi(r[2]);
    j["unique_artists"] = SafeStoi(r[3]);
    j["completion_rate"] = SafeStod(r[4]);
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
  // Boundary: SQLite reads a negative LIMIT as "no limit" (and zero as no
  // rows). A "top N" query must never turn into a full-table scan because a
  // caller passed a non-positive N, so resolve it to zero rows explicitly.
  json early;
  early["dim"] = dim;
  early["items"] = json::array();
  if ((dim != "song" && dim != "artist" && dim != "album") || limit <= 0) {
    return early.dump();
  }
  const int safeLimit = std::min(limit, ECHO_C_API_MAX_STATS_ROWS);

  long long since = RangeToTimestamp(range);
  // groupCol/nameCol from whitelist only — identifiers cannot be bound.
  const char* groupCol = DimGroupCol(dim);

  std::string nameCol;
  std::string sql;
  std::vector<BindValue> params;

  if (dim == "song") {
    // B09: song results carry the play identity (album_id) so the UI can
    // resolve the exact album/edition later. MAX() keeps every group's
    // identity/display columns deterministic; for album_id it also prefers
    // a non-empty value ('' sorts below any non-empty string).
    nameCol = "song_hash, MAX(song_name) AS song_name, MAX(singer_name) AS singer_name, "
              "MAX(album_id) AS album_id, MAX(album_name) AS album_name, "
              "MAX(cover_url) AS cover_url";
  } else if (dim == "artist") {
    if (since > 0) {
      nameCol =
          "singer_name, COALESCE((SELECT h2.cover_url FROM play_history_v2 h2 "
          "WHERE h2.singer_name = play_history_v2.singer_name "
          "AND h2.cover_url <> '' AND h2.listened_seconds > ?1 "
          "AND h2.played_at >= ?2 "
          "GROUP BY h2.cover_url ORDER BY COUNT(*) DESC, MAX(h2.played_at) DESC LIMIT 1), '')";
    } else {
      nameCol =
          "singer_name, COALESCE((SELECT h2.cover_url FROM play_history_v2 h2 "
          "WHERE h2.singer_name = play_history_v2.singer_name "
          "AND h2.cover_url <> '' AND h2.listened_seconds > ?1 "
          "GROUP BY h2.cover_url ORDER BY COUNT(*) DESC, MAX(h2.played_at) DESC LIMIT 1), '')";
    }
  } else {
    // B09 unknown-album policy: rows WITH an album_id group by their id;
    // rows WITHOUT one would all collide on album_id='' and arbitrarily
    // merge distinct albums, so their group key is the album NAME instead
    // ('byname:' cannot collide with a real upstream album_id). Display
    // columns use MAX() so a group's name/cover/singer are deterministic,
    // never "some arbitrary row of the group".
    nameCol =
        "CASE WHEN album_id IS NOT NULL AND album_id <> '' THEN album_id "
        "ELSE 'byname:' || album_name END AS group_key, "
        "MAX(album_name) AS album_name, MAX(singer_name) AS singer_name, "
        "MAX(cover_url) AS cover_url, "
        "MAX(CASE WHEN album_id IS NOT NULL AND album_id <> '' THEN album_id "
        "ELSE '' END) AS album_id_resolved";
  }

  // The album dim now groups by the computed group_key alias, not by the
  // raw album_id column.
  const std::string groupBy =
      (dim == "album") ? std::string("group_key") : std::string(groupCol);

  if (since > 0) {
    sql = "SELECT " + nameCol + ", COUNT(*) as cnt, "
          "COALESCE(SUM(listened_seconds),0) as total_sec "
          "FROM play_history_v2 WHERE listened_seconds > ?1 AND played_at >= ?2 "
          "GROUP BY " +
          groupBy + " ORDER BY cnt DESC LIMIT ?3";
    // Artist subquery also uses ?1/?2 for min listen + since.
    params = {kMinCountedListenedSeconds, static_cast<std::int64_t>(since),
              static_cast<std::int64_t>(safeLimit)};
  } else {
    sql = "SELECT " + nameCol + ", COUNT(*) as cnt, "
          "COALESCE(SUM(listened_seconds),0) as total_sec "
          "FROM play_history_v2 WHERE listened_seconds > ?1 "
          "GROUP BY " +
          groupBy + " ORDER BY cnt DESC LIMIT ?2";
    params = {kMinCountedListenedSeconds, static_cast<std::int64_t>(safeLimit)};
  }

  auto rows = db_.ExecuteQueryBound(sql, params);
  json items = json::array();
  for (auto& r : rows) {
    json item;
    if (dim == "song") {
      item["song_hash"] = r[0];
      item["name"] = r[1];
      item["singer"] = r[2];
      item["album_id"] = r[3];
      item["album"] = r[4];
      item["cover_url"] = r[5];
      item["play_count"] = SafeStoi(r[6]);
      item["total_listened_seconds"] = SafeStod(r[7]);
    } else if (dim == "artist") {
      item["name"] = r[0];
      item["cover_url"] = r[1];
      item["play_count"] = SafeStoi(r[2]);
      item["total_listened_seconds"] = SafeStod(r[3]);
    } else {
      item["name"] = r[1];
      item["album_id"] = r[4];
      item["singer"] = r[2];
      item["cover_url"] = r[3];
      item["play_count"] = SafeStoi(r[5]);
      item["total_listened_seconds"] = SafeStod(r[6]);
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
  std::vector<std::vector<std::string>> rows;
  if (since > 0) {
    rows = db_.ExecuteQueryBound(
        "SELECT date(played_at/1000, 'unixepoch', 'localtime') AS date, "
        "COUNT(*) AS count FROM play_history_v2 "
        "WHERE listened_seconds > ?1 AND played_at >= ?2 "
        "GROUP BY date ORDER BY date ASC",
        {kMinCountedListenedSeconds, static_cast<std::int64_t>(since)});
  } else {
    rows = db_.ExecuteQueryBound(
        "SELECT date(played_at/1000, 'unixepoch', 'localtime') AS date, "
        "COUNT(*) AS count FROM play_history_v2 "
        "WHERE listened_seconds > ?1 "
        "GROUP BY date ORDER BY date ASC",
        {kMinCountedListenedSeconds});
  }
  json items = json::array();
  for (auto& r : rows) {
    json item;
    item["date"] = r[0];
    item["count"] = SafeStoi(r[1]);
    items.push_back(item);
  }
  json j;
  j["items"] = items;
  return j.dump();
}

std::string PlayStatsService::GetRecent(int limit, int offset) {
  json early;
  early["items"] = json::array();
  // Non-positive limit must stay bounded (never SQLite's "no limit" case);
  // a negative OFFSET is an SQL error, so clamp it to zero.
  if (limit <= 0) return early.dump();
  const int safeOffset = std::max(offset, 0);
  const int safeLimit = std::min(limit, ECHO_C_API_MAX_STATS_ROWS);
  auto rows = db_.ExecuteQueryBound(
      "SELECT song_hash, song_name, singer_name, album_name, cover_url, "
      "duration_seconds, listened_seconds, completed, quality, played_at "
      "FROM play_history_v2 WHERE listened_seconds > ?1 "
      "ORDER BY played_at DESC LIMIT ?2 OFFSET ?3",
      {kMinCountedListenedSeconds, static_cast<std::int64_t>(safeLimit),
       static_cast<std::int64_t>(safeOffset)});
  json items = json::array();
  for (auto& r : rows) {
    json item;
    item["song_hash"] = r[0];
    item["name"] = r[1];
    item["singer"] = r[2];
    item["album"] = r[3];
    item["cover_url"] = r[4];
    item["duration_seconds"] = SafeStod(r[5]);
    item["listened_seconds"] = SafeStod(r[6]);
    item["completed"] = r[7] == "1";
    item["quality"] = r[8];
    item["played_at"] = SafeStoll(r[9]);
    items.push_back(item);
  }
  json j;
  j["items"] = items;
  return j.dump();
}

std::string PlayStatsService::GetRecommendations(int limit) {
  json early;
  early["items"] = json::array();
  // Non-positive limit must never disable the bound (SQLite LIMIT -1).
  if (limit <= 0) return early.dump();
  const int safeLimit = std::min(limit, ECHO_C_API_MAX_STATS_ROWS);
  auto rows = db_.ExecuteQueryBound(
      "SELECT singer_name, COUNT(*) as cnt FROM play_history_v2 "
      "WHERE listened_seconds > ?1 "
      "GROUP BY singer_name ORDER BY cnt DESC LIMIT ?2",
      {kMinCountedListenedSeconds, static_cast<std::int64_t>(safeLimit)});
  json items = json::array();
  for (auto& r : rows) {
    json item;
    item["singer"] = r[0];
    item["play_count"] = SafeStoi(r[1]);
    items.push_back(item);
  }
  json j;
  j["items"] = items;
  return j.dump();
}

}  // namespace echo::stats
