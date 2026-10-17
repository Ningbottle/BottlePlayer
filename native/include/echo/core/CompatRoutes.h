#pragma once

#include "echo/core/CompatApiUtils.h"

namespace echo::core {

// DiagnosticsRoutes
CompatResponse HandleHealth();
CompatResponse HandleServerNow();
CompatResponse HandleDiagnosticsMemory();

// Debug-only signature-family A/B probe (/diagnostics/signature-family).
// Release builds never define the handler nor register the route: the route
// table entry in CompatApi.cpp is compiled under #ifndef NDEBUG, so Release
// answers 404 via IsKnownCompatRoute. `budgetMs` is the shared wall-clock
// budget for ALL probes of one invocation; pairs that start after the budget
// is exhausted are reported as NOT_RUN (never as a business failure).
// Injected session transports (both GET and POST) make the probe fully
// offline-testable; production passes {} and uses the real network.
#ifndef NDEBUG
CompatResponse HandleSignatureFamilyProbe(
    storage::Database& database,
    const LoginHttpGet& sessionHttpGet,
    const LoginHttpPost& sessionHttpPost,
    long long budgetMs,
    bool reverseOrder = false);
#endif

// LoginRoutes
CompatResponse HandleLoginQrKey(
    storage::Database& database,
    const std::function<nlohmann::json(const DeviceInfo&)>& handler,
    const QueryMap& query = {},
    const execution::OperationContext& operation = execution::CurrentOperation());
CompatResponse HandleLoginQrCancel(storage::Database& database, const QueryMap& query);
CompatResponse HandleLoginQrCreate(const QueryMap& query);
CompatResponse HandleLoginQrCheck(
    storage::Database& database,
    const QueryMap& query,
    const std::function<nlohmann::json(const DeviceInfo&, std::string)>& handler,
    const LoginHttpGet& sessionHttpGet = {},
    const LoginHttpPost& sessionHttpPost = {},
    const std::function<std::string(const DeviceInfo&, const std::string&,
                                    const std::string&, std::string*)>&
        qrRegisterHandler = {},
    const execution::OperationContext& operation = execution::CurrentOperation());
CompatResponse HandleAuthLogout(storage::Database& database);
CompatResponse HandleSettingsDevice(storage::Database& database, const QueryMap& query,
    const execution::OperationContext& operation = execution::CurrentOperation());

// UserRoutes
CompatResponse HandleUserDetail(
    storage::Database& database,
    const std::function<nlohmann::json(std::string, std::string)>& handler,
    const LoginHttpGet& sessionHttpGet = {},
    const LoginHttpPost& sessionHttpPost = {},
    const execution::OperationContext& operation = execution::CurrentOperation());
CompatResponse HandleUserVipDetail(
    storage::Database& database,
    const std::function<nlohmann::json(std::string, std::string)>& handler,
    const LoginHttpGet& sessionHttpGet = {},
    const LoginHttpPost& sessionHttpPost = {},
    const execution::OperationContext& operation = execution::CurrentOperation());
CompatResponse HandleUserPlaylist(
    storage::Database& database,
    const QueryMap& query,
    const std::function<nlohmann::json(
        const DeviceInfo&, std::string, std::string, int, int)>& handler,
    const std::function<std::string(const DeviceInfo&, std::string, std::string, std::string*)>& registerHandler = {},
    const LoginHttpGet& sessionHttpGet = {},
    const LoginHttpPost& sessionHttpPost = {},
    const execution::OperationContext& operation = execution::CurrentOperation());
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
CompatResponse HandleTopAlbum(storage::Database& database, const QueryMap& query);
CompatResponse HandleRankList();
CompatResponse HandleTopSong(const QueryMap& query);
CompatResponse HandleRankAudio(const QueryMap& query);
CompatResponse HandleEverydayRecommend(
    storage::Database& database,
    const std::function<nlohmann::json(std::string, std::string)>& handler);
CompatResponse HandlePersonalFm(storage::Database& database, const QueryMap& query);

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
// Day claim: GET /youth/day/vip. Always signs with the family that minted the
// session (kProjectEdition) in every build flavour — there is deliberately no
// longer a compile-time switch that leaves Release on the standard family,
// because that asymmetry was the defect. See ADR-0005 追记.
// A Debug build additionally accepts ?profile=standard as a diagnostic A/B
// control and marks the response with diagnostic_profile:"standard"; a Release
// build ignores the parameter entirely.
CompatResponse HandleYouthDayVip(
    storage::Database& database,
    const QueryMap& query,
    const LoginHttpGet& sessionHttpGet = {},
    const LoginHttpPost& sessionHttpPost = {});
CompatResponse HandleYouthDayVipUpgrade(storage::Database& database);
CompatResponse HandleYouthListenSong(storage::Database& database);
CompatResponse HandleYouthVipAd(storage::Database& database);

// RegisterRoutes
CompatResponse HandleRegisterDev(storage::Database& database, const QueryMap& query,
    const execution::OperationContext& operation = execution::CurrentOperation(),
    const std::function<std::string(const DeviceInfo&, std::string, std::string, std::string*)>& registerHandler = {});

}  // namespace echo::core
