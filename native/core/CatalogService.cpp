#include "echo/core/CatalogService.h"

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

std::string ExtractJsonBody(std::string body) {
  const auto first = body.find('{');
  const auto last = body.rfind('}');
  if (first == std::string::npos || last == std::string::npos || first > last) return body;
  return body.substr(first, last - first + 1);
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

nlohmann::json EmptyPage(std::string id, int page, int pageSize) {
  return {
      {"status", 1},
      {"data",
       {
           {"info", nlohmann::json::array()},
           {"list", nlohmann::json::array()},
           {"songs", nlohmann::json::array()},
           {"total", 0},
           {"page", page},
           {"pagesize", pageSize},
           {"id", std::move(id)},
       }},
  };
}

nlohmann::json ErrorPayload(
    std::string code,
    const std::string& message,
    long upstreamStatus = 0) {
  nlohmann::json body = {
      {"status", 0},
      {"error_code", std::move(code)},
      {"error", message},
      {"data", {{"info", nlohmann::json::array()}, {"list", nlohmann::json::array()}, {"total", 0}}},
  };
  if (upstreamStatus > 0) body["upstream_status"] = upstreamStatus;
  return body;
}

nlohmann::json ParseJsonResult(const HttpResult& result, const std::string& errorCode) {
  if (!result.error.empty()) return ErrorPayload(errorCode, result.error, result.statusCode);
  if (result.statusCode < 200 || result.statusCode >= 300) {
    return ErrorPayload(errorCode, "Kugou catalog upstream returned an error", result.statusCode);
  }

  try {
    return nlohmann::json::parse(ExtractJsonBody(result.body));
  } catch (const nlohmann::json::exception& error) {
    return ErrorPayload(errorCode, std::string("Invalid Kugou catalog JSON: ") + error.what());
  }
}

nlohmann::json NormalizeSong(const nlohmann::json& raw) {
  nlohmann::json item = raw;
  const auto filename = ReadString(raw, "filename");
  const auto separator = filename.find(" - ");
  const auto transParam = raw.value("trans_param", nlohmann::json::object());

  item["hash"] = ReadString(raw, "hash");
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
  item["duration"] = ReadInt(raw, "duration");
  item["timelen"] = ReadInt(raw, "duration") * 1000;
  item["audio_id"] = ReadInt(raw, "audio_id");
  item["album_audio_id"] = ReadInt(raw, "album_audio_id");
  item["mixsongid"] = ReadInt(raw, "album_audio_id");
  item["mvhash"] = ReadString(raw, "mvhash");
  item["cover"] = transParam.value("union_cover", ReadString(raw, "album_sizable_cover"));
  item["HQ"] = {{"Hash", ReadString(raw, "320hash")}};
  item["SQ"] = {{"Hash", ReadString(raw, "sqhash")}};
  item["Res"] = {{"Hash", ReadString(raw, "reshash")}};
  return item;
}

nlohmann::json NormalizeAlbum(const nlohmann::json& raw) {
  nlohmann::json item = raw;
  const auto id = ReadInt(raw, "albumid", ReadInt(raw, "album_id"));
  const auto name = ReadString(raw, "albumname").empty() ? ReadString(raw, "album_name") : ReadString(raw, "albumname");
  const auto singer = ReadString(raw, "singername").empty() ? ReadString(raw, "author_name") : ReadString(raw, "singername");
  const auto image = ReadString(raw, "imgurl").empty() ? ReadString(raw, "sizable_cover") : ReadString(raw, "imgurl");

  item["albumid"] = id;
  item["album_id"] = id;
  item["AlbumId"] = id;
  item["albumname"] = name;
  item["album_name"] = name;
  item["AlbumName"] = name;
  item["name"] = name;
  item["singername"] = singer;
  item["SingerName"] = singer;
  item["singerid"] = ReadInt(raw, "singerid");
  item["SingerId"] = ReadInt(raw, "singerid");
  item["imgurl"] = image;
  item["sizable_cover"] = image;
  item["intro"] = ReadString(raw, "intro");
  item["songcount"] = ReadInt(raw, "songcount");
  item["publishtime"] = ReadString(raw, "publishtime");
  item["playcount"] = ReadInt(raw, "play_count", ReadInt(raw, "playcount"));
  item["collectcount"] = ReadInt(raw, "collectcount");
  return item;
}

nlohmann::json NormalizeArtist(const nlohmann::json& raw) {
  nlohmann::json item = raw;
  const auto id = ReadInt(raw, "singerid", ReadInt(raw, "author_id"));
  const auto name = ReadString(raw, "singername").empty() ? ReadString(raw, "author_name") : ReadString(raw, "singername");
  const auto intro = ReadString(raw, "profile").empty() ? ReadString(raw, "intro") : ReadString(raw, "profile");

  item["singerid"] = id;
  item["author_id"] = id;
  item["AuthorId"] = id;
  item["singername"] = name;
  item["author_name"] = name;
  item["AuthorName"] = name;
  item["name"] = name;
  item["intro"] = intro;
  item["profile"] = intro;
  item["imgurl"] = ReadString(raw, "imgurl");
  item["avatar"] = ReadString(raw, "imgurl");
  item["songcount"] = ReadInt(raw, "songcount");
  item["albumcount"] = ReadInt(raw, "albumcount");
  item["mvcount"] = ReadInt(raw, "mvcount");
  return item;
}

}  // namespace

