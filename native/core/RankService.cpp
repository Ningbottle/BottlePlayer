#include "echo/core/RankService.h"

#include <algorithm>
#include <string>
#include <utility>

namespace echo::core {
namespace {

int Clamp(int value, int minValue, int maxValue) {
  return std::max(minValue, std::min(value, maxValue));
}

std::string ReadString(const nlohmann::json& value, const char* key) {
  if (!value.contains(key)) return "";
  const auto& item = value.at(key);
  if (item.is_string()) return item.get<std::string>();
  if (item.is_number_integer()) return std::to_string(item.get<std::int64_t>());
  if (item.is_number_unsigned()) return std::to_string(item.get<std::uint64_t>());
  return "";
}

int ReadInt(const nlohmann::json& value, const char* key, int fallback = 0) {
  if (!value.contains(key)) return fallback;
  const auto& item = value.at(key);
  if (item.is_number_integer()) return item.get<int>();
  if (item.is_number_unsigned()) return static_cast<int>(item.get<unsigned int>());
  if (item.is_string()) {
    try {
      return std::stoi(item.get<std::string>());
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

nlohmann::json EmptyList() {
  return {{"status", 1}, {"data", {{"info", nlohmann::json::array()}, {"list", nlohmann::json::array()}, {"total", 0}}}};
}

nlohmann::json EmptySongs(int rankId, int page, int pageSize) {
  return {
      {"status", 1},
      {"data",
       {
           {"info", nlohmann::json::array()},
           {"list", nlohmann::json::array()},
           {"songs", nlohmann::json::array()},
           {"total", 0},
           {"rankid", rankId},
           {"page", page},
           {"pagesize", pageSize},
       }},
  };
}

nlohmann::json ErrorList(std::string message, long upstreamStatus = 0) {
  auto body = EmptyList();
  body["status"] = 0;
  body["error_code"] = "native_rank_list_failed";
  body["error"] = std::move(message);
  if (upstreamStatus > 0) body["upstream_status"] = upstreamStatus;
  return body;
}

nlohmann::json ErrorSongs(int rankId, int page, int pageSize, std::string message, long upstreamStatus = 0) {
  auto body = EmptySongs(rankId, page, pageSize);
  body["status"] = 0;
  body["error_code"] = "native_rank_audio_failed";
  body["error"] = std::move(message);
  if (upstreamStatus > 0) body["upstream_status"] = upstreamStatus;
  return body;
}

nlohmann::json NormalizeRankSong(const nlohmann::json& raw) {
  nlohmann::json item = raw;
  const auto albumCover = ReadString(raw, "album_sizable_cover");
  const auto hash = ReadString(raw, "hash");
  const auto duration = ReadInt(raw, "duration");

  item["audio_info"] = {
      {"hash", hash},
      {"hash_128", hash},
      {"duration", duration * 1000},
      {"duration_128", duration * 1000},
  };
  item["album_info"] = {
      {"album_id", ReadString(raw, "album_id")},
      {"album_name", ReadString(raw, "album_name")},
      {"sizable_cover", albumCover},
  };
  item["trans_param"] = {{"union_cover", albumCover}};
  item["privilege_download"] = {{"privilege", ReadInt(raw, "privilege")}};
  item["deprecated"] = {
      {"old_cpy", ReadInt(raw, "old_cpy")},
      {"pay_type", ReadInt(raw, "pay_type")},
  };
  item["HQ"] = {{"Hash", ReadString(raw, "320hash")}};
  item["SQ"] = {{"Hash", ReadString(raw, "sqhash")}};
  item["Res"] = {{"Hash", ReadString(raw, "hash_high")}};
  return item;
}

nlohmann::json ParseJsonOrError(const HttpResult& result, const char* code) {
  if (!result.error.empty()) return {{"error_code", code}, {"error", result.error}};
  if (result.statusCode < 200 || result.statusCode >= 300) {
    return {{"error_code", code}, {"error", "Kugou rank upstream returned an error"}};
  }
  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& error) {
    return {{"error_code", code}, {"error", std::string("Invalid Kugou rank JSON: ") + error.what()}};
  }
}

}  // namespace

RankService::RankService()
    : RankService([](
          const std::string& url,
          const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        return client.Get(url, headers);
      }) {}

RankService::RankService(RankHttpGet httpGet) : httpGet_(std::move(httpGet)) {}

nlohmann::json RankService::List() const {
  const auto result = httpGet_(
      "http://mobilecdn.kugou.com/api/v3/rank/list?plat=0",
      {{"Accept", "application/json"}, {"User-Agent", "EchoMusicNative/0.1"}});

  const auto upstream = ParseJsonOrError(result, "native_rank_list_failed");
  if (upstream.contains("error_code")) return ErrorList(upstream.value("error", ""), result.statusCode);

  const auto data = upstream.value("data", nlohmann::json::object());
  const auto info = data.value("info", nlohmann::json::array());
  const auto total = data.value("total", info.is_array() ? static_cast<int>(info.size()) : 0);
  return {
      {"status", 1},
      {"error", upstream.value("error", "")},
      {"data", {{"info", info}, {"list", info}, {"total", total}}},
      {"raw", upstream},
  };
}

nlohmann::json RankService::GetSongs(int rankId, int page, int pageSize) const {
  page = Clamp(page, 1, 1000);
  pageSize = Clamp(pageSize, 1, 200);
  if (rankId <= 0) return EmptySongs(rankId, page, pageSize);

  const auto url = "http://mobilecdn.kugou.com/api/v3/rank/song?rankid=" + std::to_string(rankId) +
                   "&page=" + std::to_string(page) + "&pagesize=" + std::to_string(pageSize);
  const auto result = httpGet_(
      url,
      {{"Accept", "application/json"}, {"User-Agent", "EchoMusicNative/0.1"}});

  const auto upstream = ParseJsonOrError(result, "native_rank_audio_failed");
  if (upstream.contains("error_code")) {
    return ErrorSongs(rankId, page, pageSize, upstream.value("error", ""), result.statusCode);
  }

  const auto data = upstream.value("data", nlohmann::json::object());
  const auto info = data.value("info", nlohmann::json::array());
  nlohmann::json songs = nlohmann::json::array();
  if (info.is_array()) {
    for (const auto& raw : info) {
      if (raw.is_object()) songs.push_back(NormalizeRankSong(raw));
    }
  }

  const auto total = data.value("total", static_cast<int>(songs.size()));
  return {
      {"status", 1},
      {"error", upstream.value("error", "")},
      {"data",
       {
           {"info", songs},
           {"list", songs},
           {"songs", songs},
           {"total", total},
           {"rankid", rankId},
           {"page", page},
           {"pagesize", pageSize},
       }},
      {"raw", upstream},
  };
}

}  // namespace echo::core
