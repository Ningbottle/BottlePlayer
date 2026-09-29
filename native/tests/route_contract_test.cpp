// EchoRouteContractTest — route table dispatch, 404/501, known route recognition.
// Extracted from basic_contract_tests.cpp (lines 3226-3342) for independent build.

#include <cassert>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "echo/core/CompatApi.h"
#include "echo/core/CompatApiUtils.h"
#include "echo/core/CompatRoutes.h"
#include "echo/core/Crypto.h"
#include "echo/core/Dto.h"
#include "echo/core/HttpClient.h"
#include "echo/core/KuGouProfile.h"
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

    // The signature-family A/B probe is a DEBUG-ONLY route. In Debug builds it
    // must be registered (dispatch table + recognition); in Release builds the
    // registration is compiled out (#ifndef NDEBUG in CompatApi.cpp) so the
    // path answers 404 and no probe can run against a packaged app.
    // _DEBUG tracks the MSVC runtime of THIS test binary; EchoCore is built in
    // the same config, so the two sides always agree.
#ifdef _DEBUG
    assert(echo::core::IsKnownCompatRoute("/diagnostics/signature-family"));
    std::cout << "  [ok] debug-only probe route registered (debug build)" << std::endl;
#else
    assert(!echo::core::IsKnownCompatRoute("/diagnostics/signature-family"));
    std::cout << "  [ok] debug-only probe route absent (release build)" << std::endl;
