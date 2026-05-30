#pragma once

#include "echo/core/CompatApiUtils.h"

namespace echo::core {

// DiagnosticsRoutes
CompatResponse HandleHealth();
CompatResponse HandleServerNow();
CompatResponse HandleDiagnosticsMemory();

// LoginRoutes
CompatResponse HandleLoginQrKey(
    storage::Database& database,
    const std::function<nlohmann::json(const DeviceInfo&)>& handler);
CompatResponse HandleLoginQrCreate(const QueryMap& query);
CompatResponse HandleLoginQrCheck(
    storage::Database& database,
    const QueryMap& query,
    const std::function<nlohmann::json(const DeviceInfo&, std::string)>& handler);
CompatResponse HandleAuthLogout(storage::Database& database);
CompatResponse HandleSettingsDevice(storage::Database& database, const QueryMap& query);

// UserRoutes
CompatResponse HandleUserDetail(
    storage::Database& database,
    const std::function<nlohmann::json(std::string, std::string)>& handler);
CompatResponse HandleUserVipDetail(
    storage::Database& database,
    const std::function<nlohmann::json(std::string, std::string)>& handler);
CompatResponse HandleUserPlaylist(
    storage::Database& database,
    const QueryMap& query,
    const std::function<nlohmann::json(std::string, std::string, int, int)>& handler);
CompatResponse HandleUserHistory(storage::Database& database, const QueryMap& query);
CompatResponse HandleUserCloud(storage::Database& database, const QueryMap& query);
CompatResponse HandlePlayHistoryUpload(storage::Database& database, const QueryMap& query);

// PlaylistRoutes
CompatResponse HandlePlaylistAdd(storage::Database& database, const QueryMap& query);
CompatResponse HandlePlaylistDel(storage::Database& database, const QueryMap& query);
CompatResponse HandlePlaylistTracksAdd(
    storage::Database& database, const QueryMap& query, const std::string& body);
CompatResponse HandlePlaylistTracksDel(storage::Database& database, const QueryMap& query);
CompatResponse HandlePlaylistDetail(
    storage::Database& database,
    const QueryMap& query,
    const std::function<nlohmann::json(std::string, std::string, std::string)>& handler);
CompatResponse HandlePlaylistTrackAll(
    storage::Database& database,
    const QueryMap& query,
    const std::function<nlohmann::json(std::string, int, int)>& handler);
CompatResponse HandlePlaylistTrackAllNew(
    storage::Database& database,
    const QueryMap& query,
    const std::function<nlohmann::json(std::string, int, int)>& handler);
CompatResponse HandlePlaylistTags(storage::Database& database);
CompatResponse HandleTopPlaylist(storage::Database& database, const QueryMap& query);

// MediaRoutes (Search + Song + Catalog + Everyday)
CompatResponse HandleSearchHot(const QueryMap& query);
CompatResponse HandleSearchDefault();
CompatResponse HandleSearchSuggest(const QueryMap& query);
CompatResponse HandleSearch(
    const QueryMap& query,
    const std::function<nlohmann::json(std::string, std::string, int, int)>& handler);
CompatResponse HandleTopAlbumPlaylistRecommendRankTopTopIp(const std::string& path);
CompatResponse HandleRankList();
CompatResponse HandleTopSong(const QueryMap& query);
CompatResponse HandleRankAudio(const QueryMap& query);
CompatResponse HandleEverydayRecommend(
    storage::Database& database,
    const std::function<nlohmann::json(std::string, std::string)>& handler);

CompatResponse HandlePrivilegeLite(const QueryMap& query);
CompatResponse HandleSearchLyric(
    const QueryMap& query,
    const std::function<nlohmann::json(std::string)>& handler);
CompatResponse HandleLyric(
    const QueryMap& query,
    const std::function<nlohmann::json(std::string, std::string)>& handler);
CompatResponse HandleSongClimax(const QueryMap& query);
CompatResponse HandleSongRanking(const QueryMap& query);
CompatResponse HandleSongRankingFilter(const QueryMap& query);
CompatResponse HandleImagesAudio(const QueryMap& query);

CompatResponse HandleAlbumDetail(const QueryMap& query);
CompatResponse HandleAlbumSongs(const QueryMap& query);
CompatResponse HandleArtistDetail(const QueryMap& query);
CompatResponse HandleArtistAudios(const QueryMap& query);
CompatResponse HandleArtistAlbums(const QueryMap& query);
CompatResponse HandleCommentMusicPlaylistAlbum(const std::string& path);

// YouthVipRoutes
CompatResponse HandleYouthDayVip();
CompatResponse HandleYouthListenSong(storage::Database& database);
CompatResponse HandleYouthVipAd(storage::Database& database);

// RegisterRoutes
CompatResponse HandleRegisterDev(storage::Database& database, const QueryMap& query);

}  // namespace echo::core
