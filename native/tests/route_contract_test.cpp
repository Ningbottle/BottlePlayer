// EchoRouteContractTest — route table dispatch, 404/501, known route recognition.
// Extracted from basic_contract_tests.cpp (lines 3226-3342) for independent build.

#include <cassert>
#include <filesystem>
#include <iostream>

#include "echo/core/CompatApi.h"
#include "echo/storage/Database.h"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace {

std::filesystem::path TestDbPath() {
  static int counter = 0;
  auto path = std::filesystem::temp_directory_path() /
              (L"echomusic-route-test-" + std::to_wstring(++counter) + L".db");
  std::filesystem::remove(path);
  std::filesystem::remove(path.wstring() + L"-wal");
  std::filesystem::remove(path.wstring() + L"-shm");
  return path;
}

}  // namespace

int main() {
  std::cout << "[RouteContract] started" << std::endl;
#if defined(_MSC_VER)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

  // ── IsKnownCompatRoute: all documented routes must be recognised ──────
  std::cout << "[RouteContract] Testing IsKnownCompatRoute..." << std::endl;
  {
    const char* contractRoutes[] = {
        "/health",
        "/server/now",
        "/diagnostics/memory",
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
    const size_t contractRouteCount = sizeof(contractRoutes) / sizeof(contractRoutes[0]);
    for (size_t i = 0; i < contractRouteCount; ++i) {
      assert(echo::core::IsKnownCompatRoute(contractRoutes[i]));
    }

    // Hardcoded fallback routes (not in dispatch table but recognised by IsKnownCompatRoute)
    assert(echo::core::IsKnownCompatRoute("/kmr/audio/mv"));
    assert(echo::core::IsKnownCompatRoute("/video/privilege"));
    assert(echo::core::IsKnownCompatRoute("/video/detail"));

    // Unknown routes must NOT be recognised.
    assert(!echo::core::IsKnownCompatRoute("/nonexistent"));
    assert(!echo::core::IsKnownCompatRoute("/unknown/route"));
    assert(!echo::core::IsKnownCompatRoute("/"));
    assert(!echo::core::IsKnownCompatRoute(""));

    std::cout << "  [ok] " << contractRouteCount << " contract routes + 3 fallback routes recognised" << std::endl;
  }

  // ── Unknown route returns 404 ─────────────────────────────────────────
  std::cout << "[RouteContract] Testing unknown route 404..." << std::endl;
  {
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();
    echo::core::CompatApi api(db);

    auto unknown = api.Handle("GET", "/not/a/route", {}, {}, "");
    assert(unknown.httpStatus == 404);
    assert(unknown.body["status"] == 0);
    assert(unknown.body["error_code"] == 404);

    auto unknownPost = api.Handle("POST", "/bad/post", {}, {}, "{}");
    assert(unknownPost.httpStatus == 404);

    std::cout << "  [ok] Unknown routes return 404" << std::endl;
  }

  // ── Not-yet-ported routes return 501 ──────────────────────────────────
  std::cout << "[RouteContract] Testing not-yet-ported routes return 501..." << std::endl;
  {
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();
    echo::core::CompatApi api(db);

    const char* notPortedRoutes[] = {
        "/captcha/sent",
        "/login/cellphone",
        "/login/wx/create",
        "/login/wx/check",
        "/login/openplat",
        "/user/cloud/url",
        "/youth/month/vip/record",
        "/artist/follow",
        "/artist/unfollow",
        "/comment/music/classify",
        "/comment/music/hotword",
        "/comment/floor",
        "/comment/count",
        "/favorite/count",
        "/video/url",
    };
    for (const auto* route : notPortedRoutes) {
      auto resp = api.Handle("GET", route, {}, {}, "");
      assert(resp.httpStatus == 501);
      assert(resp.body["error_code"] == "native_not_implemented");
    }

    std::cout << "  [ok] " << (sizeof(notPortedRoutes) / sizeof(notPortedRoutes[0]))
              << " not-yet-ported routes return 501" << std::endl;
  }

  // ── Implemented route dispatch: handler injection ─────────────────────
  // Verify that injected handlers are actually called for implemented routes.
  // Without this, route table refactors could silently break handler mapping.
  std::cout << "[RouteContract] Testing implemented route dispatch..." << std::endl;
  {
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();

    echo::core::CompatApiHandlers handlers;
    int songUrlCalled = 0;
    int searchCalled = 0;
    int playlistTracksCalled = 0;

    handlers.songUrl = [&](std::string hash, std::string quality, std::string ppageId) -> nlohmann::json {
      songUrlCalled++;
      return {{"status", 1}, {"hash", hash}, {"quality", quality}, {"ppage_id", ppageId}};
    };
    handlers.search = [&](std::string keywords, std::string type, int page, int pageSize) -> nlohmann::json {
      searchCalled++;
      return {{"status", 1}, {"keywords", keywords}, {"type", type}, {"page", page}, {"pageSize", pageSize}};
    };
    handlers.playlistTracks = [&](std::string id, int page, int pageSize) -> nlohmann::json {
      playlistTracksCalled++;
      return {{"status", 1}, {"id", id}, {"page", page}, {"pagesize", pageSize}};
    };

    echo::core::CompatApi api(db, handlers);

    // /song/url must dispatch to songUrl handler
    auto songResp = api.Handle("GET", "/song/url",
        {{"hash", "abc123"}, {"quality", "320"}, {"ppage_id", "999"}}, {}, "");
    assert(songResp.httpStatus == 200);
    assert(songUrlCalled == 1);
    assert(songResp.body["hash"] == "abc123");
    assert(songResp.body["quality"] == "320");

    // /search must dispatch to search handler
    auto searchResp = api.Handle("GET", "/search",
        {{"keywords", "test"}, {"type", "song"}, {"page", "2"}, {"pageSize", "20"}}, {}, "");
    assert(searchResp.httpStatus == 200);
    assert(searchCalled == 1);
    assert(searchResp.body["keywords"] == "test");

    // /playlist/track/all must dispatch to playlistTracks handler
    auto plResp = api.Handle("GET", "/playlist/track/all",
        {{"id", "42"}, {"page", "1"}, {"pagesize", "30"}}, {}, "");
    assert(plResp.httpStatus == 200);
    assert(playlistTracksCalled == 1);
    assert(plResp.body["id"] == "42");

    std::cout << "  [ok] /song/url, /search, /playlist/track/all dispatch to injected handlers" << std::endl;
  }

  std::cout << "[RouteContract] All tests passed!" << std::endl;
  return 0;
}
