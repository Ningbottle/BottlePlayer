#include "echo/core/CompatApi.h"
#include "echo/core/CompatApiUtils.h"
#include "echo/core/CompatRoutes.h"

#include <functional>
#include <sstream>
#include <unordered_map>

#include "echo/diagnostics/EchoDiagnostics.h"
#include "echo/diagnostics/ScopedTimer.h"

#include "echo/core/CompatRequestContext.h"
#include "echo/core/DeviceRegisterService.h"
#include "echo/core/SongUrlService.h"

namespace echo::core {
namespace {

using namespace std::chrono;

// ── Unified dispatch types ─────────────────────────────────────────────

struct RouteContext {
  storage::Database& database;
  CompatApiHandlers& handlers;
  const QueryMap& query;
  const HeaderMap& headers;
  const std::string& body;
};

using RouteHandlerFn = std::function<CompatResponse(const RouteContext&, const std::string& path)>;

// ── /song/url inline logic (extracted for dispatch table) ──────────────

CompatResponse DispatchSongUrl(const RouteContext& ctx, const std::string&) {
  const auto hash = QueryValue(ctx.query, "hash");
  const auto quality = QueryValue(ctx.query, "quality");
  const auto ppageId = QueryValue(ctx.query, "ppage_id", QueryValue(ctx.query, "ppageId"));

  if (ctx.handlers.songUrl) {
    return JsonResponse(ctx.handlers.songUrl(hash, quality, ppageId));
  }

  CompatRequestContext reqCtx(ctx.database);
  const auto& device = reqCtx.Device();
  const std::string userId = reqCtx.UserIdOr("");
  const std::string token = reqCtx.TokenOrEmpty();

  const auto album_id = QueryValue(ctx.query, "album_id");
  const auto album_audio_id = QueryValue(ctx.query, "album_audio_id");
  const auto& session = reqCtx.Session();
  const std::string vipToken = (session && !session->vipToken.empty()) ? session->vipToken : "";
  SongUrlService songUrl;
  auto result = songUrl.Resolve(hash, album_id, album_audio_id, quality, ppageId, userId, token, device, vipToken);
  return JsonResponse(result);
}

// ── Unified route table ────────────────────────────────────────────────
// Single source of truth for both route recognition (IsKnownCompatRoute)
// and dispatch (HandleKnownRoute).  Each route appears exactly once.
// Paths NOT in this table are unknown (404).  Paths with no handler
// (commented as "not yet ported") fall through to 501.

const std::unordered_map<std::string, RouteHandlerFn>& GetRouteTable() {
  static const std::unordered_map<std::string, RouteHandlerFn> table = {
      // Diagnostics
      {"/health",             [](const RouteContext&, const std::string&) { return HandleHealth(); }},
      {"/healthz",            [](const RouteContext&, const std::string&) { return HandleHealth(); }},  // alias for /health
      {"/server/now",        [](const RouteContext&, const std::string&) { return HandleServerNow(); }},
      {"/diagnostics/memory",[](const RouteContext&, const std::string&) { return HandleDiagnosticsMemory(); }},

      // Register
      {"/register/dev", [](const RouteContext& ctx, const std::string&) { return HandleRegisterDev(ctx.database, ctx.query); }},

      // Login
      {"/login/qr/key",   [](const RouteContext& ctx, const std::string&) { return HandleLoginQrKey(ctx.database, ctx.handlers.loginQrKey); }},
      {"/login/qr/create",[](const RouteContext& ctx, const std::string&) { return HandleLoginQrCreate(ctx.query); }},
      {"/login/qr/check", [](const RouteContext& ctx, const std::string&) { return HandleLoginQrCheck(ctx.database, ctx.query, ctx.handlers.loginQrCheck); }},
      {"/auth/logout",    [](const RouteContext& ctx, const std::string&) { return HandleAuthLogout(ctx.database); }},
      {"/settings/device",[](const RouteContext& ctx, const std::string&) { return HandleSettingsDevice(ctx.database, ctx.query); }},
      {"/captcha/sent",   nullptr},  // not yet ported
      {"/login/cellphone",nullptr},  // not yet ported
      {"/login/wx/create",nullptr},  // not yet ported
      {"/login/wx/check", nullptr},  // not yet ported
      {"/login/openplat", nullptr},  // not yet ported

      // Search & Discovery
      {"/search/hot",    [](const RouteContext& ctx, const std::string&) { return HandleSearchHot(ctx.query); }},
      {"/search/default",[](const RouteContext&, const std::string&) { return HandleSearchDefault(); }},
      {"/search/suggest",[](const RouteContext& ctx, const std::string&) { return HandleSearchSuggest(ctx.query); }},
      {"/search",        [](const RouteContext& ctx, const std::string&) { return HandleSearch(ctx.query, ctx.handlers.search); }},
      {"/rank/list",     [](const RouteContext&, const std::string&) { return HandleRankList(); }},
      {"/top/song",      [](const RouteContext& ctx, const std::string&) { return HandleTopSong(ctx.query); }},
      {"/rank/audio",    [](const RouteContext& ctx, const std::string&) { return HandleRankAudio(ctx.query); }},
      {"/everyday/recommend", [](const RouteContext& ctx, const std::string&) { return HandleEverydayRecommend(ctx.database, ctx.handlers.everydayRecommend); }},
      {"/personal/fm",   [](const RouteContext& ctx, const std::string&) { return HandlePersonalFm(ctx.database, ctx.query); }},
      // Grouped: /top/album, /playlist/recommend, /rank/top, /top/ip share a handler
      {"/top/album",         [](const RouteContext&, const std::string& path) { return HandleTopAlbumPlaylistRecommendRankTopTopIp(path); }},
      {"/playlist/recommend",[](const RouteContext&, const std::string& path) { return HandleTopAlbumPlaylistRecommendRankTopTopIp(path); }},
      {"/rank/top",          [](const RouteContext&, const std::string& path) { return HandleTopAlbumPlaylistRecommendRankTopTopIp(path); }},
      {"/top/ip",            [](const RouteContext&, const std::string& path) { return HandleTopAlbumPlaylistRecommendRankTopTopIp(path); }},

      // Song & Lyric
      {"/song/url",     DispatchSongUrl},
      {"/privilege/lite",[](const RouteContext& ctx, const std::string&) { return HandlePrivilegeLite(ctx.query); }},
      {"/search/lyric", [](const RouteContext& ctx, const std::string&) { return HandleSearchLyric(ctx.query, ctx.handlers.lyricSearch); }},
      {"/lyric",        [](const RouteContext& ctx, const std::string&) { return HandleLyric(ctx.query, ctx.handlers.lyricDetail); }},
      {"/song/climax",  [](const RouteContext& ctx, const std::string&) { return HandleSongClimax(ctx.query); }},
      {"/song/ranking", [](const RouteContext& ctx, const std::string&) { return HandleSongRanking(ctx.query); }},
      {"/song/ranking/filter",[](const RouteContext& ctx, const std::string&) { return HandleSongRankingFilter(ctx.query); }},
      {"/images/audio", [](const RouteContext& ctx, const std::string&) { return HandleImagesAudio(ctx.query); }},

      // Playlist
      {"/playlist/add",        [](const RouteContext& ctx, const std::string&) { return HandlePlaylistAdd(ctx.database, ctx.query); }},
      {"/playlist/del",        [](const RouteContext& ctx, const std::string&) { return HandlePlaylistDel(ctx.database, ctx.query); }},
      {"/playlist/tracks/add", [](const RouteContext& ctx, const std::string&) { return HandlePlaylistTracksAdd(ctx.database, ctx.query, ctx.body); }},
      {"/playlist/tracks/del", [](const RouteContext& ctx, const std::string&) { return HandlePlaylistTracksDel(ctx.database, ctx.query); }},
      {"/playlist/detail",     [](const RouteContext& ctx, const std::string&) { return HandlePlaylistDetail(ctx.database, ctx.query, ctx.handlers.playlistDetail); }},
      {"/playlist/track/all",  [](const RouteContext& ctx, const std::string&) { return HandlePlaylistTrackAll(ctx.database, ctx.query, ctx.handlers.playlistTracks); }},
      {"/playlist/track/all/new",[](const RouteContext& ctx, const std::string&) { return HandlePlaylistTrackAllNew(ctx.database, ctx.query, ctx.handlers.playlistTracks); }},
      {"/playlist/tags",  [](const RouteContext& ctx, const std::string&) { return HandlePlaylistTags(ctx.database); }},
      {"/top/playlist",  [](const RouteContext& ctx, const std::string&) { return HandleTopPlaylist(ctx.database, ctx.query); }},

      // User
      {"/user/detail",     [](const RouteContext& ctx, const std::string&) { return HandleUserDetail(ctx.database, ctx.handlers.userDetail); }},
      {"/user/vip/detail", [](const RouteContext& ctx, const std::string&) { return HandleUserVipDetail(ctx.database, ctx.handlers.userVip); }},
      {"/user/playlist",   [](const RouteContext& ctx, const std::string&) { return HandleUserPlaylist(ctx.database, ctx.query, ctx.handlers.userPlaylist, ctx.handlers.registerDevice); }},
      {"/user/history",    [](const RouteContext& ctx, const std::string&) { return HandleUserHistory(ctx.database, ctx.query); }},
      {"/user/cloud",      [](const RouteContext& ctx, const std::string&) { return HandleUserCloud(ctx.database, ctx.query); }},
      {"/user/cloud/url",  nullptr},  // not yet ported
      {"/playhistory/upload",[](const RouteContext& ctx, const std::string&) { return HandlePlayHistoryUpload(ctx.database, ctx.query); }},

      // Youth / VIP
      {"/youth/day/vip",        [](const RouteContext&, const std::string&) { return HandleYouthDayVip(); }},
      {"/youth/day/vip/upgrade",[](const RouteContext&, const std::string&) { return HandleYouthDayVip(); }},
      {"/youth/listen/song",   [](const RouteContext& ctx, const std::string&) { return HandleYouthListenSong(ctx.database); }},
      {"/youth/vip/ad",        [](const RouteContext& ctx, const std::string&) { return HandleYouthVipAd(ctx.database); }},
      {"/youth/month/vip/record",nullptr},  // not yet ported

      // Catalog
      {"/album/detail",  [](const RouteContext& ctx, const std::string&) { return HandleAlbumDetail(ctx.query); }},
      {"/album/songs",   [](const RouteContext& ctx, const std::string&) { return HandleAlbumSongs(ctx.query); }},
      {"/artist/detail", [](const RouteContext& ctx, const std::string&) { return HandleArtistDetail(ctx.query); }},
      {"/artist/audios", [](const RouteContext& ctx, const std::string&) { return HandleArtistAudios(ctx.query); }},
      {"/artist/albums", [](const RouteContext& ctx, const std::string&) { return HandleArtistAlbums(ctx.query); }},
      {"/artist/follow",  nullptr},  // not yet ported
      {"/artist/unfollow",nullptr},  // not yet ported
      // Grouped: /comment/music, /comment/playlist, /comment/album share a handler
      {"/comment/music",   [](const RouteContext&, const std::string& path) { return HandleCommentMusicPlaylistAlbum(path); }},
      {"/comment/playlist",[](const RouteContext&, const std::string& path) { return HandleCommentMusicPlaylistAlbum(path); }},
      {"/comment/album",  [](const RouteContext&, const std::string& path) { return HandleCommentMusicPlaylistAlbum(path); }},
      {"/comment/music/classify",nullptr},  // not yet ported
      {"/comment/music/hotword", nullptr},  // not yet ported
      {"/comment/floor",  nullptr},  // not yet ported
      {"/comment/count",  nullptr},  // not yet ported
      {"/favorite/count", nullptr},  // not yet ported
      {"/video/url",      nullptr},  // not yet ported
  };
  return table;
}

// Method bits for "read-strict / write-loose" dispatch.
// Frontend currently sends all traffic as GET (apiPost is dead code), so write
// routes must allow GET or logout/favorites/upload break with 405.
enum : unsigned {
  kMethodGet = 1,
  kMethodHead = 2,
  kMethodPost = 4,
};

unsigned MethodBit(const std::string& method) {
  if (method.size() == 3 &&
      (method[0] == 'G' || method[0] == 'g') &&
      (method[1] == 'E' || method[1] == 'e') &&
      (method[2] == 'T' || method[2] == 't')) {
    return kMethodGet;
  }
  if (method.size() == 4 &&
      (method[0] == 'H' || method[0] == 'h') &&
      (method[1] == 'E' || method[1] == 'e') &&
      (method[2] == 'A' || method[2] == 'a') &&
      (method[3] == 'D' || method[3] == 'd')) {
    return kMethodHead;
  }
  if (method.size() == 4 &&
      (method[0] == 'P' || method[0] == 'p') &&
      (method[1] == 'O' || method[1] == 'o') &&
      (method[2] == 'S' || method[2] == 's') &&
      (method[3] == 'T' || method[3] == 't')) {
    return kMethodPost;
  }
  return 0;
}

// Write-only routes (POST). Dual-purpose /settings/device still allows GET
// for device load; mutations use POST. Frontend write callers use apiPost.
unsigned AllowedMethods(const std::string& path) {
  static const std::unordered_map<std::string, unsigned> kWriteRoutes = {
      {"/auth/logout", kMethodPost},
      {"/playlist/add", kMethodPost},
      {"/playlist/del", kMethodPost},
      {"/playlist/tracks/add", kMethodPost},
      {"/playlist/tracks/del", kMethodPost},
      {"/playhistory/upload", kMethodPost},
      {"/register/dev", kMethodPost},
      // Read fingerprint + write overrides share one path.
      {"/settings/device", kMethodGet | kMethodPost},
  };
  auto it = kWriteRoutes.find(path);
  if (it != kWriteRoutes.end()) return it->second;
  // Read / diagnostics: GET and HEAD only.
  return kMethodGet | kMethodHead;
}

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
  StripSessionCredentials(response.body);
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
  const auto& table = GetRouteTable();
  auto it = table.find(path);
  if (it != table.end() && it->second) {
    if ((AllowedMethods(path) & MethodBit(method)) == 0) {
      return JsonResponse(
          {{"status", 0},
           {"error_code", 405},
           {"error", "Method Not Allowed"},
           {"path", path}},
          405);
    }
    RouteContext ctx{database_, handlers_, query, headers, body};
    return it->second(ctx, path);
  }

  // Route recognized (in table) but handler not yet ported, OR
  // fallback route recognized by IsKnownCompatRoute but not in table.
  return JsonResponse(NativeNotImplementedPayload(path), 501);
}

bool IsKnownCompatRoute(const std::string& path) {
  const auto& table = GetRouteTable();
  if (table.count(path)) return true;
  // Legacy fallback routes (not dispatched but recognized)
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
