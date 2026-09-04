// EchoRouteContractTest — route table dispatch, 404/501, known route recognition.
// Extracted from basic_contract_tests.cpp (lines 3226-3342) for independent build.

#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>

#include "echo/core/CompatApi.h"
#include "echo/core/CompatApiUtils.h"
#include "echo/core/Dto.h"
#include "echo/storage/Database.h"
#include "echo/storage/DeviceRepository.h"
#include "echo/storage/SessionRepository.h"

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
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

  std::cout << "[RouteContract] Testing redacted device diagnostics..." << std::endl;
  {
    echo::core::DeviceInfo device;
    device.dfid = "super-secret-dfid";
    device.mid = "123456789012345678901234567890123456789";
    device.uuid = "super-secret-uuid";
    device.guid = "super-secret-guid";
    device.registered = true;
    const auto summary = echo::core::DescribeDeviceIdentity(device);
    assert(summary.find(device.dfid) == std::string::npos);
    assert(summary.find(device.mid) == std::string::npos);
    assert(summary.find(device.uuid) == std::string::npos);
    assert(summary.find(device.guid) == std::string::npos);
    assert(summary.find("dfid_fp=") != std::string::npos);
    assert(summary.find("dfid_len=17") != std::string::npos);
    assert(summary.find("mid_kind=android") != std::string::npos);
    assert(summary.find("guid_present=Y") != std::string::npos);
  }

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
        "/personal/fm",
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

  // ── Method binding: read routes reject POST with 405 ──────────────────
  std::cout << "[RouteContract] Testing method binding (405)..." << std::endl;
  {
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();
    echo::core::CompatApi api(db);

    auto postHealth = api.Handle("POST", "/health", {}, {}, "{}");
    assert(postHealth.httpStatus == 405);
    assert(postHealth.body["error_code"] == 405);

    auto getHealth = api.Handle("GET", "/health", {}, {}, "");
    assert(getHealth.httpStatus == 200);

    // Pure write routes: GET → 405, POST allowed (not 405).
    auto getLogout = api.Handle("GET", "/auth/logout", {}, {}, "");
    assert(getLogout.httpStatus == 405);
    auto postLogout = api.Handle("POST", "/auth/logout", {}, {}, "");
    assert(postLogout.httpStatus != 405);

    auto getUpload = api.Handle("GET", "/playhistory/upload", {}, {}, "");
    assert(getUpload.httpStatus == 405);
    auto postUpload = api.Handle("POST", "/playhistory/upload", {}, {}, "");
    assert(postUpload.httpStatus != 405);

    // Dual-purpose device settings: GET load still allowed.
    auto getDevice = api.Handle("GET", "/settings/device", {}, {}, "");
    assert(getDevice.httpStatus != 405);

    std::cout << "  [ok] method binding: read POST 405; write GET 405; device GET ok"
              << std::endl;
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

  // ── /user/vip/detail: upstream failure must not become authoritative is_vip=0
  std::cout << "[RouteContract] Testing VIP detail failure is not a fake no-VIP snapshot..." << std::endl;
  {
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();
    echo::storage::SessionRepository repo(db);
    echo::core::SessionInfo session;
    session.userId = "42";
    session.token = "tok";
    session.nickname = "Bottle";
    repo.Save(session);

    echo::core::CompatApiHandlers handlers;
    handlers.userVip = [](std::string, std::string) -> nlohmann::json {
      return {
          {"status", 0},
          {"error_code", 51002},
          {"error", "activity rejected"},
          {"data", nullptr},
      };
    };
    echo::core::CompatApi api(db, handlers);
    auto resp = api.Handle("GET", "/user/vip/detail", {}, {}, "");
    assert(resp.httpStatus == 200);
    assert(resp.body.value("status", 1) == 0);
    assert(resp.body.contains("authoritative"));
    assert(resp.body["authoritative"] == false);
    assert(resp.body.value("error_code", 0) == 51002);
    assert(resp.body.value("error", std::string{}) == "activity rejected");
    assert(resp.body["data"].is_null());
    assert(!resp.body.contains("is_vip"));
    if (resp.body.contains("data") && resp.body["data"].is_object()) {
      assert(resp.body["data"].value("is_vip", -1) != 0 ||
             resp.body.value("authoritative", true) == false);
    }
    std::cout << "  [ok] VIP detail failure stays status=0 authoritative=false" << std::endl;
  }

  {
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();
    echo::core::CompatApi api(db);
    auto resp = api.Handle("GET", "/user/vip/detail", {}, {}, "");
    assert(resp.body.value("status", 1) == 0);
    assert(resp.body.contains("authoritative"));
    assert(resp.body["authoritative"] == false);
    assert(resp.body["data"].is_null());
    const auto code = resp.body.contains("error_code") ? resp.body["error_code"].dump() : "";
    assert(code.find("native_vip_no_session") != std::string::npos ||
           resp.body.value("error", std::string{}).find("not logged in") != std::string::npos);
    std::cout << "  [ok] VIP detail without session is non-authoritative" << std::endl;
  }

  // ── /youth/day/vip(+upgrade): routes must be live, not hardcoded-disabled ─
  // Regression guard: before 2026-09 both routes answered a hardcoded
  // kugou_vip_legacy_disabled payload regardless of session state. They were
  // re-enabled to retest upstream with the reference repo's 2026-08-31
  // headers; without a session they must now hit the login gate.
  std::cout << "[RouteContract] Testing /youth/day/vip routes are live..." << std::endl;
  {
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();
    echo::core::CompatApi api(db);

    for (const char* route : {"/youth/day/vip", "/youth/day/vip/upgrade"}) {
      auto resp = api.Handle("GET", route, {}, {}, "");
      assert(resp.httpStatus == 200);
      assert(resp.body.value("status", 1) == 0);
      assert(resp.body.value("error", std::string{}) == "not logged in");
      assert(!resp.body.contains("error_code") ||
             resp.body["error_code"] != "kugou_vip_legacy_disabled");
    }
    std::cout << "  [ok] /youth/day/vip(+upgrade) live: login gate, no legacy-disabled reject" << std::endl;
  }

  std::cout << "[RouteContract] Testing /user/playlist 20017 keeps trusted device..." << std::endl;
  {
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();
    echo::storage::SessionRepository repo(db);
    echo::core::SessionInfo session;
    session.userId = "42";
    session.token = "tok";
    repo.Save(session);
    echo::storage::DeviceRepository devices(db);
    echo::core::DeviceInfo device;
    device.registered = true;
    device.dfid = "abcdefghijklmnopqrstuvwx";
    device.guid = "registered-device-guid";
    device.appid = "3116";
    device.clientver = "11440";
    devices.Save(device);

    int playlistCalls = 0;
    int registerCalls = 0;
    std::vector<std::string> playlistDfids;
    echo::core::CompatApiHandlers handlers;
    handlers.userPlaylist = [&](const echo::core::DeviceInfo& requestDevice,
                                std::string, std::string, int, int) {
      playlistCalls += 1;
      playlistDfids.push_back(requestDevice.dfid);
      return nlohmann::json{
          {"status", 0},
          {"errcode", 20017},
          {"data", {{"list", nlohmann::json::array()}}},
      };
    };
    handlers.registerDevice = [&](const echo::core::DeviceInfo&, std::string, std::string,
                                  std::string*) {
      registerCalls += 1;
      return std::string{"newdfidnewdfidnewdfidnewd"};
    };

    echo::core::CompatApi api(db, handlers);
    auto resp = api.Handle("GET", "/user/playlist", {{"page", "1"}, {"pagesize", "30"}}, {}, "");
    assert(resp.body.value("status", 1) == 0);
    assert(echo::core::IsKuGouErrorCode(resp.body, 20017));
    assert(playlistCalls == 1);
    assert(registerCalls == 0);
    assert(playlistDfids.size() == 1);
    assert(playlistDfids[0] == "abcdefghijklmnopqrstuvwx");
    auto saved = devices.Load();
    assert(saved && saved->dfid == "abcdefghijklmnopqrstuvwx");
    assert(saved && saved->registered);
    std::cout << "  [ok] 20017 surfaces upstream error without rotating the trusted dfid" << std::endl;
  }

  {
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();
    echo::storage::SessionRepository repo(db);
    echo::core::SessionInfo session;
    session.userId = "42";
    session.token = "tok";
    repo.Save(session);
    echo::storage::DeviceRepository devices(db);
    echo::core::DeviceInfo device;
    device.registered = false;
    device.dfid = "-";
    device.guid = "fresh-device-guid";
    device.appid = "3116";
    device.clientver = "11440";
    devices.Save(device);

    int playlistCalls = 0;
    int registerCalls = 0;
    std::vector<std::string> playlistDfids;
    echo::core::CompatApiHandlers handlers;
    handlers.userPlaylist = [&](const echo::core::DeviceInfo& requestDevice,
                                std::string, std::string, int, int) {
      playlistCalls += 1;
      playlistDfids.push_back(requestDevice.dfid);
      return nlohmann::json{{"status", 1}, {"errcode", 0}, {"data", {{"list", nlohmann::json::array()}}}};
    };
    handlers.registerDevice = [&](const echo::core::DeviceInfo&, std::string, std::string,
                                  std::string*) {
      registerCalls += 1;
      return std::string{"newdfidnewdfidnewdfidnewd"};
    };
    echo::core::CompatApi api(db, handlers);
    auto resp = api.Handle("GET", "/user/playlist", {}, {}, "");
    assert(resp.body.value("status", 1) == 1);
    assert(registerCalls == 1);
    assert(playlistCalls == 1);
    assert(playlistDfids.size() == 1);
    assert(playlistDfids[0] == "newdfidnewdfidnewdfidnewd");
    auto saved = devices.Load();
    assert(saved && saved->dfid == "newdfidnewdfidnewdfidnewd");
    assert(saved && saved->registered);
    std::cout << "  [ok] unregistered device still performs initial registration before first attempt" << std::endl;
  }

  std::cout << "[RouteContract] All tests passed!" << std::endl;
  return 0;
}
