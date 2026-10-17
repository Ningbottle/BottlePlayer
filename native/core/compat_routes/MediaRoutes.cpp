#include "echo/core/CompatApiUtils.h"
#include "echo/core/CatalogService.h"
#include "echo/core/HomeService.h"
#include "echo/core/RadioService.h"
#include "echo/core/LyricService.h"
#include "echo/core/PrivilegeService.h"
#include "echo/core/RankService.h"
#include "echo/core/SearchService.h"
#include "echo/core/SongService.h"
#include "echo/core/CompatRequestContext.h"
#include "echo/storage/DeviceRepository.h"
#include "echo/storage/SessionRepository.h"
#include "echo/core/DeviceService.h"

namespace echo::core {

namespace {
LyricService& SharedLyricService() {
  // Public lyric identity only (no session/token), one bounded cache shared
  // by search and download so a rejected accesskey can invalidate candidates.
  static LyricService service;
  return service;
}
}  // namespace

// Search & Discovery

CompatResponse HandleSearchHot(const QueryMap& query) {
  SearchService search;
  return JsonResponse(search.Hot(QueryInt(query, "count", 20)));
}

CompatResponse HandleSearchDefault() {
  return JsonResponse({{"status", 1}, {"data", {{"keyword", ""}, {"show_keyword", ""}}}});
}

CompatResponse HandleSearchSuggest(const QueryMap& query) {
  SearchService search;
  return JsonResponse(search.Suggest(
      QueryValue(query, "keywords", QueryValue(query, "keyword")),
      QueryInt(query, "count", QueryInt(query, "MusicTipCount", 10))));
}

CompatResponse HandleSearch(
    const QueryMap& query,
    const std::function<nlohmann::json(std::string, std::string, int, int)>& handler) {
  const auto keywords = QueryValue(query, "keywords", QueryValue(query, "keyword"));
  const auto type = QueryValue(query, "type", "song");
  const auto page = QueryInt(query, "page", 1);
  const auto pageSize = QueryInt(query, "pagesize", QueryInt(query, "pageSize", 30));
  if (handler) {
    return JsonResponse(handler(keywords, type, page, pageSize));
  }
  SearchService search;
  return JsonResponse(search.Search(keywords, type, page, pageSize));
}

CompatResponse HandleTopAlbumPlaylistRecommendRankTopTopIp(const std::string& path) {
  return JsonResponse({{"status", 1}, {"data", nlohmann::json::array()}});
}
CompatResponse HandleTopAlbum(storage::Database& database, const QueryMap& query) {
  CompatRequestContext context(database);
  return JsonResponse(HomeService().GetRecommendedAlbums(QueryInt(query, "page", 1),
      QueryInt(query, "pagesize", 24), context.Device(), context.TokenOrEmpty()));
}

CompatResponse HandleRankList() {
  RankService ranks;
  return JsonResponse(ranks.List());
}

CompatResponse HandleTopSong(const QueryMap& query) {
  RankService ranks;
  return JsonResponse(ranks.GetSongs(
      6666,
      QueryInt(query, "page", 1),
      QueryInt(query, "pagesize", 30)));
}

CompatResponse HandleRankAudio(const QueryMap& query) {
  RankService ranks;
  return JsonResponse(ranks.GetSongs(
      QueryInt(query, "rankid", 0),
      QueryInt(query, "page", 1),
      QueryInt(query, "pagesize", 30)));
}

CompatResponse HandleEverydayRecommend(
    storage::Database& database,
    const std::function<nlohmann::json(std::string, std::string)>& handler) {
  if (handler) {
    return JsonResponse(handler("", ""));
  }
  storage::SessionRepository sessionRepo(database);
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

CompatResponse HandlePersonalFm(storage::Database& database, const QueryMap& query) {
  CompatRequestContext ctx(database);
  const RadioContext context{
      .userId = ctx.UserIdOr(""), .token = ctx.TokenOrEmpty(),
      .hash = QueryValue(query, "hash"),
      .songId = QueryValue(query, "songid", QueryValue(query, "song_id", QueryValue(query, "album_audio_id"))),
      .playedSeconds = QueryInt(query, "playtime", 0),
      .remainingSongs = QueryInt(query, "remain_songcnt", QueryInt(query, "remainSongCount", 0)),
      .overplay = QueryInt(query, "is_overplay", 0) != 0, .device = ctx.Device(),
      .action = QueryValue(query, "action", "play"), .songPoolId = QueryInt(query, "song_pool_id", 0),
  };
  return JsonResponse(RadioService().Recommend(context));
}

// Song & Lyric

CompatResponse HandlePrivilegeLite(const QueryMap& query) {
  PrivilegeService privilege;
  return JsonResponse(privilege.GetLite(
      QueryValue(query, "hash"),
      QueryValue(query, "album_id")));
}

CompatResponse HandleSearchLyric(
    const QueryMap& query,
    const std::function<nlohmann::json(std::string)>& handler) {
  const auto hash = QueryValue(query, "hash");
  if (handler) {
    return JsonResponse(handler(hash));
  }
  return JsonResponse(SharedLyricService().Search(hash));
}

CompatResponse HandleLyric(
    const QueryMap& query,
    const std::function<nlohmann::json(std::string, std::string)>& handler) {
  const auto id = QueryValue(query, "id");
  const auto accessKey = QueryValue(
      query,
      "accesskey",
      QueryValue(query, "accessKey", QueryValue(query, "access_key")));
  if (handler) {
    return JsonResponse(handler(id, accessKey));
  }
  return JsonResponse(SharedLyricService().GetDetail(id, accessKey));
}

CompatResponse HandleSongClimax(const QueryMap& query) {
  SongService songSvc;
  return JsonResponse(songSvc.GetClimax(QueryValue(query, "hash")));
}

CompatResponse HandleSongRanking(const QueryMap& query) {
  SongService songSvc;
  return JsonResponse(songSvc.GetRanking(QueryValue(query, "album_audio_id")));
}

CompatResponse HandleSongRankingFilter(const QueryMap& query) {
  SongService songSvc;
  return JsonResponse(songSvc.GetRankingFilter(
      QueryValue(query, "album_audio_id"),
      QueryInt(query, "page", 1),
      QueryInt(query, "pagesize", 30)));
}

CompatResponse HandleImagesAudio(const QueryMap& query) {
  HomeService homeSvc;
  return JsonResponse(homeSvc.GetImagesAudio(
      QueryValue(query, "hash"),
      QueryValue(query, "audio_id"),
      QueryValue(query, "album_audio_id"),
      QueryValue(query, "filename"),
      QueryInt(query, "count", 5)));
}

// Catalog

CompatResponse HandleAlbumDetail(const QueryMap& query) {
  CatalogService catalog;
  return JsonResponse(catalog.GetAlbumDetail(QueryValue(query, "id")));
}

CompatResponse HandleAlbumSongs(const QueryMap& query) {
  CatalogService catalog;
  return JsonResponse(catalog.GetAlbumSongs(
      QueryValue(query, "id"),
      QueryInt(query, "page", 1),
      QueryInt(query, "pagesize", 30)));
}

CompatResponse HandleArtistDetail(const QueryMap& query) {
  CatalogService catalog;
  return JsonResponse(catalog.GetArtistDetail(QueryValue(query, "id")));
}

CompatResponse HandleArtistAudios(const QueryMap& query) {
  CatalogService catalog;
  return JsonResponse(catalog.GetArtistSongs(
      QueryValue(query, "id"),
      QueryInt(query, "page", 1),
      QueryInt(query, "pagesize", 200),
      QueryValue(query, "sort", "hot")));
}

CompatResponse HandleArtistAlbums(const QueryMap& query) {
  CatalogService catalog;
  return JsonResponse(catalog.GetArtistAlbums(
      QueryValue(query, "id"),
      QueryInt(query, "page", 1),
      QueryInt(query, "pagesize", 30),
      QueryValue(query, "sort", "hot")));
}

CompatResponse HandleCommentMusicPlaylistAlbum(const std::string& path) {
  return JsonResponse(EmptyPagedData());
}

}  // namespace echo::core
