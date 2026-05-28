#include "echo/core/CompatApi.h"

#include <array>
#include <chrono>
#include <string_view>
#include <iostream>

#include "echo/core/CatalogService.h"
#include "echo/core/DeviceRegisterService.h"
#include "echo/core/DeviceService.h"
#include "echo/core/HomeService.h"
#include "echo/core/JsonHelpers.h"
#include "echo/core/LoginService.h"
#include "echo/core/LyricService.h"
#include "echo/core/PlaylistService.h"
#include "echo/core/PrivilegeService.h"
#include "echo/core/RankService.h"
#include "echo/core/SearchService.h"
#include "echo/core/SongUrlService.h"
#include "echo/core/UserService.h"
#include "echo/core/SongService.h"
#include "echo/core/PlayHistoryService.h"
#include "echo/core/UserCloudService.h"
#include "echo/storage/DeviceRepository.h"
#include "echo/storage/SessionRepository.h"

namespace echo::core {
namespace {

using namespace std::chrono;

constexpr std::array<std::string_view, 70> kKnownRoutes = {
    "/health",
    "/server/now",
    "/register/dev",
    "/login/qr/key",
    "/login/qr/create",
    "/login/qr/check",
    "/auth/logout",
    "/settings/device",
    "/captcha/sent",
    "/login/cellphone",
    "/login/wx/create",
    "/login/wx/check",
    "/login/openplat",
    "/user/detail",
    "/user/vip/detail",
    "/youth/day/vip",
    "/youth/day/vip/upgrade",
    "/youth/listen/song",
    "/youth/vip/ad",
    "/youth/month/vip/record",
    "/user/history",
    "/playhistory/upload",
    "/user/cloud",
    "/user/cloud/url",
    "/search",
    "/search/hot",
    "/search/default",
    "/search/suggest",
    "/search/lyric",
    "/lyric",
    "/song/url",
    "/privilege/lite",
    "/top/song",
    "/top/album",
    "/everyday/recommend",
    "/song/climax",
    "/song/ranking",
    "/song/ranking/filter",
    "/images/audio",
    "/playlist/recommend",
    "/playlist/detail",
    "/playlist/track/all",
    "/playlist/track/all/new",
    "/user/playlist",
    "/rank/list",
    "/playlist/tags",
    "/rank/top",
    "/top/playlist",
    "/top/ip",
    "/rank/audio",
    "/playlist/tracks/add",
    "/playlist/tracks/del",
    "/playlist/add",
    "/playlist/del",
    "/album/detail",
    "/album/songs",
    "/artist/detail",
    "/artist/audios",
    "/artist/albums",
    "/artist/follow",
    "/artist/unfollow",
    "/comment/music",
    "/comment/music/classify",
    "/comment/music/hotword",
    "/comment/playlist",
    "/comment/album",
    "/comment/floor",
    "/comment/count",
    "/favorite/count",
    "/video/url",
};

std::int64_t UnixSeconds() {
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::int64_t UnixMilliseconds() {
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

CompatResponse JsonResponse(nlohmann::json body, int httpStatus = 200) {
  std::cout << "[Debug] JsonResponse entry, httpStatus=" << httpStatus << std::endl;
  CompatResponse resp{httpStatus, "application/json; charset=utf-8", std::move(body)};
  std::cout << "[Debug] JsonResponse constructed, returning" << std::endl;
  return resp;
}

nlohmann::json EmptyPagedData() {
  return {
      {"status", 1},
      {"data",
       {
           {"lists", nlohmann::json::array()},
           {"list", nlohmann::json::array()},
           {"total", 0},
       }},
  };
}

std::string QueryValue(const QueryMap& query, const std::string& key, std::string fallback = "") {
  const auto it = query.find(key);
  return it == query.end() ? std::move(fallback) : it->second;
}

int QueryInt(const QueryMap& query, const std::string& key, int fallback) {
  const auto value = QueryValue(query, key);
  if (value.empty()) return fallback;
  try {
    return std::stoi(value);
  } catch (...) {
    return fallback;
  }
}

bool IsKuGouErrorCode(const nlohmann::json& body, int code) {
  if (!body.is_object() || !body.contains("error_code")) return false;
  const auto& value = body.at("error_code");
  if (value.is_number_integer()) return value.get<int>() == code;
  if (value.is_string()) {
    try {
      return std::stoi(value.get<std::string>()) == code;
    } catch (...) {
      return false;
    }
  }
  return false;
}

}  // namespace

CompatApi::CompatApi(storage::Database& database) : database_(database) {}

CompatApi::CompatApi(storage::Database& database, CompatApiHandlers handlers)
    : database_(database), handlers_(std::move(handlers)) {}

CompatResponse CompatApi::Handle(
    const std::string& method,
    const std::string& path,
    const QueryMap& query,
    const HeaderMap& headers,
    const std::string& body) {
  if (!IsKnownCompatRoute(path)) {
    return JsonResponse({{"status", 0}, {"error_code", 404}, {"error", "Unknown route"}}, 404);
  }

  return HandleKnownRoute(method, path, query, headers, body);
}

CompatResponse CompatApi::HandleKnownRoute(
    const std::string& method,
    const std::string& path,
    const QueryMap& query,
    const HeaderMap& headers,
    const std::string& body) {
  (void)method;
  (void)query;
  (void)headers;

  if (path == "/health") {
    return JsonResponse({
        {"status", 1},
        {"data",
         {
             {"service", "EchoCompatServer"},
             {"state", "ok"},
             {"compat_port", 6609},
             {"native", true},
         }},
    });
  }

  if (path == "/server/now") {
    return JsonResponse({
        {"status", 1},
        {"data",
         {
             {"now", UnixSeconds()},
             {"time", UnixSeconds()},
             {"timestamp", UnixMilliseconds()},
             {"server_time", UnixSeconds()},
             {"serverTime", UnixSeconds()},
         }},
    });
  }

  if (path == "/register/dev") {
    storage::DeviceRepository deviceRepo(database_);
    DeviceService devices(deviceRepo);
    auto device = devices.EnsureDeviceReady();

    // `force=1` bypasses the cached `registered=true` flag — useful for
    // recovering from stale DB state left by older builds where the flag
    // was set to true incorrectly via MSVC's NSDMI bug.
    const bool force = QueryValue(query, "force") == "1";

    if (force || !device.registered) {
      storage::SessionRepository sessionRepo(database_);
      const auto session = sessionRepo.Load();
      if (session && !session->token.empty() && !session->userId.empty()) {
        DeviceRegisterService registerSvc;
        std::string regError;
        const auto newDfid = registerSvc.Register(device, session->userId, session->token, &regError);
        if (!newDfid.empty()) {
          device.dfid = newDfid;
          device.registered = true;
          deviceRepo.Save(device);
          std::cout << "[CompatApi] /register/dev upgraded dfid=" << newDfid << std::endl;
        } else {
          std::cout << "[CompatApi] /register/dev upgrade failed: " << regError << std::endl;
          // Surface the error to the response so we can debug from a curl probe.
          return JsonResponse({
              {"status", 1},
              {"data", ToJson(device)},
              {"register_error", regError},
          });
        }
      } else {
        std::cout << "[CompatApi] /register/dev skipped (no session)" << std::endl;
      }
    }
    return JsonResponse({
        {"status", 1},
        {"data", ToJson(device)},
    });
  }

  if (path == "/search/hot") {
    SearchService search;
    return JsonResponse(search.Hot(QueryInt(query, "count", 20)));
  }

  if (path == "/search/default") {
    return JsonResponse({{"status", 1}, {"data", {{"keyword", ""}, {"show_keyword", ""}}}});
  }

  if (path == "/search/suggest") {
    SearchService search;
    return JsonResponse(search.Suggest(
        QueryValue(query, "keywords", QueryValue(query, "keyword")),
        QueryInt(query, "count", QueryInt(query, "MusicTipCount", 10))));
  }

  if (path == "/top/album" ||
      path == "/playlist/recommend" ||
      path == "/rank/top" || path == "/top/ip") {
    return JsonResponse({{"status", 1}, {"data", nlohmann::json::array()}});
  }

  if (path == "/search") {
    const auto keywords = QueryValue(query, "keywords", QueryValue(query, "keyword"));
    const auto type = QueryValue(query, "type", "song");
    const auto page = QueryInt(query, "page", 1);
    const auto pageSize = QueryInt(query, "pagesize", QueryInt(query, "pageSize", 30));
    if (handlers_.search) {
      return JsonResponse(handlers_.search(keywords, type, page, pageSize));
    }
    SearchService search;
    return JsonResponse(search.Search(keywords, type, page, pageSize));
  }

  if (path == "/song/url") {
    const auto hash = QueryValue(query, "hash");
    const auto quality = QueryValue(query, "quality");
    const auto ppageId = QueryValue(query, "ppage_id", QueryValue(query, "ppageId"));

    if (handlers_.songUrl) {
      return JsonResponse(handlers_.songUrl(hash, quality, ppageId));
    }

    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    const std::string userId = session ? session->userId : "";
    const std::string token = session ? session->token : "";

    storage::DeviceRepository deviceRepo(database_);
    DeviceService devices(deviceRepo);
    const auto device = devices.EnsureDeviceReady();

    const auto album_id = QueryValue(query, "album_id");
    const auto album_audio_id = QueryValue(query, "album_audio_id");
    SongUrlService songUrl;
    return JsonResponse(songUrl.Resolve(hash, album_id, album_audio_id, quality, ppageId, userId, token, device));
  }

  if (path == "/privilege/lite") {
    PrivilegeService privilege;
    return JsonResponse(privilege.GetLite(
        QueryValue(query, "hash"),
        QueryValue(query, "album_id")));
  }

  if (path == "/search/lyric") {
    const auto hash = QueryValue(query, "hash");
    if (handlers_.lyricSearch) {
      return JsonResponse(handlers_.lyricSearch(hash));
    }
    LyricService lyric;
    return JsonResponse(lyric.Search(hash));
  }

  if (path == "/lyric") {
    const auto id = QueryValue(query, "id");
    const auto accessKey = QueryValue(
        query,
        "accesskey",
        QueryValue(query, "accessKey", QueryValue(query, "access_key")));
    if (handlers_.lyricDetail) {
      return JsonResponse(handlers_.lyricDetail(id, accessKey));
    }
    LyricService lyric;
    return JsonResponse(lyric.GetDetail(id, accessKey));
  }

  if (path == "/playlist/track/all") {
    const auto id = QueryValue(query, "id", QueryValue(query, "listid"));
    const auto page = QueryInt(query, "page", 1);
    const auto pageSize = QueryInt(query, "pagesize", QueryInt(query, "pageSize", 30));
    if (handlers_.playlistTracks) {
      return JsonResponse(handlers_.playlistTracks(id, page, pageSize));
    }
    storage::DeviceRepository deviceRepo(database_);
    DeviceService devices(deviceRepo);
    const auto device = devices.EnsureDeviceReady();

    PlaylistService playlist;
    return JsonResponse(playlist.GetTracks(device, id, page, pageSize));
  }

  if (path == "/playlist/track/all/new") {
    const auto id = QueryValue(query, "listid", QueryValue(query, "id"));
    const auto page = QueryInt(query, "page", 1);
    const auto pageSize = QueryInt(query, "pagesize", QueryInt(query, "pageSize", 30));
    if (handlers_.playlistTracks) {
      return JsonResponse(handlers_.playlistTracks(id, page, pageSize));
    }
    storage::DeviceRepository deviceRepo(database_);
    DeviceService devices(deviceRepo);
    const auto device = devices.EnsureDeviceReady();

    PlaylistService playlist;
    return JsonResponse(playlist.GetTracks(device, id, page, pageSize));
  }

  if (path == "/playlist/tags") {
    PlaylistService playlist;
    return JsonResponse(playlist.GetTags());
  }

  if (path == "/top/playlist") {
    PlaylistService playlist;
    return JsonResponse(playlist.GetTopPlaylists(
        QueryInt(query, "category_id", QueryInt(query, "categoryid", 0)),
        QueryInt(query, "page", 1),
        QueryInt(query, "pagesize", 30),
        QueryInt(query, "sort", 2)));
  }

  if (path == "/rank/list") {
    RankService ranks;
    return JsonResponse(ranks.List());
  }

  if (path == "/top/song") {
    RankService ranks;
    return JsonResponse(ranks.GetSongs(
        6666,
        QueryInt(query, "page", 1),
        QueryInt(query, "pagesize", 30)));
  }

  if (path == "/rank/audio") {
    RankService ranks;
    return JsonResponse(ranks.GetSongs(
        QueryInt(query, "rankid", 0),
        QueryInt(query, "page", 1),
        QueryInt(query, "pagesize", 30)));
  }

  if (path == "/album/detail") {
    CatalogService catalog;
    return JsonResponse(catalog.GetAlbumDetail(QueryValue(query, "id")));
  }

  if (path == "/album/songs") {
    CatalogService catalog;
    return JsonResponse(catalog.GetAlbumSongs(
        QueryValue(query, "id"),
        QueryInt(query, "page", 1),
        QueryInt(query, "pagesize", 30)));
  }

  if (path == "/artist/detail") {
    CatalogService catalog;
    return JsonResponse(catalog.GetArtistDetail(QueryValue(query, "id")));
  }

  if (path == "/artist/audios") {
    CatalogService catalog;
    return JsonResponse(catalog.GetArtistSongs(
        QueryValue(query, "id"),
        QueryInt(query, "page", 1),
        QueryInt(query, "pagesize", 200),
        QueryValue(query, "sort", "hot")));
  }

  if (path == "/artist/albums") {
    CatalogService catalog;
    return JsonResponse(catalog.GetArtistAlbums(
        QueryValue(query, "id"),
        QueryInt(query, "page", 1),
        QueryInt(query, "pagesize", 30),
        QueryValue(query, "sort", "hot")));
  }

  if (path == "/comment/music" || path == "/comment/playlist" ||
      path == "/comment/album") {
    return JsonResponse(EmptyPagedData());
  }

  if (path == "/user/history") {
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    const std::string userId = session ? session->userId : "";
    const std::string token = session ? session->token : "";
    const std::string bp = QueryValue(query, "bp");
    PlayHistoryService playSvc;
    return JsonResponse(playSvc.GetUserHistory(userId, token, bp));
  }

  if (path == "/playlist/add") {
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    const std::string userId = session ? session->userId : "";
    const std::string token = session ? session->token : "";
    const std::string name = QueryValue(query, "name");
    const int type = QueryInt(query, "type", 0);
    const int source = QueryInt(query, "source", 1);
    const std::string createUserId = QueryValue(query, "list_create_userid");
    const std::string createListId = QueryValue(query, "list_create_listid");
    const std::string createGid = QueryValue(query, "list_create_gid");

    storage::DeviceRepository deviceRepo(database_);
    DeviceService devices(deviceRepo);
    const auto device = devices.EnsureDeviceReady();

    PlaylistService playlist;
    return JsonResponse(playlist.AddPlaylist(
        device, userId, token, name, type, source, createUserId, createListId, createGid));
  }

  if (path == "/playlist/del") {
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    const std::string userId = session ? session->userId : "";
    const std::string token = session ? session->token : "";
    const std::string listIdStr = QueryValue(query, "listid", QueryValue(query, "id"));
    const long long listId = listIdStr.empty() ? 0 : std::stoll(listIdStr);

    storage::DeviceRepository deviceRepo(database_);
    DeviceService devices(deviceRepo);
    const auto device = devices.EnsureDeviceReady();

    PlaylistService playlist;
    return JsonResponse(playlist.DeletePlaylist(device, userId, token, listId));
  }

  if (path == "/playlist/tracks/add") {
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    const std::string userId = session ? session->userId : "";
    const std::string token = session ? session->token : "";
    storage::DeviceRepository deviceRepo(database_);
    DeviceService devices(deviceRepo);
    const auto device = devices.EnsureDeviceReady();

    nlohmann::json jsonBody;
    try {
      if (!body.empty()) {
        jsonBody = nlohmann::json::parse(body);
      }
    } catch (...) {}

    auto ReadString = [](const nlohmann::json& j, const std::string& k, const std::string& def = "") -> std::string {
      if (j.contains(k)) {
        if (j[k].is_string()) return j[k].get<std::string>();
        if (j[k].is_number()) return std::to_string(j[k].get<long long>());
      }
      return def;
    };

    const std::string listIdFromQuery = QueryValue(query, "listid", QueryValue(query, "id"));
    const std::string listId = listIdFromQuery.empty() ? ReadString(jsonBody, "listId", ReadString(jsonBody, "id", ReadString(jsonBody, "listid"))) : listIdFromQuery;

    const std::string dataFromQuery = QueryValue(query, "data");
    const std::string data = dataFromQuery.empty() ? ReadString(jsonBody, "data") : dataFromQuery;

    PlaylistService playlist;
    return JsonResponse(playlist.AddPlaylistTracks(device, userId, token, listId, data));
  }

  if (path == "/playlist/tracks/del") {
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    const std::string userId = session ? session->userId : "";
    const std::string token = session ? session->token : "";
    const std::string listId = QueryValue(query, "listid", QueryValue(query, "id"));
    const std::string fileids = QueryValue(query, "fileids", QueryValue(query, "ids", QueryValue(query, "data")));

    storage::DeviceRepository deviceRepo(database_);
    DeviceService devices(deviceRepo);
    const auto device = devices.EnsureDeviceReady();

    PlaylistService playlist;
    return JsonResponse(playlist.DeletePlaylistTracks(device, userId, token, listId, fileids));
  }

  if (path == "/user/detail") {
    std::cout << "[Debug] /user/detail entry" << std::endl;
    if (handlers_.userDetail) {
      return JsonResponse(handlers_.userDetail("", ""));
    }
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    std::string userId;
    std::string token;
    if (session) {
      userId = session->userId;
      token = session->token;
    }
    std::cout << "[Debug] /user/detail no handler" << std::endl;
    if (session && !userId.empty()) {
      storage::DeviceRepository deviceRepo(database_);
      DeviceService devices(deviceRepo);
      const auto device = devices.EnsureDeviceReady();
      UserService userSvc;
      nlohmann::json detail = userSvc.GetUserDetail(device, userId, token);
      if (detail.value("status", 0) == 1 && detail.contains("data") && detail["data"].is_object()) {
        auto data = detail["data"];
        std::string nickname = data.value("nickname", "");
        std::string pic = data.value("pic", "");
        if (pic.empty()) {
          pic = data.value("avatar", "");
        }
        if ((!nickname.empty() && nickname != session->nickname) || (!pic.empty() && pic != session->pic)) {
          SessionInfo updatedSession = *session;
          if (!nickname.empty()) updatedSession.nickname = nickname;
          if (!pic.empty()) updatedSession.pic = pic;
          sessionRepo.Save(updatedSession);
        }
        if (detail["data"].value("pic", "").empty() && !pic.empty()) {
          detail["data"]["pic"] = pic;
        }
        return JsonResponse(detail);
      }

      // Fallback
      return JsonResponse({
          {"status", 1},
          {"data", {
              {"userid", userId},
              {"nickname", session->nickname.empty() ? "听歌用户" : session->nickname},
              {"pic", session->pic},
              {"token", token},
          }},
      });
    }
    return JsonResponse({{"status", 0}, {"error", "not logged in"}, {"data", nullptr}});
  }

  if (path == "/user/vip/detail") {
    // DEBUG: force return test marker
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    std::string userId;
    std::string token;
    if (session) {
      userId = session->userId;
      token = session->token;
    }
    if (handlers_.userVip) {
      return JsonResponse(handlers_.userVip(userId, token));
    }

    if (session && !userId.empty()) {
      storage::DeviceRepository deviceRepo(database_);
      DeviceService devices(deviceRepo);
      const auto device = devices.EnsureDeviceReady();
      UserService userSvc;
      nlohmann::json vip = userSvc.GetUserVip(device, userId, token);

      // Pull profile fields out of the union_vip response if KuGou included
      // them — saves us a separate RSA-encrypted /v3/get_my_info call.
      if (vip.value("status", 0) == 1 && vip.contains("data") && vip["data"].is_object()) {
        const auto& data = vip["data"];
        const auto extractStr = [&](std::initializer_list<const char*> keys) {
          for (const char* k : keys) {
            if (data.contains(k) && data[k].is_string() && !data[k].get<std::string>().empty()) {
              return data[k].get<std::string>();
            }
          }
          return std::string{};
        };
        const auto nickname = extractStr({"nickname", "username", "name"});
        const auto pic = extractStr({"pic", "headphoto", "avatar", "headerurl", "userpic"});
        if ((!nickname.empty() && nickname != session->nickname) ||
            (!pic.empty() && pic != session->pic)) {
          SessionInfo updated = *session;
          if (!nickname.empty()) updated.nickname = nickname;
          if (!pic.empty()) updated.pic = pic;
          sessionRepo.Save(updated);
        }
        return JsonResponse(vip);
      }

      // Fallback: session-derived defaults so the UI still renders.
      return JsonResponse({
          {"status", 1},
          {"data", {
              {"vip_level", 0},
              {"vip_type", 0},
              {"is_vip", 0},
              {"end_time", ""},
              {"nickname", session->nickname},
              {"pic", session->pic},
          }},
      });
    }
    return JsonResponse({{"status", 0}, {"error", "not logged in"}, {"data", nullptr}});
  }

  if (path == "/youth/day/vip" || path == "/youth/day/vip/upgrade") {
    return JsonResponse({
        {"status", 0},
        {"error_code", "kugou_vip_legacy_disabled"},
        {"error", "该端点需要广告 SDK 凭证，纯 HTTP 不可达；请使用 /youth/listen/song 或 /youth/vip/ad"},
        {"data", nullptr},
    });
  }

  if (path == "/youth/listen/song") {
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    if (!session || session->userId.empty() || session->token.empty()) {
      return JsonResponse({{"status", 0}, {"error", "not logged in"}, {"data", nullptr}});
    }
    UserService userSvc;
    return JsonResponse(userSvc.ClaimYouthListenSong(session->userId, session->token));
  }

  if (path == "/youth/vip/ad") {
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    if (!session || session->userId.empty() || session->token.empty()) {
      return JsonResponse({{"status", 0}, {"error", "not logged in"}, {"data", nullptr}});
    }
    UserService userSvc;
    return JsonResponse(userSvc.ClaimYouthAdVip(session->userId, session->token));
  }

  if (path == "/everyday/recommend") {
    if (handlers_.everydayRecommend) {
      return JsonResponse(handlers_.everydayRecommend("", ""));
    }
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    std::string userId;
    std::string token;
    if (session) {
      userId = session->userId;
      token = session->token;
    }
    HomeService homeSvc;
    return JsonResponse(homeSvc.GetEverydayRecommend(userId, token));
  }

  if (path == "/login/qr/key") {
    storage::DeviceRepository deviceRepo(database_);
    DeviceService devices(deviceRepo);
    const auto device = devices.EnsureDeviceReady();
    if (handlers_.loginQrKey) {
      return JsonResponse(handlers_.loginQrKey(device));
    }
    LoginService login;
    return JsonResponse(login.BeginQrLogin(device));
  }

  if (path == "/login/qr/create") {
    const auto key = QueryValue(query, "key");
    if (key.empty()) {
      return JsonResponse({{"status", 0}, {"error", "missing key parameter"}, {"data", nullptr}});
    }
    const auto qrcodeUrl = "https://h5.kugou.com/apps/loginQRCode/html/index.html?qrcode=" + key;
    return JsonResponse({
        {"status", 1},
        {"data", {{"qrcode", key}, {"qrcodeurl", qrcodeUrl}}},
    });
  }

  if (path == "/login/qr/check") {
    const auto key = QueryValue(query, "key");
    if (key.empty()) {
      return JsonResponse({{"status", 0}, {"error", "missing key parameter"}, {"data", nullptr}});
    }
    storage::DeviceRepository deviceRepo(database_);
    DeviceService devices(deviceRepo);
    const auto device = devices.EnsureDeviceReady();
    nlohmann::json result;
    if (handlers_.loginQrCheck) {
      result = handlers_.loginQrCheck(device, key);
    } else {
      LoginService login;
      result = login.PollQrLogin(device, key);
    }

    // Persist session on successful login.
    // KuGou may return status=4 either nested in data.status or at the top level.
    auto ExtractUserId = [](const nlohmann::json& j, const std::string& key) -> std::string {
      if (!j.contains(key)) return "";
      const auto& v = j[key];
      if (v.is_string()) return v.get<std::string>();
      if (v.is_number_integer()) return std::to_string(v.get<std::int64_t>());
      if (v.is_number_unsigned()) return std::to_string(v.get<std::uint64_t>());
      return "";
    };

    const nlohmann::json* loginData = nullptr;
    if (result.contains("data") && result["data"].is_object() &&
        result["data"].value("status", 0) == 4) {
      loginData = &result["data"];
    } else if (result.value("status", 0) == 4) {
      loginData = &result;
    }

    if (loginData) {
      // KuGou's QR check response has used different field names for nickname
      // and avatar across versions (nickname/username, pic/headphoto/avatar).
      // Try all known variants so the sidebar shows real data immediately
      // instead of waiting for /user/vip/detail to backfill it.
      auto FirstNonEmptyString = [&](std::initializer_list<const char*> keys) {
        for (const char* k : keys) {
          if (loginData->contains(k) && (*loginData)[k].is_string()) {
            auto v = (*loginData)[k].get<std::string>();
            if (!v.empty()) return v;
          }
        }
        return std::string{};
      };
      SessionInfo session;
      session.token     = loginData->value("token", "");
      session.userId    = ExtractUserId(*loginData, "userid");
      session.nickname  = FirstNonEmptyString({"nickname", "username", "name"});
      session.pic       = FirstNonEmptyString({"pic", "headphoto", "avatar", "headerurl", "userpic"});
      if (!session.token.empty() && !session.userId.empty()) {
        storage::SessionRepository sessionRepo(database_);
        sessionRepo.Save(session);

        // Upgrade the random local dfid to a KuGou-issued one. This is the
        // critical step that flips us from "untrusted browser" to "trusted
        // app" — without it /song/url only serves 60s previews even for
        // VIP-eligible users, and /user/playlist returns error_code 20017.
        if (!device.registered) {
          DeviceRegisterService registerSvc;
          std::string regError;
          const auto newDfid = registerSvc.Register(device, session.userId, session.token, &regError);
          if (!newDfid.empty()) {
            DeviceInfo updated = device;
            updated.dfid = newDfid;
            updated.registered = true;
            storage::DeviceRepository devRepo(database_);
            devRepo.Save(updated);
            std::cout << "[CompatApi] Device registered with KuGou, new dfid=" << newDfid << std::endl;
          } else {
            std::cout << "[CompatApi] Device registration failed: " << regError << std::endl;
          }
        }
      }
    }
    return JsonResponse(result);
  }

  if (path == "/auth/logout") {
    // Clear both session and device. The next QR scan will register a fresh
    // device with the current appid (1005), so any old "lite-scope" token
    // bound to a stale appid=1014 device is fully discarded.
    storage::SessionRepository sessionRepo(database_);
    sessionRepo.Clear();
    storage::DeviceRepository deviceRepo(database_);
    deviceRepo.Clear();
    return JsonResponse({{"status", 1}, {"data", {{"cleared", true}}}});
  }

  if (path == "/settings/device") {
    // GET /settings/device                          → return current device
    // GET /settings/device?dfid=X&mid=Y&uuid=Z      → override fields, mark
    //   registered=true so /song/url / /user/playlist treat it as trusted.
    // Empty values are ignored (keep existing). Pass `clear=1` to reset.
    storage::DeviceRepository deviceRepo(database_);
    DeviceService devices(deviceRepo);
    auto device = devices.EnsureDeviceReady();

    const auto newDfid = QueryValue(query, "dfid");
    const auto newMid = QueryValue(query, "mid");
    const auto newUuid = QueryValue(query, "uuid");
    const bool clearOverride = QueryValue(query, "clear") == "1";

    bool changed = false;
    if (clearOverride) {
      // Reset to a fresh random device — useful for testing.
      deviceRepo.Clear();
      device = devices.EnsureDeviceReady();
      changed = true;
    }
    if (!newDfid.empty() && newDfid != device.dfid) {
      device.dfid = newDfid;
      changed = true;
    }
    if (!newMid.empty() && newMid != device.mid) {
      device.mid = newMid;
      changed = true;
    }
    if (!newUuid.empty() && newUuid != device.uuid) {
      device.uuid = newUuid;
      changed = true;
    }
    if (changed) {
      // Any manual override implies the user trusts the value as KuGou-issued.
      // Set registered=true so downstream skips the (broken) /register_dev call.
      if (!newDfid.empty() || !newMid.empty() || !newUuid.empty()) {
        device.registered = true;
      }
      deviceRepo.Save(device);
      std::cout << "[CompatApi] /settings/device updated dfid=" << device.dfid
                << " mid=" << device.mid << " uuid=" << device.uuid << std::endl;
    }
    return JsonResponse({
        {"status", 1},
        {"data", ToJson(device)},
        {"updated", changed},
    });
  }

  if (path == "/song/climax") {
    SongService songSvc;
    return JsonResponse(songSvc.GetClimax(QueryValue(query, "hash")));
  }

  if (path == "/song/ranking") {
    SongService songSvc;
    return JsonResponse(songSvc.GetRanking(QueryValue(query, "album_audio_id")));
  }

  if (path == "/song/ranking/filter") {
    SongService songSvc;
    return JsonResponse(songSvc.GetRankingFilter(
        QueryValue(query, "album_audio_id"),
        QueryInt(query, "page", 1),
        QueryInt(query, "pagesize", 30)));
  }

  if (path == "/images/audio") {
    HomeService homeSvc;
    return JsonResponse(homeSvc.GetImagesAudio(
        QueryValue(query, "hash"),
        QueryValue(query, "audio_id"),
        QueryValue(query, "album_audio_id"),
        QueryValue(query, "filename"),
        QueryInt(query, "count", 5)));
  }

  if (path == "/playhistory/upload") {
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    const std::string userId = session ? session->userId : "";
    const std::string token = session ? session->token : "";
    
    std::string mxidStr = QueryValue(query, "mxid");
    std::string timeStr = QueryValue(query, "time");
    int pc = QueryInt(query, "pc", 1);

    PlayHistoryService playSvc;
    long long mxidVal = mxidStr.empty() ? 0 : std::stoll(mxidStr);
    long long timeVal = timeStr.empty() ? 0 : std::stoll(timeStr);
    return JsonResponse(playSvc.UploadSong(userId, token, mxidVal, timeVal, pc));
  }

  if (path == "/user/cloud") {
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    const std::string userId = session ? session->userId : "";
    const std::string token = session ? session->token : "";
    const int page = QueryInt(query, "page", 1);
    const int pageSize = QueryInt(query, "pagesize", 30);
    UserCloudService cloudSvc;
    return JsonResponse(cloudSvc.GetList(userId, token, page, pageSize));
  }

  if (path == "/playlist/detail") {
    const auto id = QueryValue(query, "id", QueryValue(query, "ids"));
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    const std::string userId = session ? session->userId : "0";
    const std::string token = session ? session->token : "";
    if (handlers_.playlistDetail) {
      return JsonResponse(handlers_.playlistDetail(id, userId, token));
    }
    storage::DeviceRepository deviceRepo(database_);
    DeviceService devices(deviceRepo);
    const auto device = devices.EnsureDeviceReady();

    PlaylistService playlist;
    return JsonResponse(playlist.GetPlaylistDetail(device, id, userId, token));
  }

  if (path == "/user/playlist") {
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    const std::string userId = session ? session->userId : "";
    const std::string token = session ? session->token : "";
    const int page = QueryInt(query, "page", 1);
    const int pageSize = QueryInt(query, "pagesize", 30);
    if (handlers_.userPlaylist) {
      return JsonResponse(handlers_.userPlaylist(userId, token, page, pageSize));
    }
    storage::DeviceRepository deviceRepo(database_);
    DeviceService devices(deviceRepo);
    auto device = devices.EnsureDeviceReady();

    PlaylistService playlist;
    if (!device.registered && session && !userId.empty() && !token.empty()) {
      DeviceRegisterService registerSvc;
      std::string regError;
      const auto newDfid = registerSvc.Register(device, userId, token, &regError);
      if (!newDfid.empty()) {
        device.dfid = newDfid;
        device.registered = true;
        deviceRepo.Save(device);
        std::cout << "[CompatApi] /user/playlist registered device dfid=" << newDfid << std::endl;
      } else {
        std::cout << "[CompatApi] /user/playlist device registration failed: " << regError << std::endl;
      }
    }
    auto result = playlist.GetUserPlaylists(device, userId, token, page, pageSize);
    if (IsKuGouErrorCode(result, 20017) && session && !userId.empty() && !token.empty()) {
      DeviceRegisterService registerSvc;
      std::string regError;
      DeviceInfo retryDevice = device;
      retryDevice.registered = false;
      const auto newDfid = registerSvc.Register(retryDevice, userId, token, &regError);
      if (!newDfid.empty()) {
        retryDevice.dfid = newDfid;
        retryDevice.registered = true;
        deviceRepo.Save(retryDevice);
        std::cout << "[CompatApi] /user/playlist refreshed device dfid=" << newDfid << std::endl;
        result = playlist.GetUserPlaylists(retryDevice, userId, token, page, pageSize);
      } else {
        std::cout << "[CompatApi] /user/playlist device refresh failed: " << regError << std::endl;
      }
    }

    // The user_playlist response embeds real `list_create_username` +
    // `create_user_pic` on every entry — backfill them into the session so
    // the sidebar avatar / nickname stops falling back to "听歌用户".
    if (session && result.value("status", 0) == 1 && result.contains("data") &&
        result["data"].is_object() && result["data"].contains("info") &&
        result["data"]["info"].is_array() && !result["data"]["info"].empty()) {
      const auto& first = result["data"]["info"][0];
      std::string nick;
      std::string pic;
      if (first.is_object()) {
        if (first.contains("list_create_username") && first["list_create_username"].is_string())
          nick = first["list_create_username"].get<std::string>();
        if (first.contains("create_user_pic") && first["create_user_pic"].is_string())
          pic = first["create_user_pic"].get<std::string>();
      }
      if ((!nick.empty() && nick != session->nickname) ||
          (!pic.empty() && pic != session->pic)) {
        SessionInfo updated = *session;
        if (!nick.empty()) updated.nickname = nick;
        if (!pic.empty()) updated.pic = pic;
        sessionRepo.Save(updated);
      }
    }
    return JsonResponse(result);
  }

  return JsonResponse(NativeNotImplementedPayload(path), 501);
}

bool IsKnownCompatRoute(const std::string& path) {
  for (const auto route : kKnownRoutes) {
    if (path == route) return true;
  }
  return path == "/kmr/audio/mv" || path == "/video/privilege" || path == "/video/detail";
}

nlohmann::json NativeNotImplementedPayload(const std::string& path) {
  return {
      {"status", 0},
      {"error_code", "native_not_implemented"},
      {"error", "Native C++ compatibility route has not been ported yet"},
      {"path", path},
      {"data", nullptr},
  };
}

}  // namespace echo::core
