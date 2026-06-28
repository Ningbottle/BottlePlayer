#include "echo/stats/PlayStatsService.h"

#include <chrono>

#include <nlohmann/json.hpp>

namespace echo::stats {

using nlohmann::json;

constexpr double kMinCountedListenedSeconds = 60.0;

static std::string MinCountedListenedSecondsSql() {
  return std::to_string(kMinCountedListenedSeconds);
}

static std::string StatsWhere(long long since) {
  std::string where = "WHERE listened_seconds > " + MinCountedListenedSecondsSql();
  if (since > 0) where += " AND played_at >= " + std::to_string(since);
  return where;
}

static std::string StatsAliasPredicate(const std::string& alias, long long since) {
  std::string where = alias + ".listened_seconds > " + MinCountedListenedSecondsSql();
  if (since > 0) where += " AND " + alias + ".played_at >= " + std::to_string(since);
  return where;
}

static std::string SqlEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    if (c == '\'') out += "''";
    else out += c;
  }
  return out;
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
  if (range == "7d") return now - 7LL * 24 * 3600 * 1000;
  if (range == "30d") return now - 30LL * 24 * 3600 * 1000;
  return 0;  // "all"
}

bool PlayStatsService::RecordPlay(const PlayRecord& r) {
  if (r.listenedSeconds <= kMinCountedListenedSeconds) return false;

  std::string sql = std::string("INSERT INTO play_history_v2 "
                    "(song_hash, song_name, singer_name, album_id, album_name, cover_url, "
                    "duration_seconds, completed, listened_seconds, quality, played_at) VALUES (") +
                    "'" + SqlEscape(r.songHash) + "','" + SqlEscape(r.songName) + "','" +
                    SqlEscape(r.singerName) + "','" + SqlEscape(r.albumId) + "','" +
                    SqlEscape(r.albumName) + "','" + SqlEscape(r.coverUrl) + "'," +
                    std::to_string(r.durationSeconds) + "," +
                    (r.completed ? "1" : "0") + "," +
                    std::to_string(r.listenedSeconds) + ",'" + SqlEscape(r.quality) + "'," +
                    std::to_string(r.playedAtMs) + ")";
  try {
    db_.Execute(sql);
    return true;
  } catch (...) {
    return false;
  }
}

std::string PlayStatsService::GetSummary(const std::string& range) {
  long long since = RangeToTimestamp(range);
  std::string where = StatsWhere(since);
  auto rows = db_.ExecuteQuery(
      "SELECT COUNT(*), COALESCE(SUM(listened_seconds),0), "
      "COUNT(DISTINCT song_hash), COUNT(DISTINCT singer_name), "
      "COALESCE(CAST(SUM(CASE WHEN completed=1 THEN 1 ELSE 0 END) AS FLOAT) / NULLIF(COUNT(*), 0), 0) "
      "FROM play_history_v2 " + where);
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
  long long since = RangeToTimestamp(range);
  std::string where = StatsWhere(since);
  std::string groupCol, nameCol;
  if (dim == "song") {
    groupCol = "song_hash";
    nameCol = "song_hash, song_name, singer_name, album_name, cover_url";
  } else if (dim == "artist") {
    groupCol = "singer_name";
    nameCol =
        "singer_name, COALESCE((SELECT h2.cover_url FROM play_history_v2 h2 "
        "WHERE h2.singer_name = play_history_v2.singer_name "
        "AND h2.cover_url <> '' AND " +
        StatsAliasPredicate("h2", since) +
        " GROUP BY h2.cover_url ORDER BY COUNT(*) DESC, MAX(h2.played_at) DESC LIMIT 1), '')";
  } else {
    // #4: group by album_id, not album_name — two different albums with the
    // same name from different artists must not be merged in the stats.
    groupCol = "album_id";
    nameCol = "album_id, album_name, singer_name, cover_url";
  }

  std::string sql = "SELECT " + nameCol + ", COUNT(*) as cnt, "
                    "COALESCE(SUM(listened_seconds),0) as total_sec "
                    "FROM play_history_v2 " + where + " GROUP BY " + groupCol +
                    " ORDER BY cnt DESC LIMIT " + std::to_string(limit);
  auto rows = db_.ExecuteQuery(sql);
  json items = json::array();
  for (auto& r : rows) {
    json item;
    if (dim == "song") {
      item["song_hash"] = r[0];
      item["name"] = r[1];
      item["singer"] = r[2];
      item["album"] = r[3];
      item["cover_url"] = r[4];
      item["play_count"] = SafeStoi(r[5]);
      item["total_listened_seconds"] = SafeStod(r[6]);
    } else if (dim == "artist") {
      item["name"] = r[0];
      item["cover_url"] = r[1];
      item["play_count"] = SafeStoi(r[2]);
      item["total_listened_seconds"] = SafeStod(r[3]);
    } else {
      item["name"] = r[1];          // album_name (display)
      item["album_id"] = r[0];      // for client-side dedup/routing
      item["singer"] = r[2];
      item["cover_url"] = r[3];
      item["play_count"] = SafeStoi(r[4]);
      item["total_listened_seconds"] = SafeStod(r[5]);
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
  std::string where = StatsWhere(since);
  auto rows = db_.ExecuteQuery(
      "SELECT date(played_at/1000, 'unixepoch', 'localtime') AS date, "
      "COUNT(*) AS count FROM play_history_v2 " + where +
      " GROUP BY date ORDER BY date ASC");
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
  auto rows = db_.ExecuteQuery(
      "SELECT song_hash, song_name, singer_name, album_name, cover_url, "
      "duration_seconds, listened_seconds, completed, quality, played_at "
      "FROM play_history_v2 WHERE listened_seconds > " + MinCountedListenedSecondsSql() +
      " ORDER BY played_at DESC LIMIT " +
      std::to_string(limit) + " OFFSET " + std::to_string(offset));
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
  auto rows = db_.ExecuteQuery(
      "SELECT singer_name, COUNT(*) as cnt FROM play_history_v2 "
      "WHERE listened_seconds > " + MinCountedListenedSecondsSql() +
      " GROUP BY singer_name ORDER BY cnt DESC LIMIT " + std::to_string(limit));
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
