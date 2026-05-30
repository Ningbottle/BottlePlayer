#include "echo/core/PlaylistService.h"
#include "echo/core/Crypto.h"
#include "echo/core/DeviceService.h"
#include "echo/core/KuGouProfile.h"
#include "echo/core/StringUtils.h"

#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <initializer_list>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace echo::core {
namespace {

std::string Base64Encode(const std::vector<BYTE>& data) {
  DWORD b64Len = 0;
  if (!CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &b64Len)) {
    return {};
  }
  std::string b64Str(b64Len, '\0');
  if (!CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &b64Str[0], &b64Len)) {
    return {};
  }
  while (!b64Str.empty() && (b64Str.back() == '\0' || b64Str.back() == '\r' || b64Str.back() == '\n')) {
    b64Str.pop_back();
  }
  return b64Str;
}

std::vector<BYTE> Base64Decode(const std::string& b64Str) {
  DWORD decodedLen = 0;
  if (!CryptStringToBinaryA(b64Str.data(), static_cast<DWORD>(b64Str.size()), CRYPT_STRING_BASE64, nullptr, &decodedLen, nullptr, nullptr)) {
    return {};
  }
  std::vector<BYTE> decoded(decodedLen);
  if (!CryptStringToBinaryA(b64Str.data(), static_cast<DWORD>(b64Str.size()), CRYPT_STRING_BASE64, decoded.data(), &decodedLen, nullptr, nullptr)) {
    return {};
  }
  return decoded;
}

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

std::string ReadString(const nlohmann::json& value, std::string_view key) {
  if (!value.contains(key)) return "";
  const auto& item = value.at(key);
  if (item.is_string()) return item.get<std::string>();
  if (item.is_number_integer()) return std::to_string(item.get<std::int64_t>());
  if (item.is_number_unsigned()) return std::to_string(item.get<std::uint64_t>());
  if (item.is_number_float()) return std::to_string(item.get<double>());
  return "";
}

