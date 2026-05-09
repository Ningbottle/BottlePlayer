#include "echo/core/PlaylistService.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

namespace echo::core {
namespace {

int Clamp(int value, int minValue, int maxValue) {
  return std::max(minValue, std::min(value, maxValue));
}

std::string Trim(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  std::size_t first = 0;
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
    ++first;
  }
  if (first > 0) value.erase(0, first);
  return value;
}

std::string UrlEncode(std::string_view value) {
  std::ostringstream stream;
  stream << std::uppercase << std::hex;
  for (const unsigned char ch : value) {
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
        ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      stream << static_cast<char>(ch);
    } else {
      stream << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }
  }
  return stream.str();
}

std::string ReadString(const nlohmann::json& value, std::string_view key) {
  if (!value.contains(key)) return "";
  const auto& item = value.at(key);
  if (item.is_string()) return item.get<std::string>();
  if (item.is_number_integer()) return std::to_string(item.get<std::int64_t>());
  if (item.is_number_unsigned()) return std::to_string(item.get<std::uint64_t>());
  if (item.is_number_float()) return std::to_string(item.get<double>());
  return "";
}

int ReadInt(const nlohmann::json& value, std::string_view key, int fallback = 0) {
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

nlohmann::json EmptyTracks(std::string id, int page, int pageSize) {
  const auto list = nlohmann::json::array();
  return {
      {"status", 1},
      {"data",
       {
           {"songs", list},
           {"info", list},
           {"list", list},
           {"songlist", list},
           {"total", 0},
           {"page", page},
           {"pagesize", pageSize},
           {"id", std::move(id)},
       }},
  };
}

nlohmann::json NormalizeTrack(const nlohmann::json& raw) {
  nlohmann::json item = raw;
  const auto transParam = raw.value("trans_param", nlohmann::json::object());
  const auto filename = ReadString(raw, "filename");
  const auto separator = filename.find(" - ");

  item["songname"] = ReadString(raw, "songname");
  if (item["songname"].get<std::string>().empty() && separator != std::string::npos) {
    item["songname"] = filename.substr(separator + 3);
  }

  item["singername"] = ReadString(raw, "singername");
  if (item["singername"].get<std::string>().empty() && separator != std::string::npos) {
    item["singername"] = filename.substr(0, separator);
  }

  item["filename"] = filename;
  item["album_name"] = ReadString(raw, "album_name");
  item["album_id"] = ReadString(raw, "album_id");
  item["timelen"] = ReadInt(raw, "duration") * 1000;
  item["duration"] = ReadInt(raw, "duration");
  item["hash"] = ReadString(raw, "hash");
  item["mvhash"] = ReadString(raw, "mvhash");
  item["mixsongid"] = ReadInt(raw, "album_audio_id");
  item["album_audio_id"] = ReadInt(raw, "album_audio_id");
  item["audio_id"] = ReadInt(raw, "audio_id");
  item["fileid"] = ReadInt(raw, "audio_id");
  item["cover"] = transParam.value("union_cover", "");

  item["HQ"] = {{"Hash", ReadString(raw, "320hash")}};
  item["SQ"] = {{"Hash", ReadString(raw, "sqhash")}};
  item["Res"] = {{"Hash", ReadString(raw, "reshash")}};
  return item;
}

nlohmann::json ErrorTracks(
    const std::string& id,
    int page,
    int pageSize,
    const std::string& message,
    long upstreamStatus = 0) {
  auto body = EmptyTracks(id, page, pageSize);
  body["status"] = 0;
  body["error_code"] = "native_playlist_tracks_failed";
  body["error"] = message;
  if (upstreamStatus > 0) body["upstream_status"] = upstreamStatus;
  return body;
}

nlohmann::json ErrorPlaylistDiscovery(
    std::string errorCode,
    const std::string& message,
    long upstreamStatus = 0) {
  nlohmann::json body = {
      {"status", 0},
      {"error_code", std::move(errorCode)},
      {"error", message},
      {"data", {{"list", nlohmann::json::array()}, {"info", nlohmann::json::array()}, {"total", 0}}},
  };
  if (upstreamStatus > 0) body["upstream_status"] = upstreamStatus;
  return body;
}

std::string ExtractJsonBody(std::string body) {
  const auto first = body.find('{');
  const auto last = body.rfind('}');
  if (first == std::string::npos || last == std::string::npos || first > last) return body;
  return body.substr(first, last - first + 1);
}

nlohmann::json NormalizeTag(const nlohmann::json& raw) {
  const auto tagId = ReadInt(raw, "special_tag_id", ReadInt(raw, "tag_id", ReadInt(raw, "id")));
  const auto id = ReadInt(raw, "id", tagId);
  const auto name = ReadString(raw, "name");
  nlohmann::json item = raw;
  item["tag_id"] = tagId;
  item["id"] = id;
  item["tag_name"] = name;
  item["name"] = name;
  item["bannerurl"] = ReadString(raw, "bannerurl");
  item["imgurl"] = ReadString(raw, "imgurl");
  return item;
}

nlohmann::json NormalizePlaylistMeta(const nlohmann::json& raw) {
  nlohmann::json item = raw;
  const auto specialId = ReadInt(raw, "specialid", ReadInt(raw, "id"));
  const auto name = ReadString(raw, "specialname").empty() ? ReadString(raw, "name") : ReadString(raw, "specialname");
  const auto image = ReadString(raw, "imgurl").empty() ? ReadString(raw, "cover") : ReadString(raw, "imgurl");

  item["specialid"] = specialId;
  item["id"] = specialId;
  item["specialname"] = name;
  item["name"] = name;
  item["title"] = name;
  item["imgurl"] = image;
  item["cover"] = image;
  item["pic_url"] = image;
  item["playcount"] = ReadInt(raw, "playcount");
  item["songcount"] = ReadInt(raw, "songcount");
  item["intro"] = ReadString(raw, "intro");
  const auto nickname =
      ReadString(raw, "nickname").empty() ? ReadString(raw, "username") : ReadString(raw, "nickname");
  item["nickname"] = nickname;
  item["username"] = ReadString(raw, "username").empty() ? nickname : ReadString(raw, "username");
  item["publishtime"] = ReadString(raw, "publishtime");
  return item;
}

}  // namespace

