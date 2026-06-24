#include "echo/stats/PlayStatsService.h"

#include <chrono>

#include <nlohmann/json.hpp>

namespace echo::stats {

using nlohmann::json;

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
  std::string sql = std::string("INSERT INTO play_history_v2 "
                    "(song_hash, song_name, singer_name, album_id, album_name, cover_url, "
                    "duration_seconds, completed, listened_seconds, quality, played_at) VALUES (") +
                    "'" + r.songHash + "','" + r.songName + "','" + r.singerName + "','" +
                    r.albumId + "','" + r.albumName + "','" + r.coverUrl + "'," +
                    std::to_string(r.durationSeconds) + "," +
                    (r.completed ? "1" : "0") + "," +
                    std::to_string(r.listenedSeconds) + ",'" + r.quality + "'," +
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
  if (dim == "song") {
    groupCol = "song_hash";
    nameCol = "song_name, singer_name, album_name, cover_url";
  } else if (dim == "artist") {
    groupCol = "singer_name";
    nameCol = "singer_name";
  } else {
    groupCol = "album_name";
    nameCol = "album_name, singer_name, cover_url";
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
      item["name"] = r[0];
      item["singer"] = r[1];
      item["album"] = r[2];
      item["cover_url"] = r[3];
      item["play_count"] = std::stoi(r[4]);
      item["total_listened_seconds"] = std::stod(r[5]);
    } else if (dim == "artist") {
      item["name"] = r[0];
      item["play_count"] = std::stoi(r[1]);
      item["total_listened_seconds"] = std::stod(r[2]);
    } else {
      item["name"] = r[0];
      item["singer"] = r[1];
      item["cover_url"] = r[2];
      item["play_count"] = std::stoi(r[3]);
      item["total_listened_seconds"] = std::stod(r[4]);
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
    item["song_hash"] = r[0];
    item["name"] = r[1];
    item["singer"] = r[2];
    item["album"] = r[3];
    item["cover_url"] = r[4];
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