CatalogService::CatalogService()
    : CatalogService([](
          const std::string& url,
          const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        return client.Get(url, headers);
      }) {}

CatalogService::CatalogService(CatalogHttpGet httpGet) : httpGet_(std::move(httpGet)) {}

nlohmann::json CatalogService::GetAlbumDetail(std::string id) const {
  id = Trim(std::move(id));
  if (id.empty()) return ErrorPayload("native_album_detail_failed", "Missing album id");

  const auto result = httpGet_(
      "http://mobilecdn.kugou.com/api/v3/album/info?version=9108&plat=0&albumid=" + UrlEncode(id),
      {{"Accept", "application/json"}, {"User-Agent", "EchoMusicNative/0.1"}});
  auto upstream = ParseJsonResult(result, "native_album_detail_failed");
  if (upstream.value("status", 1) == 0 && upstream.contains("error_code")) return upstream;

  const auto data = upstream.value("data", nlohmann::json::object());
  const auto album = data.is_object() ? NormalizeAlbum(data) : nlohmann::json::object();
  return {{"status", 1}, {"data", {{"info", nlohmann::json::array({album})}, {"list", nlohmann::json::array({album})}}}, {"raw", upstream}};
}

nlohmann::json CatalogService::GetAlbumSongs(std::string id, int page, int pageSize) const {
  id = Trim(std::move(id));
  page = Clamp(page, 1, 1000);
  pageSize = Clamp(pageSize, 1, 200);
  if (id.empty()) return EmptyPage(id, page, pageSize);

  const auto url = "http://mobilecdn.kugou.com/api/v3/album/song?version=9108&plat=0&area_code=1&with_res_tag=1&albumid=" +
                   UrlEncode(id) + "&page=" + std::to_string(page) + "&pagesize=" + std::to_string(pageSize);
  auto upstream = ParseJsonResult(
      httpGet_(url, {{"Accept", "application/json"}, {"User-Agent", "EchoMusicNative/0.1"}}),
      "native_album_songs_failed");
  if (upstream.value("status", 1) == 0 && upstream.contains("error_code")) return upstream;

  const auto data = upstream.value("data", nlohmann::json::object());
  const auto info = data.value("info", nlohmann::json::array());
  nlohmann::json songs = nlohmann::json::array();
  if (info.is_array()) {
    for (const auto& raw : info) {
      if (raw.is_object()) songs.push_back(NormalizeSong(raw));
    }
  }
  return {{"status", 1}, {"data", {{"info", songs}, {"list", songs}, {"songs", songs}, {"total", data.value("total", static_cast<int>(songs.size()))}, {"page", page}, {"pagesize", pageSize}}}, {"raw", upstream}};
}