std::string ReadFirstString(const nlohmann::json& value, std::initializer_list<std::string_view> keys) {
  for (const auto key : keys) {
    const auto text = ReadString(value, key);
    if (!text.empty()) return text;
  }
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

  // Two upstream shapes:
  //   A) mobilecdn special/song:  filename="姝屾墜 - 姝屽悕", songname, singername
  //   B) pubsongs/v2:             name="姝屾墜 - 姝屽悕", singerinfo[{name}], albuminfo{name}
  //                               (filename usually empty)
  std::string filename = ReadString(raw, "filename");
  if (filename.empty()) filename = ReadString(raw, "name");
  const auto separator = filename.find(" - ");

  item["songname"] = ReadString(raw, "songname");
  if (item["songname"].get<std::string>().empty() && separator != std::string::npos) {
    item["songname"] = filename.substr(separator + 3);
  }
  if (item["songname"].get<std::string>().empty() && raw.contains("name")) {
    item["songname"] = ReadString(raw, "name");
  }

  item["singername"] = ReadString(raw, "singername");
  if (item["singername"].get<std::string>().empty() && separator != std::string::npos) {
    item["singername"] = filename.substr(0, separator);
  }
  if (item["singername"].get<std::string>().empty() && raw.contains("singerinfo") &&
      raw["singerinfo"].is_array() && !raw["singerinfo"].empty()) {
    std::string joined;
    for (const auto& si : raw["singerinfo"]) {
      if (si.is_object() && si.contains("name") && si["name"].is_string()) {
        if (!joined.empty()) joined += "、";
        joined += si["name"].get<std::string>();
      }
    }
    if (!joined.empty()) item["singername"] = joined;
  }

  item["filename"] = filename;

  std::string albumName = ReadString(raw, "album_name");
  std::string albumId = ReadString(raw, "album_id");
  if (raw.contains("albuminfo") && raw["albuminfo"].is_object()) {
    const auto& albumInfo = raw["albuminfo"];
    if (albumName.empty() && albumInfo.contains("name") && albumInfo["name"].is_string()) {
      albumName = albumInfo["name"].get<std::string>();
    }
    if (albumId.empty() && albumInfo.contains("id")) {
      const auto& aid = albumInfo["id"];
      if (aid.is_string()) albumId = aid.get<std::string>();
      else if (aid.is_number()) albumId = std::to_string(aid.get<std::int64_t>());
    }
  }
  item["album_name"] = albumName;
  item["album_id"] = albumId;

  item["timelen"] = ReadInt(raw, "duration") * 1000;
  item["duration"] = ReadInt(raw, "duration");
  item["hash"] = ReadString(raw, "hash");
  item["mvhash"] = ReadString(raw, "mvhash");
  item["mixsongid"] = ReadInt(raw, "album_audio_id", ReadInt(raw, "mixsongid"));
  item["album_audio_id"] = ReadInt(raw, "album_audio_id", ReadInt(raw, "mixsongid"));
  item["audio_id"] = ReadInt(raw, "audio_id");
  item["fileid"] = ReadInt(raw, "audio_id");

  std::string cover = transParam.value("union_cover", std::string{});
  if (cover.empty()) cover = ReadString(raw, "cover");
  item["cover"] = cover;

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

std::string UserPlaylistId(const nlohmann::json& raw) {
  return ReadFirstString(
      raw,
      {
          "global_collection_id",
          "global_collectionid",
          "listid",
          "list_id",
          "specialid",
          "special_id",
          "id",
          "list_create_listid",
          "collection_id",
          "gid",
      });
}

std::string UserPlaylistName(const nlohmann::json& raw) {
  return ReadFirstString(
      raw,
      {
          "name",
          "listname",
          "list_name",
          "specialname",
          "special_name",
          "title",
          "filename",
          "list_create_name",
      });
}

bool LooksLikeUserPlaylist(const nlohmann::json& raw) {
  return raw.is_object() && (!UserPlaylistId(raw).empty() || !UserPlaylistName(raw).empty());
}

nlohmann::json NormalizeUserPlaylistMeta(const nlohmann::json& raw) {
  nlohmann::json item = raw;
  const auto id = UserPlaylistId(raw);
  const auto name = UserPlaylistName(raw);
  const auto image = ReadFirstString(
      raw,
      {
          "imgurl",
          "pic",
          "pic_url",
          "cover",
          "image",
          "sizable_cover",
          "sizable_pic",
      });

  item["id"] = id;
  item["global_collection_id"] = id;
  item["listid"] = id;
  item["specialid"] = id;
  item["name"] = name.empty() ? "无标题歌单" : name;
  item["listname"] = item["name"];
  item["specialname"] = item["name"];
  item["title"] = item["name"];
  item["imgurl"] = image;
  item["cover"] = image;
  item["pic_url"] = image;
  item["songcount"] = ReadInt(raw, "songcount", ReadInt(raw, "song_count", ReadInt(raw, "count")));
  item["playcount"] = ReadInt(raw, "playcount", ReadInt(raw, "play_count"));
  return item;
}

void PushUserPlaylist(
    const nlohmann::json& raw,
    nlohmann::json& playlists,
    std::unordered_set<std::string>& seen) {
  const auto id = UserPlaylistId(raw);
  if (id.empty() || seen.contains(id)) return;
  seen.insert(id);
  playlists.push_back(NormalizeUserPlaylistMeta(raw));
}

void CollectUserPlaylists(
    const nlohmann::json& node,
    nlohmann::json& playlists,
    std::unordered_set<std::string>& seen,
    int depth = 0) {
  if (depth > 5 || node.is_null()) return;

  if (node.is_array()) {
    bool hasPlaylistItems = false;
    for (const auto& item : node) {
      if (LooksLikeUserPlaylist(item)) {
        hasPlaylistItems = true;
        break;
      }
    }

    if (hasPlaylistItems) {
      for (const auto& item : node) {
        if (LooksLikeUserPlaylist(item)) PushUserPlaylist(item, playlists, seen);
      }
      return;
    }

    for (const auto& item : node) {
      CollectUserPlaylists(item, playlists, seen, depth + 1);
    }
    return;
  }

  if (!node.is_object()) return;

  if (LooksLikeUserPlaylist(node)) {
    PushUserPlaylist(node, playlists, seen);
  }

  constexpr std::array<std::string_view, 10> keys = {
      "data",
      "list",
      "lists",
      "info",
      "special_list",
      "specialList",
      "cloud_list",
      "cloudList",
      "playlist",
      "playlists",
  };
  for (const auto key : keys) {
    if (node.contains(key)) {
      CollectUserPlaylists(node.at(key), playlists, seen, depth + 1);
    }
  }
}

int ReadTotal(const nlohmann::json& upstream, const nlohmann::json& playlists) {
  if (upstream.contains("data") && upstream["data"].is_object()) {
    const auto& data = upstream["data"];
    const auto total = ReadInt(data, "total", ReadInt(data, "total_count", ReadInt(data, "count", -1)));
    if (total >= 0) return total;
  }
  return ReadInt(upstream, "total", ReadInt(upstream, "total_count", static_cast<int>(playlists.size())));
}

nlohmann::json NormalizeUserPlaylistsResponse(nlohmann::json upstream, int page, int pageSize) {
  nlohmann::json playlists = nlohmann::json::array();
  std::unordered_set<std::string> seen;
  CollectUserPlaylists(upstream, playlists, seen);

  int status = ReadInt(upstream, "status", playlists.empty() ? 0 : 1);
  if (ReadInt(upstream, "errcode", -1) == 0 || ReadInt(upstream, "error_code", -1) == 0) {
    status = 1;
  }
  if (!playlists.empty()) {
    status = 1;
  }

  if (!upstream.contains("data") || !upstream["data"].is_object()) {
    upstream["data"] = nlohmann::json::object();
  }
  auto& data = upstream["data"];
  data["list"] = playlists;
  data["lists"] = playlists;
  data["info"] = playlists;
  data["total"] = ReadTotal(upstream, playlists);
  data["page"] = page;
  data["pagesize"] = pageSize;
  upstream["status"] = status;
  return upstream;
}

}  // namespace

