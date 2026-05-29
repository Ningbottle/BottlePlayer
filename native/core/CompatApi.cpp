#include "echo/core/CompatApi.h"
#include "echo/core/CompatApiUtils.h"
#include "echo/core/CompatRoutes.h"

#include <array>
#include <sstream>

#include "echo/diagnostics/EchoDiagnostics.h"
#include "echo/diagnostics/ScopedTimer.h"

#include "echo/core/CompatRequestContext.h"
#include "echo/core/DeviceRegisterService.h"
#include "echo/core/SongUrlService.h"

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

}  // namespace

CompatApi::CompatApi(storage::Database& database)
    : database_(database) {}

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

  auto sw = diagnostics::Stopwatch::Start();
  auto response = HandleKnownRoute(method, path, query, headers, body);
  {
    std::ostringstream log;
    log << "route=" << path
        << " status=" << (response.body.is_object() ? response.body.value("status", 0) : 0)
        << " http=" << response.httpStatus
        << " elapsed_ms=" << sw.ElapsedMs();
    ECHO_LOG("CompatApi", log.str());
  }
  return response;
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

  // Diagnostics
  if (path == "/health") return HandleHealth();
  if (path == "/server/now") return HandleServerNow();

  // Register
  if (path == "/register/dev") return HandleRegisterDev(database_, query);

  // Search & Discovery
  if (path == "/search/hot") return HandleSearchHot(query);
  if (path == "/search/default") return HandleSearchDefault();
  if (path == "/search/suggest") return HandleSearchSuggest(query);
  if (path == "/search") return HandleSearch(query, handlers_.search);
  if (path == "/top/album" || path == "/playlist/recommend" ||
      path == "/rank/top" || path == "/top/ip") {
    return HandleTopAlbumPlaylistRecommendRankTopTopIp(path);
  }
  if (path == "/rank/list") return HandleRankList();
  if (path == "/top/song") return HandleTopSong(query);
  if (path == "/rank/audio") return HandleRankAudio(query);
  if (path == "/everyday/recommend") return HandleEverydayRecommend(database_, handlers_.everydayRecommend);

  // Song & Lyric
  if (path == "/song/url") {
    const auto hash = QueryValue(query, "hash");
    const auto quality = QueryValue(query, "quality");
    const auto ppageId = QueryValue(query, "ppage_id", QueryValue(query, "ppageId"));

    if (handlers_.songUrl) {
      return JsonResponse(handlers_.songUrl(hash, quality, ppageId));
    }

    CompatRequestContext ctx(database_);
    const auto& device = ctx.Device();
    const std::string userId = ctx.UserIdOr("");
    const std::string token = ctx.TokenOrEmpty();

    const auto album_id = QueryValue(query, "album_id");
    const auto album_audio_id = QueryValue(query, "album_audio_id");
    SongUrlService songUrl;
    auto result = songUrl.Resolve(hash, album_id, album_audio_id, quality, ppageId, userId, token, device);
    return JsonResponse(result);
  }

  if (path == "/privilege/lite") return HandlePrivilegeLite(query);
  if (path == "/search/lyric") return HandleSearchLyric(query, handlers_.lyricSearch);
  if (path == "/lyric") return HandleLyric(query, handlers_.lyricDetail);
  if (path == "/song/climax") return HandleSongClimax(query);
  if (path == "/song/ranking") return HandleSongRanking(query);
  if (path == "/song/ranking/filter") return HandleSongRankingFilter(query);
  if (path == "/images/audio") return HandleImagesAudio(query);

  // Playlist
  if (path == "/playlist/add") return HandlePlaylistAdd(database_, query);
  if (path == "/playlist/del") return HandlePlaylistDel(database_, query);
  if (path == "/playlist/tracks/add") return HandlePlaylistTracksAdd(database_, query, body);
  if (path == "/playlist/tracks/del") return HandlePlaylistTracksDel(database_, query);
  if (path == "/playlist/detail") return HandlePlaylistDetail(database_, query, handlers_.playlistDetail);
  if (path == "/playlist/track/all") return HandlePlaylistTrackAll(database_, query, handlers_.playlistTracks);
  if (path == "/playlist/track/all/new") return HandlePlaylistTrackAllNew(database_, query, handlers_.playlistTracks);
  if (path == "/playlist/tags") return HandlePlaylistTags(database_);
  if (path == "/top/playlist") return HandleTopPlaylist(database_, query);

  // User
  if (path == "/user/detail") {
    return HandleUserDetail(database_, handlers_.userDetail);
  }
  if (path == "/user/vip/detail") return HandleUserVipDetail(database_, handlers_.userVip);
  if (path == "/user/playlist") return HandleUserPlaylist(database_, query, handlers_.userPlaylist);
  if (path == "/user/history") return HandleUserHistory(database_, query);
  if (path == "/user/cloud") return HandleUserCloud(database_, query);
  if (path == "/playhistory/upload") return HandlePlayHistoryUpload(database_, query);

  // Youth / VIP
  if (path == "/youth/day/vip" || path == "/youth/day/vip/upgrade") return HandleYouthDayVip();
  if (path == "/youth/listen/song") return HandleYouthListenSong(database_);
  if (path == "/youth/vip/ad") return HandleYouthVipAd(database_);

  // Login
  if (path == "/login/qr/key") return HandleLoginQrKey(database_, handlers_.loginQrKey);
  if (path == "/login/qr/create") return HandleLoginQrCreate(query);
  if (path == "/login/qr/check") return HandleLoginQrCheck(database_, query, handlers_.loginQrCheck);
  if (path == "/auth/logout") return HandleAuthLogout(database_);
  if (path == "/settings/device") return HandleSettingsDevice(database_, query);

  // Catalog
  if (path == "/album/detail") return HandleAlbumDetail(query);
  if (path == "/album/songs") return HandleAlbumSongs(query);
  if (path == "/artist/detail") return HandleArtistDetail(query);
  if (path == "/artist/audios") return HandleArtistAudios(query);
  if (path == "/artist/albums") return HandleArtistAlbums(query);
  if (path == "/comment/music" || path == "/comment/playlist" ||
      path == "/comment/album") {
    return HandleCommentMusicPlaylistAlbum(path);
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