#endif

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

  // ── /youth/day/vip route-layer profile decision + request counts ────────
  // Debug default → Standard family, exactly 1 upstream POST.
  // Debug candidate profile=concept → Concept family, exactly 1 upstream POST.
  // Release/forced-disabled candidate → reject, exactly 0 upstream POST.
  // Fixed device/session params stay on the claim contract; only the whole
  // profile (appid/clientver/salt→signature) changes for the candidate.
  std::cout << "[RouteContract] Testing /youth/day/vip profile decision + POST counts..." << std::endl;
  {
    auto makeSessionDb = []() {
      auto db = std::make_unique<echo::storage::Database>();
      db->Open(TestDbPath());
      db->Initialize();
      echo::storage::SessionRepository repo(*db);
      echo::core::SessionInfo session;
      session.userId = "42";
      session.token = "tok";
      repo.Save(session);
      echo::storage::DeviceRepository devices(*db);
      echo::core::DeviceInfo device;
      device.registered = true;
      device.dfid = "abcdefghijklmnopqrstuvwx";
      device.guid = "registered-device-guid";
      device.uuid = "0123456789abcdef0123456789abcdef";
      devices.Save(device);
      return db;
    };

    auto captureDay = [&](bool conceptCandidateEnabled, echo::core::QueryMap query,
                          int* postCalls, std::string* url, std::string* body,
                          std::unordered_map<std::string, std::string>* headers) {
      auto db = makeSessionDb();
      echo::core::LoginHttpPost post = [=](const std::string& u, const std::string& b,
                                           const std::unordered_map<std::string, std::string>& h) {
        ++(*postCalls);
        if (url) *url = u;
        if (body) *body = b;
        if (headers) *headers = h;
        return echo::core::HttpResult{
            200, R"({"status":0,"error_code":51002,"error_msg":"device validation failed"})", ""};
      };
      echo::core::LoginHttpGet get = [](const std::string&,
                                        const std::unordered_map<std::string, std::string>&) {
        return echo::core::HttpResult{500, "", "unexpected GET"};
      };
      return echo::core::HandleYouthDayVip(*db, query, get, post, conceptCandidateEnabled);
    };

    // 1) Debug default (no profile param): Standard, 1 POST.
    {
      int postCalls = 0;
      std::string url, body;
      std::unordered_map<std::string, std::string> headers;
      auto resp = captureDay(true, {}, &postCalls, &url, &body, &headers);
      assert(resp.httpStatus == 200);
      assert(resp.body.value("status", -1) == 0);
      assert(postCalls == 1);
      assert(url.find("https://gateway.kugou.com/youth/v1/recharge/receive_vip_listen_song") == 0);
      assert(url.find("appid=1005") != std::string::npos);
      assert(url.find("clientver=20489") != std::string::npos);
      assert(url.find("uuid=-") != std::string::npos);
      assert(url.find("source_id=90139") != std::string::npos);
      assert(headers.count("kg-rf") == 1);
      // Signature family: Standard salt recompute matches; Lite does not.
      {
        std::map<std::string, std::string> q;
        const auto qpos = url.find('?');
        assert(qpos != std::string::npos);
        std::istringstream pairs(url.substr(qpos + 1));
        std::string pair;
        while (std::getline(pairs, pair, '&')) {
          const auto eq = pair.find('=');
          if (eq == std::string::npos) continue;
          q[pair.substr(0, eq)] = pair.substr(eq + 1);
        }
        std::unordered_map<std::string, std::string> signedParams;
        for (const auto& [k, v] : q) {
          if (k != "signature") signedParams[k] = v;
        }
        const auto signature = q.at("signature");
        assert(echo::core::SignatureAndroidParams(signedParams, "",
                                                  echo::core::KuGouSaltKind::Standard) == signature);
        assert(echo::core::SignatureAndroidParams(signedParams, "",
                                                  echo::core::KuGouSaltKind::Lite) != signature);
      }
      std::cout << "  [ok] Debug default day claim: Standard appid=1005/clientver=20489, POST=1"
                << std::endl;
    }

    // 2) Debug candidate profile=concept: Concept family, 1 POST.
    {
      int postCalls = 0;
      std::string url, body;
      std::unordered_map<std::string, std::string> headers;
      auto resp = captureDay(true, {{"profile", "concept"}}, &postCalls, &url, &body, &headers);
      assert(resp.httpStatus == 200);
      assert(resp.body.value("status", -1) == 0);
      assert(postCalls == 1);
      assert(url.find("https://gateway.kugou.com/youth/v1/recharge/receive_vip_listen_song") == 0);
      assert(url.find("appid=3116") != std::string::npos);
      assert(url.find("clientver=11440") != std::string::npos);
      assert(url.find("uuid=-") != std::string::npos);
      assert(url.find("source_id=90139") != std::string::npos);
      assert(headers.count("kg-rf") == 1);
      {
        std::map<std::string, std::string> q;
        const auto qpos = url.find('?');
        assert(qpos != std::string::npos);
        std::istringstream pairs(url.substr(qpos + 1));
        std::string pair;
        while (std::getline(pairs, pair, '&')) {
          const auto eq = pair.find('=');
          if (eq == std::string::npos) continue;
          q[pair.substr(0, eq)] = pair.substr(eq + 1);
        }
        std::unordered_map<std::string, std::string> signedParams;
        for (const auto& [k, v] : q) {
          if (k != "signature") signedParams[k] = v;
        }
        const auto signature = q.at("signature");
        assert(echo::core::SignatureAndroidParams(signedParams, "",
                                                  echo::core::KuGouSaltKind::Lite) == signature);
        assert(echo::core::SignatureAndroidParams(signedParams, "",
                                                  echo::core::KuGouSaltKind::Standard) != signature);
      }
      std::cout << "  [ok] Debug candidate day claim: Concept appid=3116/clientver=11440/lite, POST=1"
                << std::endl;
    }

    // 3) Release/forced-disabled candidate: explicit reject, POST=0.
    {
      int postCalls = 0;
      std::string url = "UNSET";
      auto resp = captureDay(false, {{"profile", "concept"}}, &postCalls, &url, nullptr, nullptr);
      assert(resp.httpStatus == 200);
      assert(resp.body.value("status", 1) == 0);
      assert(resp.body.value("error", std::string{}) == "candidate_profile_unavailable");
      assert(resp.body.value("upstream_called", true) == false);
      assert(resp.body.value("profile_requested", std::string{}) == "concept");
      assert(postCalls == 0);
      assert(url == "UNSET");
      std::cout << "  [ok] Release-style candidate reject: error=candidate_profile_unavailable, "
                << "POST=0 (no silent Standard fallback)" << std::endl;
    }

    // 4) Release-style without candidate param still executes Standard (production default).
    {
      int postCalls = 0;
      std::string url;
      auto resp = captureDay(false, {}, &postCalls, &url, nullptr, nullptr);
      assert(resp.httpStatus == 200);
      assert(postCalls == 1);
      assert(url.find("appid=1005") != std::string::npos);
      std::cout << "  [ok] Release-style default day claim remains Standard, POST=1" << std::endl;
    }

    // 5) CompatApi's build policy: Debug may call the candidate handler;
    // Release must reject it before invoking the injected POST. NDEBUG is
    // deliberately unset for test assertions, so it cannot identify the
    // production configuration of EchoCore.
    {
      auto db = makeSessionDb();
      int postCalls = 0;
      std::string url;
      echo::core::CompatApiHandlers handlers;
      handlers.sessionHttpPost = [&](const std::string& u, const std::string&,
                                     const std::unordered_map<std::string, std::string>&) {
        ++postCalls;
        url = u;
        return echo::core::HttpResult{200, R"({"status":0,"error_code":51002})", ""};
      };
      echo::core::CompatApi api(*db, handlers);
      auto resp = api.Handle("GET", "/youth/day/vip", {{"profile", "concept"}}, {}, "");
      assert(resp.httpStatus == 200);
#ifdef _DEBUG
      assert(postCalls == 1);
      assert(url.find("appid=3116") != std::string::npos);
      std::cout << "  [ok] CompatApi GET /youth/day/vip?profile=concept → Concept, POST=1"
                << std::endl;
#else
      assert(postCalls == 0);
      assert(resp.body.value("error", std::string{}) == "candidate_profile_unavailable");
      std::cout << "  [ok] CompatApi Release /youth/day/vip?profile=concept rejected, POST=0"
                << std::endl;
#endif
    }
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

  // ── Production route seam: /user/vip/detail must send the Concept family
  // (2026-09-15 signature-family experiment) end to end through the real
  // route entry point — protecting the explicit KuGouEdition::Concept at the
  // call site from silent removal. ─────────────────────────────────────────
  std::cout << "[RouteContract] Testing /user/vip/detail production seam uses Concept profile..." << std::endl;
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

    std::string capturedUrl;
    std::unordered_map<std::string, std::string> capturedHeaders;
    echo::core::CompatApiHandlers handlers;
    handlers.sessionHttpGet = [&](const std::string& url,
                                  const std::unordered_map<std::string, std::string>& headers) {
      capturedUrl = url;
      capturedHeaders = headers;
      return echo::core::HttpResult{200, R"({"status":1,"data":{"is_vip":0,"vip_type":0}})", ""};
    };

    echo::core::CompatApi api(db, handlers);
    auto resp = api.Handle("GET", "/user/vip/detail", {}, {}, "");
    assert(resp.httpStatus == 200);
    assert(resp.body.value("status", 1) == 1);
    assert(!capturedUrl.empty());
    assert(capturedUrl.find("https://kugouvip.kugou.com/v1/get_union_vip") != std::string::npos);
    // 整套 Concept 配置（appid/clientver/盐）必须在生产路由出口可见。
    assert(capturedUrl.find("appid=3116") != std::string::npos);
    assert(capturedUrl.find("clientver=11440") != std::string::npos);
    assert(capturedUrl.find("busi_type=concept") != std::string::npos);
    assert(capturedUrl.find("token=tok") != std::string::npos);
    assert(capturedHeaders.count("Cookie") &&
           capturedHeaders.at("Cookie").find("userid=42") != std::string::npos);

    // 盐校验：用 Lite 盐重算签名一致；用 Standard 盐重算不一致（盐被钉死）。
    {
      std::map<std::string, std::string> query;
      const auto qpos = capturedUrl.find('?');
      assert(qpos != std::string::npos);
      std::istringstream pairs(capturedUrl.substr(qpos + 1));
      std::string pair;
      while (std::getline(pairs, pair, '&')) {
        const auto eq = pair.find('=');
        if (eq == std::string::npos) continue;
        query[pair.substr(0, eq)] = pair.substr(eq + 1);
      }
      assert(query.at("signature").size() == 32);
      std::unordered_map<std::string, std::string> signedParams;
      for (const auto& [k, v] : query) {
        if (k != "signature") signedParams[k] = v;
      }
      const std::string signature = query.at("signature");
      assert(echo::core::SignatureAndroidParams(signedParams, "",
                                                echo::core::KuGouSaltKind::Lite) == signature);
      assert(echo::core::SignatureAndroidParams(signedParams, "",
                                                echo::core::KuGouSaltKind::Standard) != signature);
    }
    std::cout << "  [ok] /user/vip/detail routes through Concept appid=3116/clientver=11440/lite-salt" << std::endl;
  }

  // ── Production route seam: /user/playlist must send the Concept family
  // through the real route entry point as well. ────────────────────────────
  std::cout << "[RouteContract] Testing /user/playlist production seam uses Concept profile..." << std::endl;
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

    std::string capturedUrl;
    std::string capturedBody;
    std::unordered_map<std::string, std::string> capturedHeaders;
    echo::core::CompatApiHandlers handlers;
    handlers.sessionHttpPost = [&](const std::string& url, const std::string& body,
                                   const std::unordered_map<std::string, std::string>& headers) {
      capturedUrl = url;
      capturedBody = body;
      capturedHeaders = headers;
      return echo::core::HttpResult{200, R"({"status":1,"data":{"info":[]}})", ""};
    };

    echo::core::CompatApi api(db, handlers);
    auto resp = api.Handle("GET", "/user/playlist", {{"page", "1"}, {"pagesize", "30"}}, {}, "");
    assert(resp.httpStatus == 200);
    assert(resp.body.value("status", 1) == 1);
    assert(!capturedUrl.empty());
    assert(capturedUrl.find("https://gateway.kugou.com/v7/get_all_list") != std::string::npos);
    assert(capturedUrl.find("appid=3116") != std::string::npos);
    assert(capturedUrl.find("clientver=11440") != std::string::npos);
    assert(capturedBody.find("\"token\":\"tok\"") != std::string::npos);

    {
      std::map<std::string, std::string> query;
      const auto qpos = capturedUrl.find('?');
      assert(qpos != std::string::npos);
      std::istringstream pairs(capturedUrl.substr(qpos + 1));
      std::string pair;
      while (std::getline(pairs, pair, '&')) {
        const auto eq = pair.find('=');
        if (eq == std::string::npos) continue;
        query[pair.substr(0, eq)] = pair.substr(eq + 1);
      }
      const std::string signature = query.at("signature");
      std::unordered_map<std::string, std::string> signedParams;
      for (const auto& [k, v] : query) {
        if (k != "signature") signedParams[k] = v;
      }
      assert(echo::core::SignatureAndroidParams(signedParams, capturedBody,
                                                echo::core::KuGouSaltKind::Lite) == signature);
      assert(echo::core::SignatureAndroidParams(signedParams, capturedBody,
                                                echo::core::KuGouSaltKind::Standard) != signature);
    }
    std::cout << "  [ok] /user/playlist routes through Concept appid=3116/clientver=11440/lite-salt" << std::endl;
  }

  // ── Partial injection on the production seams must be refused explicitly,
  // never half-real-network. GetUserVip is GET-only, GetUserPlaylists is
  // POST-only. ──────────────────────────────────────────────────────────────
  std::cout << "[RouteContract] Testing partial transport injection refusal on user routes..." << std::endl;
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

    echo::core::CompatApiHandlers postOnly;
    postOnly.sessionHttpPost = [](const std::string&, const std::string&,
                                  const std::unordered_map<std::string, std::string>&) {
      return echo::core::HttpResult{200, R"({"status":1,"data":{}})", ""};
    };
    echo::core::CompatApi vipApi(db, postOnly);
    auto vipResp = vipApi.Handle("GET", "/user/vip/detail", {}, {}, "");
    assert(vipResp.body.value("status", 1) == 0);
    assert(vipResp.body.value("error_code", "") == "native_vip_no_get_transport");

    echo::core::CompatApiHandlers getOnly;
    getOnly.sessionHttpGet = [](const std::string&,
                                const std::unordered_map<std::string, std::string>&) {
      return echo::core::HttpResult{200, R"({"status":1,"data":{}})", ""};
    };
    echo::core::CompatApi plApi(db, getOnly);
    auto plResp = plApi.Handle("GET", "/user/playlist", {}, {}, "");
    assert(plResp.body.value("status", 1) == 0);
    assert(plResp.body.value("error_code", "") == "native_playlist_no_post_transport");
    std::cout << "  [ok] missing verb on an injected seam is refused, real network never built" << std::endl;
  }

  // Concurrent /user/detail requests for a session that still has no vip_token
  // must share one in-flight lazy refresh. Hold the first injected refresh at
  // the transport boundary while the second request runs; no real account or
  // network is involved. With a registered fixture device RefreshSession may
  // issue Standard + Concept POSTs, so count both as one refresh attempt.
  std::cout << "[RouteContract] Testing concurrent empty-vip detail refresh is claimed once..."
            << std::endl;
  {
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();
    echo::storage::SessionRepository(db).Save([] {
      echo::core::SessionInfo session;
      session.userId = "42";
      session.token = "fixture-token";
      session.t1 = "fixture-t1";
      return session;
    }());
    echo::storage::DeviceRepository devices(db);
    echo::core::DeviceInfo device;
    device.registered = true;
    device.dfid = "abcdefghijklmnopqrstuvwx";
    device.guid = "registered-device-guid";
    device.appid = "3116";
    device.clientver = "11440";
    devices.Save(device);

    std::atomic<int> refreshPosts{0};
    std::atomic<int> detailPosts{0};
    std::mutex gateMutex;
    std::condition_variable gateChanged;
    bool firstRefreshEntered = false;
    bool releaseFirstRefresh = false;
    echo::core::CompatApiHandlers handlers;
    handlers.sessionHttpPost = [&](const std::string& url, const std::string&,
                                   const std::unordered_map<std::string, std::string>&) {
      if (url.find("login_by_token") != std::string::npos) {
        const int call = ++refreshPosts;
        if (call == 1) {
          std::unique_lock<std::mutex> lock(gateMutex);
          firstRefreshEntered = true;
          gateChanged.notify_all();
          gateChanged.wait_for(lock, std::chrono::seconds(5), [&] {
            return releaseFirstRefresh;
          });
        }
        // Valid refresh response with an empty VIP credential reproduces the
        // persistent condition that used to trigger the same refresh again.
        return echo::core::HttpResult{200,
            R"({"status":1,"data":{"token":"fixture-token-rotated","vip_token":"","vip_type":0,"t1":"fixture-t1-rotated"}})",
            ""};
      }
      if (url.find("get_my_info") != std::string::npos) {
        ++detailPosts;
        return echo::core::HttpResult{200,
            R"({"status":1,"data":{"userid":"42","nickname":"fixture-user","pic":"https://img.example/fixture.png"}})",
            ""};
      }
      return echo::core::HttpResult{500, "", "fixture: unexpected POST"};
    };

    echo::core::CompatApi firstApi(db, handlers);
    echo::core::CompatApi secondApi(db, handlers);
    echo::core::CompatResponse firstResponse;
    std::thread first([&] {
      firstResponse = firstApi.Handle("GET", "/user/detail", {}, {}, "");
    });
    struct RefreshThreadGuard {
      std::mutex& mutex;
      std::condition_variable& changed;
      bool& release;
      std::thread& worker;
      ~RefreshThreadGuard() noexcept {
        {
          std::lock_guard<std::mutex> lock(mutex);
          release = true;
        }
        changed.notify_all();
        if (worker.joinable()) worker.join();
      }
    } cleanup{gateMutex, gateChanged, releaseFirstRefresh, first};

    bool firstEntered = false;
    {
      std::unique_lock<std::mutex> lock(gateMutex);
      firstEntered = gateChanged.wait_for(lock, std::chrono::seconds(5), [&] {
        return firstRefreshEntered;
      });
    }

    echo::core::CompatResponse secondResponse;
    if (firstEntered) {
      secondResponse = secondApi.Handle("GET", "/user/detail", {}, {}, "");
    }
    {
      std::lock_guard<std::mutex> lock(gateMutex);
      releaseFirstRefresh = true;
    }
    gateChanged.notify_all();
    if (first.joinable()) first.join();

    assert(firstEntered);
    assert(firstResponse.httpStatus == 200);
    assert(secondResponse.httpStatus == 200);
    if (detailPosts != 2 || refreshPosts != 2) {
      std::cerr << "[RouteContract] refreshPosts=" << refreshPosts.load()
                << " detailPosts=" << detailPosts.load() << std::endl;
    }
    assert(detailPosts == 2);
    // One RefreshSession attempt can make two login_by_token POSTs. The
    // second route must observe the in-flight claim and skip both.
    assert(refreshPosts == 2);
    const auto repeated = secondApi.Handle("GET", "/user/detail", {}, {}, "");
    assert(repeated.httpStatus == 200);
    assert(detailPosts == 3);
    assert(refreshPosts == 2);
    std::cout << "  [ok] concurrent and repeated /user/detail share one empty-vip refresh"
              << std::endl;
  }

  // A throwing injected refresh is still a completed attempt for cooldown
  // purposes. The route's noexcept finish guard must persist the release
  // before the transport exception propagates to this direct contract caller.
  std::cout << "[RouteContract] Testing lazy refresh exception releases its claim..."
            << std::endl;
  {
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();
    echo::core::SessionInfo session;
    session.userId = "42";
    session.token = "fixture-token";
    echo::storage::SessionRepository repo(db);
    const auto generation = repo.Save(session);
    echo::storage::DeviceRepository devices(db);
    echo::core::DeviceInfo device;
    device.registered = true;
    device.dfid = "abcdefghijklmnopqrstuvwx";
    device.guid = "registered-device-guid";
    device.appid = "3116";
    device.clientver = "11440";
    devices.Save(device);

    int refreshPosts = 0;
    echo::core::CompatApiHandlers handlers;
    handlers.sessionHttpPost = [&](const std::string& url, const std::string&,
                                   const std::unordered_map<std::string, std::string>&) {
      if (url.find("login_by_token") != std::string::npos) {
        ++refreshPosts;
        throw std::runtime_error("fixture refresh exception");
      }
      return echo::core::HttpResult{200,
          R"({"status":1,"data":{"userid":"42"}})", ""};
    };

    echo::core::CompatApi api(db, handlers);
    bool threw = false;
    try {
      (void)api.Handle("GET", "/user/detail", {}, {}, "");
    } catch (const std::runtime_error&) {
      threw = true;
    }
    assert(threw);
    assert(refreshPosts == 1);
    assert(!repo.TryClaimLazyVipRefresh(
        generation, echo::storage::SessionRepository::CurrentUnixTimeMs(),
        echo::storage::SessionRepository::CurrentProcessEpoch()));
    const auto payload = db.GetJson("session.info");
    assert(payload && payload->contains("lazy_vip_refresh"));
    assert((*payload)["lazy_vip_refresh"].value("in_flight", true) == false);
    std::cout << "  [ok] exception propagates after the claim is safely cooled down"
              << std::endl;
  }

  std::cout << "[RouteContract] All tests passed!" << std::endl;
  return 0;
}