PlaylistService::PlaylistService()
    : PlaylistService(
          [](const std::string& url,
             const std::unordered_map<std::string, std::string>& headers) {
            HttpClient client;
            return client.Get(url, headers);
          },
          [](const std::string& url,
             const std::string& body,
             const std::unordered_map<std::string, std::string>& headers) {
            HttpClient client;
            return client.Post(url, body, headers);
          }) {}

PlaylistService::PlaylistService(PlaylistHttpGet httpGet)
    : httpGet_(std::move(httpGet)),
      httpPost_([](const std::string& url,
                   const std::string& body,
                   const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        return client.Post(url, body, headers);
      }) {}

PlaylistService::PlaylistService(PlaylistHttpGet httpGet, PlaylistHttpPost httpPost)
    : httpGet_(std::move(httpGet)), httpPost_(std::move(httpPost)) {}

nlohmann::json PlaylistService::GetTracks(std::string id, int page, int pageSize) const {
  return GetTracks(DeviceInfo{}, std::move(id), page, pageSize);
}

nlohmann::json PlaylistService::GetTracks(
    const DeviceInfo& device,
    std::string id,
    int page,
    int pageSize) const {
  id = Trim(std::move(id));
  page = Clamp(page, 1, 1000);
  pageSize = Clamp(pageSize, 1, 200);

  if (id.empty() || id == "0" || id == "null") return EmptyTracks(id, page, pageSize);

  // User-collection playlists from /user/playlist come back as
  // "collection_3_<userid>_<listid>_0" (a global_collection_id). The
  // legacy mobilecdn/special/song endpoint doesn't accept these 鈥?it
  // returns "鍙傛暟涓嶅悎娉? (invalid params). Use the modern signed pubsongs
  // endpoint with `global_collection_id` for those.
  const bool isUserCollection = id.rfind("collection_", 0) == 0;
  if (isUserCollection) {
    const auto beginIdx = std::to_string((page - 1) * pageSize);
    const auto profile = GetKuGouProfile(KuGouEdition::Concept);
    const std::string appid = profile.appid;
    const std::string clientver = profile.clientver;
    const auto clienttime = std::to_string(std::time(nullptr));
    std::unordered_map<std::string, std::string> params = {
        {"appid", appid},
        {"clientver", clientver},
        {"clienttime", clienttime},
        {"plat", "1"},
        {"dfid", device.dfid.empty() ? "-" : device.dfid},
        {"mid", ResolveAndroidMid(device)},
        {"uuid", "-"},
        {"global_collection_id", id},
        {"begin_idx", beginIdx},
        {"pagesize", std::to_string(pageSize)},
        {"area_code", "1"},
    };
    params["signature"] = SignatureAndroidParams(params, "", profile.saltKind);

    std::ostringstream urlStream;
    // Note: pubsongs endpoint is hosted on pubsongs.kugou.com directly
    // (NOT under gateway.kugou.com 鈥?gateway returns 404).
    urlStream << "https://pubsongs.kugou.com/v2/get_other_list_file_nofilt?";
    bool first = true;
    for (const auto& [key, value] : params) {
      if (!first) urlStream << "&";
      urlStream << key << "=" << UrlEncode(value);
      first = false;
    }

    const auto result = httpGet_(
        urlStream.str(),
        {
            {"Accept", "application/json"},
            {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
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

    // Response shape: { status:1, data:{ info:[ {hash,filename,...} ], count } }
    const auto data = upstream.value("data", nlohmann::json::object());
    const auto info = data.value("info", data.value("songs", nlohmann::json::array()));
    nlohmann::json songs = nlohmann::json::array();
    if (info.is_array()) {
      for (const auto& raw : info) {
        if (raw.is_object()) songs.push_back(NormalizeTrack(raw));
      }
    }
    const auto total = data.value("count", data.value("total", static_cast<int>(songs.size())));
    return {
        {"status", upstream.value("status", 1)},
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

nlohmann::json PlaylistService::GetPlaylistDetail(
    const std::string& id,
    const std::string& userId,
    const std::string& token) const {
  return GetPlaylistDetail(DeviceInfo{}, id, userId, token);
}

nlohmann::json PlaylistService::GetPlaylistDetail(
    const DeviceInfo& device,
    const std::string& id,
    const std::string& userId,
    const std::string& token) const {
  if (id.empty()) {
    return {{"status", 0}, {"error", "empty playlist id"}, {"data", nullptr}};
  }

  nlohmann::json dataPayload = {
      {"data", nlohmann::json::array({{"global_collection_id", id}})},
      {"userid", userId.empty() ? "0" : userId},
      {"token", token}
  };
  const std::string body = dataPayload.dump();

  const auto clienttime = std::to_string(std::time(nullptr));
  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  const std::string appid = profile.appid;
  const std::string clientver = profile.clientver;
  std::unordered_map<std::string, std::string> params = {
      {"appid", appid},
      {"clientver", clientver},
      {"clienttime", clienttime},
      {"plat", "1"},
      {"userid", userId.empty() ? "0" : userId},
      {"token", token}
  };
  if (!device.dfid.empty()) params["dfid"] = device.dfid;
  params["mid"] = ResolveAndroidMid(device);
  params["uuid"] = "-";

  params["signature"] = SignatureAndroidParams(params, body, profile.saltKind);

  std::ostringstream urlStream;
  // Route via gateway.kugou.com + x-router (see GetUserPlaylists comment).
  urlStream << "https://gateway.kugou.com/v3/get_list_info?";
  bool first = true;
  for (const auto& [key, value] : params) {
    if (!first) urlStream << "&";
    urlStream << key << "=" << value;
    first = false;
  }

  const auto result = httpPost_(
      urlStream.str(),
      body,
      {
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
          {"x-router", "pubsongs.kugou.com"},
      });

  if (!result.error.empty()) {
    return {{"status", 0}, {"error", result.error}, {"data", nullptr}};
  }

  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return {{"status", 0}, {"error", std::string("JSON parse error: ") + e.what()}, {"data", nullptr}};
  }
}

nlohmann::json PlaylistService::GetUserPlaylists(
    const std::string& userId,
    const std::string& token,
    int page,
    int pageSize) const {
  return GetUserPlaylists(DeviceInfo{}, userId, token, page, pageSize);
}

nlohmann::json PlaylistService::GetUserPlaylists(
    const DeviceInfo& device,
    const std::string& userId,
    const std::string& token,
    int page,
    int pageSize) const {
  if (userId.empty() || userId == "0") {
    return {{"status", 0}, {"error", "not logged in"}, {"data", {{"list", nlohmann::json::array()}, {"total", 0}}}};
  }

  page = std::max(1, page);
  pageSize = std::max(1, std::min(pageSize, 100));

  nlohmann::json dataPayload = {
      {"userid", userId.empty() ? 0 : std::stoll(userId)},
      {"token", token},
      {"total_ver", 979},
      {"type", 2},
      {"page", page},
      {"pagesize", pageSize}
  };
  const std::string body = dataPayload.dump();

  const auto clienttime = std::to_string(std::time(nullptr));
  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  const std::string appid = profile.appid;
  const std::string clientver = profile.clientver;
  std::unordered_map<std::string, std::string> params = {
      {"appid", appid},
      {"clientver", clientver},
      {"clienttime", clienttime},
      {"plat", "1"},
      {"dfid", device.dfid.empty() ? "-" : device.dfid},
      {"mid", ResolveAndroidMid(device)},
      {"uuid", "-"},
      {"userid", userId},
      {"token", token}
  };
  params["signature"] = SignatureAndroidParams(params, body, profile.saltKind);

  std::ostringstream urlStream;
  // Reference (MakcRe/KuGouMusicApi util/request.js): the base URL is
  // gateway.kugou.com; the x-router header tells KuGou's gateway which
  // backend service to proxy to. Hitting cloudlist.service.kugou.com
  // directly gives WinHttp 12175 (SSL certificate validation failure).
  urlStream << "https://gateway.kugou.com/v7/get_all_list?";
  bool first = true;
  for (const auto& [key, value] : params) {
    if (!first) urlStream << "&";
    urlStream << key << "=" << value;
    first = false;
  }

  const auto result = httpPost_(
      urlStream.str(),
      body,
      {
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
          {"dfid", device.dfid.empty() ? "-" : device.dfid},
          {"clienttime", clienttime},
          {"mid", ResolveAndroidMid(device)},
          {"kg-rc", "1"},
          {"kg-thash", "5d816a0"},
          {"kg-rec", "1"},
          {"kg-rf", "B9EDA08A64250DEFFBCADDEE00F8F25F"},
          {"x-router", "cloudlist.service.kugou.com"},
      });

  if (!result.error.empty()) {
    return {{"status", 0}, {"error", result.error}, {"data", {{"list", nlohmann::json::array()}, {"total", 0}}}};
  }

  try {
    return NormalizeUserPlaylistsResponse(nlohmann::json::parse(result.body), page, pageSize);
  } catch (const nlohmann::json::exception& e) {
    return {{"status", 0}, {"error", std::string("JSON parse error: ") + e.what()}, {"data", {{"list", nlohmann::json::array()}, {"total", 0}}}};
  }
}

nlohmann::json PlaylistService::AddPlaylist(
    const std::string& userId,
    const std::string& token,
    const std::string& name,
    int type,
    int source,
    const std::string& createUserId,
    const std::string& createListId,
    const std::string& createGid) const {
  return AddPlaylist(DeviceInfo{}, userId, token, name, type, source, createUserId, createListId, createGid);
}

nlohmann::json PlaylistService::AddPlaylist(
    const DeviceInfo& device,
    const std::string& userId,
    const std::string& token,
    const std::string& name,
    int type,
    int source,
    const std::string& createUserId,
    const std::string& createListId,
    const std::string& createGid) const {
  
  nlohmann::json dataPayload = {
      {"userid", userId.empty() ? 0 : std::stoll(userId)},
      {"token", token},
      {"total_ver", 0},
      {"name", name},
      {"type", type},
      {"source", source},
      {"is_pri", 0},
      {"list_create_userid", createUserId.empty() ? 0 : std::stoll(createUserId)},
      {"list_create_listid", createListId.empty() ? 0 : std::stoll(createListId)},
      {"list_create_gid", createGid},
      {"from_shupinmv", 0}
  };

  if (type == 0) {
    dataPayload["is_pri"] = 0;
  }

  const std::string body = dataPayload.dump();
  const auto clienttime = std::to_string(std::time(nullptr));

  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  const std::string appid = profile.appid;
  const std::string clientver = profile.clientver;

  std::unordered_map<std::string, std::string> params;
  params["appid"] = appid;
  params["clientver"] = clientver;
  params["clienttime"] = clienttime;
  params["mid"] = ResolveAndroidMid(device);
  params["uuid"] = "-";
  params["dfid"] = device.dfid.empty() ? "-" : device.dfid;
  if (!userId.empty()) params["userid"] = userId;
  if (!token.empty()) params["token"] = token;

  if (type == 0) {
    params["last_time"] = clienttime;
    params["last_area"] = "gztx";
  }

  params["signature"] = SignatureAndroidParams(params, body, profile.saltKind);

  std::ostringstream urlStream;
  urlStream << "https://gateway.kugou.com/cloudlist.service/v5/add_list?";
  bool first = true;
  for (const auto& [key, value] : params) {
    if (!first) urlStream << "&";
    urlStream << key << "=" << UrlEncode(value);
    first = false;
  }

  const auto result = httpPost_(
      urlStream.str(),
      body,
      {
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"}
      });

  if (!result.error.empty()) {
    return {{"status", 0}, {"error", result.error}};
  }

  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return {{"status", 0}, {"error", std::string("JSON parse error: ") + e.what()}};
  }
}

nlohmann::json PlaylistService::DeletePlaylist(
    const std::string& userId,
    const std::string& token,
    long long listId) const {
  return DeletePlaylist(DeviceInfo{}, userId, token, listId);
}

nlohmann::json PlaylistService::DeletePlaylist(
    const DeviceInfo& device,
    const std::string& userId,
    const std::string& token,
    long long listId) const {
  
  nlohmann::json dataMap = {
      {"listid", listId},
      {"total_ver", 0},
      {"type", 1}
  };

  AesKeyPair aesKeyPair = PlaylistAesEncrypt(dataMap.dump());
  if (aesKeyPair.key.empty() || aesKeyPair.data.empty()) {
    return {{"status", 0}, {"error", "AES encryption failed"}};
  }

  nlohmann::json rsaPayload = {
      {"aes", aesKeyPair.key},
      {"uid", userId.empty() ? 0 : std::stoll(userId)},
      {"token", token}
  };
  std::string p = RsaPkcs1Encrypt(rsaPayload.dump());

  const auto clienttime = std::to_string(std::time(nullptr));
  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  const std::string appid = profile.appid;
  const std::string clientver = profile.clientver;

  std::unordered_map<std::string, std::string> paramsMap;
  paramsMap["clienttime"] = clienttime;
  paramsMap["mid"] = ResolveAndroidMid(device);
  paramsMap["key"] = SignParamsKey(clienttime, appid, clientver, profile.saltKind);
  paramsMap["last_area"] = "gztx";
  paramsMap["clientver"] = clientver;
  paramsMap["appid"] = appid;
  paramsMap["last_time"] = clienttime;
  paramsMap["p"] = p;

  std::vector<std::string> keys;
  keys.reserve(paramsMap.size());
  for (const auto& [k, _] : paramsMap) keys.push_back(k);
  std::sort(keys.begin(), keys.end());

  std::ostringstream urlStream;
  urlStream << "https://gateway.kugou.com/v2/delete_list?";
  bool first = true;
  for (const auto& key : keys) {
    if (!first) urlStream << "&";
    urlStream << key << "=" << UrlEncode(paramsMap[key]);
    first = false;
  }

  const auto result = httpPost_(
      urlStream.str(),
      aesKeyPair.data,
      {
          {"Accept", "application/json"},
          {"Content-Type", "text/plain"},
          {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
          {"x-router", "cloudlist.service.kugou.com"}
      });

  if (!result.error.empty()) {
    return {{"status", 0}, {"error", result.error}};
  }

  std::string base64Resp = Base64Encode(std::vector<BYTE>(result.body.begin(), result.body.end()));
  std::string decryptedBody = PlaylistAesDecrypt(base64Resp, aesKeyPair.key);

  if (decryptedBody.empty()) {
    return {{"status", 0}, {"error", "AES decryption of response failed"}};
  }

  try {
    return nlohmann::json::parse(decryptedBody);
  } catch (const nlohmann::json::exception& e) {
    return {{"status", 0}, {"error", std::string("JSON parse error of decrypted response: ") + e.what()}};
  }
}

nlohmann::json PlaylistService::AddPlaylistTracks(
    const std::string& userId,
    const std::string& token,
    const std::string& listId,
    const std::string& commaSeparatedTracks) const {
  return AddPlaylistTracks(DeviceInfo{}, userId, token, listId, commaSeparatedTracks);
}

nlohmann::json PlaylistService::AddPlaylistTracks(
    const DeviceInfo& device,
    const std::string& userId,
    const std::string& token,
    const std::string& listId,
    const std::string& commaSeparatedTracks) const {
  
  nlohmann::json resource = nlohmann::json::array();
  std::stringstream ss(commaSeparatedTracks);
  std::string track;
  while (std::getline(ss, track, ',')) {
    if (track.empty()) continue;
    std::vector<std::string> parts;
    std::stringstream tss(track);
    std::string part;
    while (std::getline(tss, part, '|')) {
      parts.push_back(part);
    }
    nlohmann::json item = {
        {"number", 1},
        {"name", parts.size() > 0 ? parts[0] : ""},
        {"hash", parts.size() > 1 ? parts[1] : ""},
        {"size", 0},
        {"sort", 0},
        {"timelen", 0},
        {"bitrate", 0},
        {"album_id", (parts.size() > 2 && !parts[2].empty()) ? std::stoll(parts[2]) : 0},
        {"mixsongid", (parts.size() > 3 && !parts[3].empty()) ? std::stoll(parts[3]) : 0}
    };
    resource.push_back(item);
  }

  nlohmann::json dataPayload = {
      {"userid", userId.empty() ? 0 : std::stoll(userId)},
      {"token", token},
      {"listid", listId.empty() ? 0 : std::stoll(listId)},
      {"list_ver", 0},
      {"type", 0},
      {"slow_upload", 1},
      {"scene", "false;null"},
      {"data", resource}
  };
  const std::string body = dataPayload.dump();
  const auto clienttime = std::to_string(std::time(nullptr));

  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  const std::string appid = profile.appid;
  const std::string clientver = profile.clientver;

  std::unordered_map<std::string, std::string> params;
  params["appid"] = appid;
  params["clientver"] = clientver;
  params["clienttime"] = clienttime;
  params["mid"] = ResolveAndroidMid(device);
  params["uuid"] = "-";
  params["dfid"] = device.dfid.empty() ? "-" : device.dfid;
  if (!userId.empty()) params["userid"] = userId;
  if (!token.empty()) params["token"] = token;
  params["last_time"] = clienttime;
  params["last_area"] = "gztx";

  params["signature"] = SignatureAndroidParams(params, body, profile.saltKind);

  std::ostringstream urlStream;
  urlStream << "https://gateway.kugou.com/cloudlist.service/v6/add_song?";
  bool first = true;
  for (const auto& [key, value] : params) {
    if (!first) urlStream << "&";
    urlStream << key << "=" << UrlEncode(value);
    first = false;
  }

  const auto result = httpPost_(
      urlStream.str(),
      body,
      {
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"}
      });

  if (!result.error.empty()) {
    return {{"status", 0}, {"error", result.error}};
  }

  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return {{"status", 0}, {"error", std::string("JSON parse error: ") + e.what()}};
  }
}

nlohmann::json PlaylistService::DeletePlaylistTracks(
    const std::string& userId,
    const std::string& token,
    const std::string& listId,
    const std::string& commaSeparatedFileIds) const {
  return DeletePlaylistTracks(DeviceInfo{}, userId, token, listId, commaSeparatedFileIds);
}

nlohmann::json PlaylistService::DeletePlaylistTracks(
    const DeviceInfo& device,
    const std::string& userId,
    const std::string& token,
    const std::string& listId,
    const std::string& commaSeparatedFileIds) const {
  
  nlohmann::json resource = nlohmann::json::array();
  std::stringstream ss(commaSeparatedFileIds);
  std::string idStr;
  while (std::getline(ss, idStr, ',')) {
    if (!idStr.empty()) {
      resource.push_back({{"fileid", std::stoll(idStr)}});
    }
  }

  nlohmann::json dataPayload = {
      {"listid", listId.empty() ? 0 : std::stoll(listId)},
      {"userid", userId.empty() ? 0 : std::stoll(userId)},
      {"data", resource},
      {"type", 0},
      {"token", token},
      {"list_ver", 0}
  };
  const std::string body = dataPayload.dump();
  const auto clienttime = std::to_string(std::time(nullptr));

  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  const std::string appid = profile.appid;
  const std::string clientver = profile.clientver;

  std::unordered_map<std::string, std::string> params;
  params["appid"] = appid;
  params["clientver"] = clientver;
  params["clienttime"] = clienttime;
  params["mid"] = ResolveAndroidMid(device);
  params["uuid"] = "-";
  params["dfid"] = device.dfid.empty() ? "-" : device.dfid;
  if (!userId.empty()) params["userid"] = userId;
  if (!token.empty()) params["token"] = token;

  params["signature"] = SignatureAndroidParams(params, body, profile.saltKind);

  std::ostringstream urlStream;
  urlStream << "https://gateway.kugou.com/v4/delete_songs?";
  bool first = true;
  for (const auto& [key, value] : params) {
    if (!first) urlStream << "&";
    urlStream << key << "=" << UrlEncode(value);
    first = false;
  }

  const auto result = httpPost_(
      urlStream.str(),
      body,
      {
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
          {"x-router", "cloudlist.service.kugou.com"}
      });

  if (!result.error.empty()) {
    return {{"status", 0}, {"error", result.error}};
  }

  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return {{"status", 0}, {"error", std::string("JSON parse error: ") + e.what()}};
  }
}

}  // namespace echo::core