nlohmann::json CatalogService::GetArtistDetail(std::string id) const {
  id = Trim(std::move(id));
  if (id.empty()) return ErrorPayload("native_artist_detail_failed", "Missing artist id");

  const auto url = "http://mobilecdn.kugou.com/api/v3/singer/info?version=9108&plat=0&singerid=" + UrlEncode(id);
  auto upstream = ParseJsonResult(
      httpGet_(url, {{"Accept", "application/json"}, {"User-Agent", "EchoMusicNative/0.1"}}),
      "native_artist_detail_failed");
  if (upstream.value("status", 1) == 0 && upstream.contains("error_code")) return upstream;

  const auto data = upstream.value("data", nlohmann::json::object());
  const auto artist = data.is_object() ? NormalizeArtist(data) : nlohmann::json::object();
  return {{"status", 1}, {"data", {{"info", nlohmann::json::array({artist})}, {"list", nlohmann::json::array({artist})}}}, {"raw", upstream}};
}

nlohmann::json CatalogService::GetArtistSongs(std::string id, int page, int pageSize, std::string sort) const {
  id = Trim(std::move(id));
  page = Clamp(page, 1, 1000);
  pageSize = Clamp(pageSize, 1, 200);
  const auto sortValue = Trim(std::move(sort)) == "new" ? "2" : "1";
  if (id.empty()) return EmptyPage(id, page, pageSize);

  const auto url = "http://mobilecdn.kugou.com/api/v3/singer/song?version=9108&plat=0&with_res_tag=1&singerid=" +
                   UrlEncode(id) + "&page=" + std::to_string(page) + "&pagesize=" + std::to_string(pageSize) +
                   "&sort=" + sortValue;
  auto upstream = ParseJsonResult(
      httpGet_(url, {{"Accept", "application/json"}, {"User-Agent", "EchoMusicNative/0.1"}}),
      "native_artist_songs_failed");
  if (upstream.value("status", 1) == 0 && upstream.contains("error_code")) return upstream;

  const auto data = upstream.value("data", nlohmann::json::object());
  const auto info = data.value("info", nlohmann::json::array());
  nlohmann::json songs = nlohmann::json::array();
  if (info.is_array()) {
    for (const auto& raw : info) {
      if (raw.is_object()) songs.push_back(NormalizeSong(raw));
    }
  }
  return {{"status", 1}, {"data", {{"info", songs}, {"list", songs}, {"songs", songs}, {"total", data.value("total", static_cast<int>(songs.size()))}, {"page", page}, {"pagesize", pageSize}}}, {"raw", upstream}};
}

nlohmann::json CatalogService::GetArtistAlbums(std::string id, int page, int pageSize, std::string sort) const {
  id = Trim(std::move(id));
  page = Clamp(page, 1, 1000);
  pageSize = Clamp(pageSize, 1, 100);
  const auto sortValue = Trim(std::move(sort)) == "new" ? "1" : "3";
  if (id.empty()) return EmptyPage(id, page, pageSize);

  const auto url = "http://mobilecdn.kugou.com/api/v3/singer/album?version=9108&plat=0&singerid=" +
                   UrlEncode(id) + "&page=" + std::to_string(page) + "&pagesize=" + std::to_string(pageSize) +
                   "&sort=" + sortValue;
  auto upstream = ParseJsonResult(
      httpGet_(url, {{"Accept", "application/json"}, {"User-Agent", "EchoMusicNative/0.1"}}),
      "native_artist_albums_failed");
  if (upstream.value("status", 1) == 0 && upstream.contains("error_code")) return upstream;

  const auto data = upstream.value("data", nlohmann::json::object());
  const auto info = data.value("info", nlohmann::json::array());
  nlohmann::json albums = nlohmann::json::array();
  if (info.is_array()) {
    for (const auto& raw : info) {
      if (raw.is_object()) albums.push_back(NormalizeAlbum(raw));
    }
  }
  return {{"status", 1}, {"data", {{"info", albums}, {"list", albums}, {"total", data.value("total", static_cast<int>(albums.size()))}, {"page", page}, {"pagesize", pageSize}}}, {"raw", upstream}};
}

}  // namespace echo::core
