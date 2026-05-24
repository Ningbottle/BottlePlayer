#include "echo/core/CompatApi.h"

#include <array>
#include <chrono>
#include <string_view>

#include "echo/core/CatalogService.h"
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

constexpr std::array<std::string_view, 66> kKnownRoutes = {
    "/health",
    "/server/now",
    "/register/dev",
    "/login/qr/key",
    "/login/qr/create",
    "/login/qr/check",
    "/captcha/sent",
    "/login/cellphone",
    "/login/wx/create",
    "/login/wx/check",
    "/login/openplat",
    "/user/detail",
    "/user/vip/detail",
    "/youth/day/vip",
    "/youth/day/vip/upgrade",
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
  return CompatResponse{httpStatus, "application/json; charset=utf-8", std::move(body)};
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

}  // namespace

CompatApi::CompatApi(storage::Database& database) : database_(database) {}

CompatApi::CompatApi(storage::Database& database, CompatApiHandlers handlers)
    : database_(database), handlers_(std::move(handlers)) {}

CompatResponse CompatApi::Handle(
    const std::string& method,
    const std::string& path,
    const QueryMap& query,
    const HeaderMap& headers) {
  if (!IsKnownCompatRoute(path)) {
    return JsonResponse({{"status", 0}, {"error_code", 404}, {"error", "Unknown route"}}, 404);
  }

  return HandleKnownRoute(method, path, query, headers);
}

CompatResponse CompatApi::HandleKnownRoute(
    const std::string& method,
    const std::string& path,
    const QueryMap& query,
    const HeaderMap& headers) {
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
    storage::DeviceRepository repository(database_);
    DeviceService devices(repository);
    return JsonResponse({
        {"status", 1},
        {"data", ToJson(devices.EnsureDeviceReady())},
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
    SongUrlService songUrl;
    return JsonResponse(songUrl.Resolve(hash, quality, ppageId));
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
    PlaylistService playlist;
    return JsonResponse(playlist.GetTracks(id, page, pageSize));
  }

  if (path == "/playlist/track/all/new") {
    const auto id = QueryValue(query, "listid", QueryValue(query, "id"));
    const auto page = QueryInt(query, "page", 1);
    const auto pageSize = QueryInt(query, "pagesize", QueryInt(query, "pageSize", 30));
    if (handlers_.playlistTracks) {
      return JsonResponse(handlers_.playlistTracks(id, page, pageSize));
    }
    PlaylistService playlist;
    return JsonResponse(playlist.GetTracks(id, page, pageSize));
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

    PlaylistService playlist;
    return JsonResponse(playlist.AddPlaylist(
        userId, token, name, type, source, createUserId, createListId, createGid));
  }

  if (path == "/playlist/del") {
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    const std::string userId = session ? session->userId : "";
    const std::string token = session ? session->token : "";
    const std::string listIdStr = QueryValue(query, "listid", QueryValue(query, "id"));
    const long long listId = listIdStr.empty() ? 0 : std::stoll(listIdStr);

    PlaylistService playlist;
    return JsonResponse(playlist.DeletePlaylist(userId, token, listId));
  }

  if (path == "/playlist/tracks/add") {
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    const std::string userId = session ? session->userId : "";
    const std::string token = session ? session->token : "";
    const std::string listId = QueryValue(query, "listid", QueryValue(query, "id"));
    const std::string data = QueryValue(query, "data");

    PlaylistService playlist;
    return JsonResponse(playlist.AddPlaylistTracks(userId, token, listId, data));
  }

  if (path == "/playlist/tracks/del") {
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    const std::string userId = session ? session->userId : "";
    const std::string token = session ? session->token : "";
    const std::string listId = QueryValue(query, "listid", QueryValue(query, "id"));
    const std::string fileids = QueryValue(query, "fileids", QueryValue(query, "ids", QueryValue(query, "data")));

    PlaylistService playlist;
    return JsonResponse(playlist.DeletePlaylistTracks(userId, token, listId, fileids));
  }

  if (path == "/user/detail") {
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    const std::string userId = session ? session->userId : "";
    const std::string token = session ? session->token : "";
    if (handlers_.userDetail) {
      return JsonResponse(handlers_.userDetail(userId, token));
    }
    UserService userSvc;
    return JsonResponse(userSvc.GetUserDetail(userId, token));
  }

  if (path == "/user/vip/detail") {
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    const std::string userId = session ? session->userId : "";
    const std::string token = session ? session->token : "";
    if (handlers_.userVip) {
      return JsonResponse(handlers_.userVip(userId, token));
    }
    UserService userSvc;
    return JsonResponse(userSvc.GetUserVip(userId, token));
  }

  if (path == "/youth/day/vip") {
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    const std::string userId = session ? session->userId : "";
    const std::string token = session ? session->token : "";
    UserService userSvc;
    return JsonResponse(userSvc.ClaimVip(userId, token));
  }

  if (path == "/everyday/recommend") {
    storage::SessionRepository sessionRepo(database_);
    const auto session = sessionRepo.Load();
    const std::string userId = session ? session->userId : "";
    const std::string token = session ? session->token : "";
    if (handlers_.everydayRecommend) {
      return JsonResponse(handlers_.everydayRecommend(userId, token));
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
    if (result.contains("data") && result["data"].is_object()) {
      const auto& data = result["data"];
      if (data.value("status", 0) == 4) {
        SessionInfo session;
        session.token = data.value("token", "");
        session.userId = data.contains("userid")
            ? (data["userid"].is_string()
                   ? data["userid"].get<std::string>()
                   : std::to_string(data["userid"].get<std::int64_t>()))
            : "";
        if (!session.token.empty() && !session.userId.empty()) {
          storage::SessionRepository sessionRepo(database_);
          sessionRepo.Save(session);
        }
      }
    }
    return JsonResponse(result);
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
    PlaylistService playlist;
    return JsonResponse(playlist.GetPlaylistDetail(id, userId, token));
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
    PlaylistService playlist;
    return JsonResponse(playlist.GetUserPlaylists(userId, token, page, pageSize));
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