PlaylistService::PlaylistService()
    : PlaylistService([](
          const std::string& url,
          const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        return client.Get(url, headers);
      }) {}

PlaylistService::PlaylistService(PlaylistHttpGet httpGet) : httpGet_(std::move(httpGet)) {}

nlohmann::json PlaylistService::GetTracks(std::string id, int page, int pageSize) const {
  id = Trim(std::move(id));
  page = Clamp(page, 1, 1000);
  pageSize = Clamp(pageSize, 1, 200);

  if (id.empty() || id == "0" || id == "null") return EmptyTracks(id, page, pageSize);

  const auto url = "http://mobilecdn.kugou.com/api/v3/special/song?specialid=" + UrlEncode(id) +
                   "&page=" + std::to_string(page) + "&pagesize=" + std::to_string(pageSize);

  const auto result = httpGet_(
      url,
      {
          {"Accept", "application/json"},
          {"User-Agent", "EchoMusicNative/0.1"},
      });

  if (!result.error.empty()) return ErrorTracks(id, page, pageSize, result.error, result.statusCode);
  if (result.statusCode < 200 || result.statusCode >= 300) {
    return ErrorTracks(id, page, pageSize, "Kugou playlist upstream returned an error", result.statusCode);
  }

  nlohmann::json upstream;
  try {
    upstream = nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& error) {
    return ErrorTracks(id, page, pageSize, std::string("Invalid Kugou playlist JSON: ") + error.what());
  }

  const auto data = upstream.value("data", nlohmann::json::object());
  const auto info = data.value("info", nlohmann::json::array());
  nlohmann::json songs = nlohmann::json::array();
  if (info.is_array()) {
    for (const auto& raw : info) {
      if (raw.is_object()) songs.push_back(NormalizeTrack(raw));
    }
  }

  const auto total = data.value("total", static_cast<int>(songs.size()));
  return {
      {"status", 1},
      {"error", upstream.value("error", "")},
      {"data",
       {
           {"songs", songs},
           {"info", songs},
           {"list", songs},
           {"songlist", songs},
           {"total", total},
           {"page", page},
           {"pagesize", pageSize},
           {"id", id},
       }},
      {"raw", upstream},
  };
}

nlohmann::json PlaylistService::GetTags() const {
  const auto url =
      "http://mobilecdn.kugou.com/api/v3/tag/recommend?version=9108&plat=0&showtype=2&"
      "parentid=0&apiver=6&area_code=1&withsong=1&with_res_tag=1";

  const auto result = httpGet_(
      url,
      {
          {"Accept", "application/json"},
          {"User-Agent", "EchoMusicNative/0.1"},
      });

  if (!result.error.empty()) {
    return ErrorPlaylistDiscovery("native_playlist_tags_failed", result.error, result.statusCode);
  }
  if (result.statusCode < 200 || result.statusCode >= 300) {
    return ErrorPlaylistDiscovery(
        "native_playlist_tags_failed",
        "Kugou playlist tags upstream returned an error",
        result.statusCode);
  }

  nlohmann::json upstream;
  try {
    upstream = nlohmann::json::parse(ExtractJsonBody(result.body));
  } catch (const nlohmann::json::exception& error) {
    return ErrorPlaylistDiscovery(
        "native_playlist_tags_failed",
        std::string("Invalid Kugou playlist tags JSON: ") + error.what());
  }

  const auto data = upstream.value("data", nlohmann::json::object());
  const auto info = data.value("info", nlohmann::json::array());
  nlohmann::json tags = nlohmann::json::array();
  if (info.is_array()) {
    for (const auto& raw : info) {
      if (raw.is_object()) tags.push_back(NormalizeTag(raw));
    }
  }

  nlohmann::json list = nlohmann::json::array();
  list.push_back({
      {"tag_id", 0},
      {"id", 0},
      {"tag_name", "推荐"},
      {"name", "推荐"},
      {"son", tags},
      {"children", tags},
  });

  return {
      {"status", 1},
      {"data",
       {
           {"list", list},
           {"info", list},
           {"total", static_cast<int>(tags.size())},
       }},
      {"raw", upstream},
  };
}

nlohmann::json PlaylistService::GetTopPlaylists(int categoryId, int page, int pageSize, int sort) const {
  categoryId = std::max(0, categoryId);
  page = Clamp(page, 1, 1000);
  pageSize = Clamp(pageSize, 1, 100);
  sort = Clamp(sort, 1, 5);

  const auto url = "http://mobilecdn.kugou.com/api/v3/tag/specialList?plat=0&page=" +
                   std::to_string(page) + "&pagesize=" + std::to_string(pageSize) +
                   "&ugc=1&sort=" + std::to_string(sort) +
                   "&tagid=" + std::to_string(categoryId) +
                   "&id=0";

  const auto result = httpGet_(
      url,
      {
          {"Accept", "application/json"},
          {"User-Agent", "EchoMusicNative/0.1"},
      });

  if (!result.error.empty()) {
    return ErrorPlaylistDiscovery("native_top_playlist_failed", result.error, result.statusCode);
  }
  if (result.statusCode < 200 || result.statusCode >= 300) {
    return ErrorPlaylistDiscovery(
        "native_top_playlist_failed",
        "Kugou top playlist upstream returned an error",
        result.statusCode);
  }

  nlohmann::json upstream;
  try {
    upstream = nlohmann::json::parse(ExtractJsonBody(result.body));
  } catch (const nlohmann::json::exception& error) {
    return ErrorPlaylistDiscovery(
        "native_top_playlist_failed",
        std::string("Invalid Kugou top playlist JSON: ") + error.what());
  }

  const auto data = upstream.value("data", nlohmann::json::object());
  const auto info = data.value("info", nlohmann::json::array());
  nlohmann::json playlists = nlohmann::json::array();
  if (info.is_array()) {
    for (const auto& raw : info) {
      if (raw.is_object()) playlists.push_back(NormalizePlaylistMeta(raw));
    }
  }

  const auto total = data.value("total", static_cast<int>(playlists.size()));
  return {
      {"status", 1},
      {"data",
       {
           {"special_list", playlists},
           {"info", playlists},
           {"list", playlists},
           {"total", total},
           {"page", page},
           {"pagesize", pageSize},
           {"category_id", categoryId},
       }},
      {"raw", upstream},
  };
}

}  // namespace echo::core
