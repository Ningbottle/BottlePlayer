// EchoSessionChainContractTest (B1) — credential chain, end to end and offline.
//
// What this pins, in order:
//   * a real /login/qr/check route call performs the dual-family refresh,
//     persists the rotated credential bundle and strips credentials from the
//     payload the WebView receives;
//   * the session really lands encrypted (CryptProtectData) on disk and
//     survives a database close/reopen;
//   * the reopened session is what the real /song/url route sends upstream
//     (current token, viptoken, vip type, dfid/mid);
//   * the /user/detail restore path lazily refreshes a session whose vip_token
//     is missing and the later nickname/avatar sync does not drop the new
//     credentials;
//   * a rejected refresh leaves the stored session untouched;
//   * the injection seam never calls an empty callback and never silently
//     reaches the network: every unmodelled transport call fails loudly.
//
// Failures print a named message and exit non-zero, so RED/GREEN evidence is
// readable instead of being swallowed by abort().

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "echo/core/CompatApi.h"
#include "echo/core/CompatApiUtils.h"
#include "echo/core/CompatRoutes.h"
#include "echo/core/DeviceService.h"
#include "echo/core/Dto.h"
#include "echo/core/HttpClient.h"
#include "echo/core/KuGouProfile.h"
#include "echo/core/LoginService.h"
#include "echo/storage/Database.h"
#include "echo/storage/DeviceRepository.h"
#include <echo/storage/SessionRepository.h>

// CRT assert reporting must go to stderr, never to an interactive dialog:
// a modal box would hang the test under CTest instead of failing it.
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

#define CHECK(cond, msg)                                                     \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "[SessionChain] CHECK FAILED: %s [%s:%d]\n", msg, \
                   __FILE__, __LINE__);                                      \
      std::fflush(stderr);                                                   \
      std::cout.flush();                                                     \
      return 1;                                                              \
    }                                                                        \
  } while (0)

namespace {

using echo::core::CompatApi;
using echo::core::CompatApiHandlers;
using echo::core::DeviceInfo;
using echo::core::DeviceService;
using echo::core::HandleAuthLogout;
using echo::core::HandleLoginQrKey;
using echo::core::HttpResult;
using echo::core::SessionInfo;
using echo::storage::Database;
using echo::storage::DeviceRepository;
using echo::storage::SessionRepository;

std::filesystem::path TestDbPath(const wchar_t* tag) {
  auto path = std::filesystem::temp_directory_path() /
              (std::wstring(L"echomusic-session-chain-") + tag + L".db");
  std::filesystem::remove(path);
  std::filesystem::remove(path.wstring() + L"-wal");
  std::filesystem::remove(path.wstring() + L"-shm");
  return path;
}

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

// Fixture failure used by every transport branch the test does not model, so an
// unmodelled request can never quietly reach the real network.
HttpResult UnmappedTransport() {
  return HttpResult{0, "", "fixture: unmapped transport call"};
}

// Seeds the registered device that /login/qr/check expects. With
// registered=true the route never reaches DeviceRegisterService, which has no
// injection seam of its own.
DeviceInfo SeedRegisteredDevice(Database& db) {
  DeviceRepository repo(db);
  DeviceService service(repo);
  DeviceInfo device = service.EnsureDeviceReady();
  device.dfid = "dfid-fixture-0001";
  device.registered = true;
  repo.Save(device);
  return device;
}

bool HasCredentialKey(const nlohmann::json& value) {
  if (value.is_object()) {
    for (auto it = value.begin(); it != value.end(); ++it) {
      const std::string key = it.key();
      if (key == "token" || key == "vip_token" || key == "viptoken" || key == "t1") {
        return true;
      }
      if (HasCredentialKey(it.value())) return true;
    }
    return false;
  }
  if (value.is_array()) {
    for (const auto& child : value) {
      if (HasCredentialKey(child)) return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  std::cout << "[SessionChain] started" << std::endl;
  // Surface native diagnostics in test output so RED/GREEN evidence carries
  // the underlying VipToken/AuthSession story, not just the CHECK verdicts.
  echo::diagnostics::SetLogCallback(
      [](int, const char* tag, const char* message, void*) {
        std::fprintf(stderr, "[echo][%s] %s\n", tag, message);
      },
      nullptr);
#if defined(_MSC_VER)
  // Without this an abort() (uncaught exception, CRT assert) opens a modal
  // dialog and hangs the process instead of failing the test.
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  _set_error_mode(_OUT_TO_STDERR);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

  // ── A. fresh QR login -> encrypted store -> reopen -> real /song/url ───
  {
    std::cout << "[SessionChain] A: QR login -> encrypted store -> reopen -> song url"
              << std::endl;
    const auto path = TestDbPath(L"qr");
    Database db;
    db.Open(path);
    db.Initialize();
    const DeviceInfo device = SeedRegisteredDevice(db);

    int refreshCalls = 0;
    int songPostCalls = 0;
    int songGetCalls = 0;
    std::vector<std::string> songPostBodies;
    std::string songGetUrl;

    CompatApiHandlers handlers;
    handlers.loginQrCheck = [](const DeviceInfo&, std::string) {
      // The QR poll response carries no vip_token; the route must mint one
      // through the dual-family refresh below. The login payload lives under
      // "data" exactly like the upstream /v2/get_userinfo_qrcode response.
      return nlohmann::json{
          {"status", 4},
          {"data", {{"status", 4},
                    {"userid", "42"},
                    {"token", "login-token"},
                    {"nickname", "fixture-user"},
                    {"pic", "https://img.example/a.png"}}}};
    };
    handlers.sessionHttpPost =
        [&](const std::string& url, const std::string& body,
            const std::unordered_map<std::string, std::string>&) -> HttpResult {
      if (Contains(url, "login_by_token")) {
        ++refreshCalls;
        if (refreshCalls == 1) {
          // Standard family rotates the ordinary token but grants no VIP.
          return HttpResult{200,
              R"({"status":1,"data":{"token":"std-token","vip_token":"","vip_type":0,"t1":"std-t1"}})",
              ""};
        }
        // Concept family rotates again and issues the VIP credential.
        return HttpResult{200,
            R"({"status":1,"data":{"token":"concept-token","vip_token":"vip-concept","vip_type":1,"t1":"concept-t1"}})",
            ""};
      }
      if (Contains(url, "priv_url")) {
        ++songPostCalls;
        songPostBodies.push_back(body);
        if (songPostCalls == 1) {
          return HttpResult{200,
              R"({"status":1,"data":[{"url":"https://cdn.example/full/song.flac","info":{"bitrate":320,"extname":"flac","timeLength":269000}}]})",
              ""};
        }
        // Second call: v6 rejects, forcing the v5 GET fallback.
        return HttpResult{200, R"({"status":0,"errcode":20018})", ""};
      }
      std::cerr << "[SessionChain] unmodelled POST " << url << std::endl;
      return UnmappedTransport();
    };
    handlers.sessionHttpGet =
        [&](const std::string& url,
            const std::unordered_map<std::string, std::string>&) -> HttpResult {
      ++songGetCalls;
      songGetUrl = url;
      return HttpResult{200,
          R"({"status":1,"url":["https://cdn.example/full/v5.flac"],"timeLength":269000})",
          ""};
    };

    CompatApi api(db, handlers);
    const auto loginResp = api.Handle("GET", "/login/qr/check", {{"key", "k1"}}, {}, "");
    CHECK(loginResp.httpStatus == 200, "qr check http status");
    CHECK(loginResp.body.value("status", 0) == 4, "qr check login status 4");
    // The WebView must never receive credentials it is not allowed to hold.
    CHECK(!HasCredentialKey(loginResp.body), "qr response leaked a credential key");
    CHECK(refreshCalls == 2, "qr route did not run both refresh families");

    // Encrypted at rest: the raw row must be a protected blob, not plaintext.
    const auto raw = db.GetJson("session.info");
    CHECK(raw.has_value(), "session.info row missing");
    CHECK(raw->value("version", 0) == 1, "session.info version");
    CHECK(raw->contains("protected_data") && (*raw)["protected_data"].is_string(),
          "session.info is not a protected payload");
    const auto rawDump = raw->dump();
    CHECK(!Contains(rawDump, "concept-token"), "rotated token stored in plaintext");
    CHECK(!Contains(rawDump, "vip-concept"), "vip token stored in plaintext");
    CHECK(!Contains(rawDump, "concept-t1"), "t1 stored in plaintext");

    db.Close();
    db.Open(path);  // reopen forces a real unprotect + parse of the stored blob
    db.Initialize();

    SessionRepository sessionRepo(db);
    const auto session = sessionRepo.Load();
    CHECK(session.has_value(), "session not readable after reopen");
    CHECK(session->userId == "42", "userId after reopen");
    CHECK(session->token == "concept-token", "rotated token after reopen");
    CHECK(session->vipToken == "vip-concept", "rotated vip token after reopen");
    CHECK(session->vipType == 1, "rotated vip type after reopen");
    CHECK(session->t1 == "concept-t1", "rotated t1 after reopen");
    CHECK(session->nickname == "fixture-user", "nickname after reopen");
    CHECK(!device.dfid.empty(), "fixture device dfid");

    // The real /song/url route must send exactly the persisted credentials.
    const auto song =
        api.Handle("GET", "/song/url", {{"hash", "ABC123"}, {"quality", "320"}}, {}, "");
    CHECK(song.body.value("status", 0) == 1, "song url v6 status");
    CHECK(song.body.value("delivery", "") == "full", "song url v6 delivery");
    CHECK(song.body["data"].value("delivery", "") == "full", "song url data delivery");
    CHECK(songPostBodies.size() == 1, "v6 POST call count");
    CHECK(songGetCalls == 0, "v5 GET ran although v6 succeeded");
    if (!songPostBodies.empty()) {
      const auto& v6Body = songPostBodies.front();
      CHECK(Contains(v6Body, R"("viptoken":"vip-concept")"), "v6 body missing vip token");
      CHECK(Contains(v6Body, R"("vip":1)"), "v6 body missing vip type");
      CHECK(Contains(v6Body, R"("token":"concept-token")"), "v6 body missing session token");
      CHECK(Contains(v6Body, "abc123"), "v6 body missing normalized hash");
    }

    // Second call falls back to v5: the GET must carry the same session and
    // the registered device fingerprint.
    const auto songFallback =
        api.Handle("GET", "/song/url", {{"hash", "ABC123"}, {"quality", "320"}}, {}, "");
    CHECK(songGetCalls == 1, "v5 GET call count");
    CHECK(Contains(songGetUrl, "userid=42"), "v5 url missing userid");
    CHECK(Contains(songGetUrl, "token=concept-token"), "v5 url missing session token");
    CHECK(Contains(songGetUrl, "dfid=dfid-fixture-0001"), "v5 url missing device dfid");
    CHECK(Contains(songGetUrl, "abc123"), "v5 url missing normalized hash");
    // The offset-free full URL is honestly reported as full.
    CHECK(songFallback.body.value("delivery", "") == "full", "v5 fallback delivery");

    db.Close();
    std::cout << "  [ok] A" << std::endl;
  }

  // ── B. stored session without vip_token -> /user/detail lazy refresh ────
  {
    std::cout << "[SessionChain] B: /user/detail lazy refresh keeps one credential bundle"
              << std::endl;
    const auto path = TestDbPath(L"restore");
    Database db;
    db.Open(path);
    db.Initialize();
    SeedRegisteredDevice(db);
    {
      SessionInfo stale;
      stale.token = "old-token";
      stale.userId = "42";
      stale.t1 = "old-t1";
      stale.nickname = "old-nick";
      stale.pic = "https://img.example/old.png";
      stale.vipToken = "";
      stale.vipType = 0;
      SessionRepository(db).Save(stale);
    }

    int refreshCalls = 0;
    int detailCalls = 0;
    CompatApiHandlers handlers;
    // No handlers.userDetail: the route must take its real code path.
    handlers.sessionHttpPost =
        [&](const std::string& url, const std::string&,
            const std::unordered_map<std::string, std::string>&) -> HttpResult {
      if (Contains(url, "login_by_token")) {
        ++refreshCalls;
        if (refreshCalls == 1) {
          return HttpResult{200,
              R"({"status":1,"data":{"token":"restore-token","vip_token":"","vip_type":0,"t1":"restore-t1"}})",
              ""};
        }
        return HttpResult{200,
            R"({"status":1,"data":{"token":"restore-token-2","vip_token":"vip-restored","vip_type":2,"t1":"restore-t1-2"}})",
            ""};
      }
      if (Contains(url, "get_my_info")) {
        ++detailCalls;
        return HttpResult{200,
            R"({"status":1,"data":{"nickname":"new-nick","pic":"https://img.example/new.png"}})",
            ""};
      }
      std::cerr << "[SessionChain] unmodelled POST " << url << std::endl;
      return UnmappedTransport();
    };

    CompatApi api(db, handlers);
    const auto detail = api.Handle("GET", "/user/detail", {}, {}, "");
    CHECK(detail.httpStatus == 200, "user detail http status");
    CHECK(detail.body.value("status", 0) == 1, "user detail status");
    CHECK(refreshCalls == 2, "user detail did not run both refresh families");
    CHECK(detailCalls == 1, "user detail upstream call count");
    CHECK(!HasCredentialKey(detail.body), "user detail leaked a credential key");

    const auto session = SessionRepository(db).Load();
    CHECK(session.has_value(), "session missing after lazy refresh");
    // The refresh rotated the whole bundle...
    CHECK(session->token == "restore-token-2", "token after lazy refresh");
    CHECK(session->vipToken == "vip-restored", "vip token after lazy refresh");
    CHECK(session->vipType == 2, "vip type after lazy refresh");
    CHECK(session->t1 == "restore-t1-2", "t1 after lazy refresh");
    // ...and the later nickname/avatar sync must not drop it.
    CHECK(session->nickname == "new-nick", "nickname synced");
    CHECK(session->pic == "https://img.example/new.png", "pic synced");
    CHECK(session->userId == "42", "userId preserved");

    db.Close();
    db.Open(path);
    db.Initialize();
    const auto reopened = SessionRepository(db).Load();
    CHECK(reopened.has_value(), "session missing after reopen");
    CHECK(reopened->vipToken == "vip-restored", "vip token after reopen");
    CHECK(reopened->token == "restore-token-2", "token after reopen");
    db.Close();
    std::cout << "  [ok] B" << std::endl;
  }

  // ── C. rejected refresh keeps the stored session untouched ──────────────
  {
    std::cout << "[SessionChain] C: rejected refresh leaves the session intact"
              << std::endl;
    const auto path = TestDbPath(L"reject");
    Database db;
    db.Open(path);
    db.Initialize();
    SeedRegisteredDevice(db);
    {
      SessionInfo existing;
      existing.token = "keep-token";
      existing.userId = "42";
      existing.t1 = "keep-t1";
      existing.nickname = "keep-nick";
      SessionRepository(db).Save(existing);
    }

    int refreshCalls = 0;
    int detailCalls = 0;
    CompatApiHandlers handlers;
    handlers.sessionHttpPost =
        [&](const std::string& url, const std::string&,
            const std::unordered_map<std::string, std::string>&) -> HttpResult {
      // The same transport serves the refresh and /user/detail itself, so the
      // two must be counted apart or the assertion below can never mean "both
      // families were tried".
      if (Contains(url, "login_by_token")) ++refreshCalls;
      if (Contains(url, "get_my_info")) ++detailCalls;
      return HttpResult{200, R"({"status":0,"error_code":20018})", ""};
    };

    CompatApi api(db, handlers);
    const auto detail = api.Handle("GET", "/user/detail", {}, {}, "");
    CHECK(detail.httpStatus == 200, "reject case http status");
    CHECK(refreshCalls == 2, "both families should be attempted before giving up");
    CHECK(detailCalls == 1, "user detail upstream call count");

    const auto session = SessionRepository(db).Load();
    CHECK(session.has_value(), "session missing after rejected refresh");
    CHECK(session->token == "keep-token", "token changed by a rejected refresh");
    CHECK(session->t1 == "keep-t1", "t1 changed by a rejected refresh");
    CHECK(session->userId == "42", "userId changed by a rejected refresh");
    CHECK(session->vipToken.empty(), "rejected refresh invented a vip token");
    CHECK(session->vipType == 0, "rejected refresh invented a vip type");
    db.Close();
    std::cout << "  [ok] C" << std::endl;
  }

  // ── D. injection seam: an absent verb fails, it never calls null ───────
  {
    std::cout << "[SessionChain] D: partial transport injection fails explicitly"
              << std::endl;
    const auto path = TestDbPath(L"seam");
    Database db;
    db.Open(path);
    db.Initialize();

    // Only POST injected. The v6 endpoint rejects, so the service would need
    // the (absent) GET for the v5 fallback.
    {
      CompatApiHandlers postOnly;
      postOnly.sessionHttpPost =
          [](const std::string&, const std::string&,
             const std::unordered_map<std::string, std::string>&) -> HttpResult {
        return HttpResult{200, R"({"status":0,"errcode":20018})", ""};
      };
      CompatApi api(db, postOnly);
      const auto resp = api.Handle("GET", "/song/url", {{"hash", "ABC123"}}, {}, "");
      CHECK(resp.httpStatus == 200, "post-only song url http status");
      CHECK(resp.body.value("status", 0) == 0, "post-only song url must fail explicitly");
      CHECK(!resp.body.value("error", std::string{}).empty(),
            "post-only song url must report an error");
    }

    // Only GET injected. v6 must be skipped rather than run through an empty
    // POST callback.
    {
      int getCalls = 0;
      CompatApiHandlers getOnly;
      getOnly.sessionHttpGet =
          [&](const std::string&,
              const std::unordered_map<std::string, std::string>&) -> HttpResult {
        ++getCalls;
        return HttpResult{200,
            R"({"status":1,"url":["https://cdn.example/full/v5.flac"],"timeLength":269000})",
            ""};
      };
      CompatApi api(db, getOnly);
      const auto resp = api.Handle("GET", "/song/url", {{"hash", "ABC123"}}, {}, "");
      CHECK(getCalls == 1, "get-only song url should use the injected GET once");
      CHECK(resp.body.value("status", 0) == 1, "get-only song url status");
      CHECK(resp.body.value("delivery", "") == "full", "get-only song url delivery");
    }

    // The QR poll verb is the GET seam. /login/qr/check now accepts an injected
    // GET and is covered end to end by EchoQrTransportContractTest; this pins the
    // service level guard so an absent verb can never invoke an empty
    // std::function, whatever the caller wires.
    {
      echo::core::LoginService svc(echo::core::LoginHttpGet{});  // GET seam absent
      const auto poll = svc.PollQrLogin(DeviceInfo{}, "k");
      CHECK(poll.value("status", -1) == 0,
            "poll without a GET verb must not report a login");
      CHECK(!poll.value("error", std::string{}).empty(),
            "poll without a GET verb must fail loudly, not crash");
    }

    db.Close();
    std::cout << "  [ok] D" << std::endl;
  }

  // ── E. /user/detail partial injection: a missing POST never falls back to
  //      the default real transport ─────────────────────────────────────────
  //
  // /user/detail is POST-only (get_my_info) and its route also lazily refreshes
  // the session through the POST-only LoginService::RefreshSession. Injecting
  // ONLY the GET seam must therefore (a) not reach the real network through a
  // default-constructed transport, and (b) fail explicitly rather than degrade
  // to the local-profile fallback. The refusal branch the route takes is
  // reachable only when a seam is injected and the POST is absent, so its
  // error_code is a provable marker that no transport was built, let alone
  // called. (When neither callback is injected the route keeps the production
  // default — real network — which is not exercised offline.)
  {
    std::cout << "[SessionChain] E: /user/detail partial injection is isolated"
              << std::endl;

    // E1: the stored session has no vip_token, so the lazy-refresh branch is
    // entered first; then the detail request. Both must refuse without a POST.
    {
      const auto path = TestDbPath(L"detail-getonly-refresh");
      Database db;
      db.Open(path);
      db.Initialize();
      SeedRegisteredDevice(db);
      {
        SessionInfo stored;
        stored.token = "seed-token";
        stored.userId = "42";
        stored.t1 = "seed-t1";
        stored.nickname = "seed-nick";
        stored.pic = "https://img.example/seed.png";
        stored.vipToken = "";  // forces the lazy-refresh branch
        stored.vipType = 0;
        SessionRepository(db).Save(stored);
      }

      int getCalls = 0;
      CompatApiHandlers handlers;
      // ONLY the GET seam. No sessionHttpPost, and no handlers.userDetail
      // short-circuit — the route must run its real code path.
      handlers.sessionHttpGet =
          [&](const std::string&,
              const std::unordered_map<std::string, std::string>&) -> HttpResult {
        ++getCalls;
        return HttpResult{200,
            R"({"status":1,"url":["https://cdn.example/full/x.flac"],"timeLength":269000})",
            ""};
      };

      CompatApi api(db, handlers);
      const auto resp = api.Handle("GET", "/user/detail", {}, {}, "");
      // Diagnostic first: a RED run must show what the route actually returned,
      // not just which assertion tripped.
      std::fprintf(stderr, "[SessionChain] E1 response: %s\n", resp.body.dump().c_str());
      CHECK(resp.httpStatus == 200, "E1 http status");
      // Explicit failure, not a silent local-profile success.
      CHECK(resp.body.value("status", 0) == 0, "E1 must fail explicitly");
      CHECK(resp.body.value("error_code", std::string{}) == "native_detail_no_post_transport",
            "E1 must name the missing POST transport");
      CHECK(resp.body.value("data", nlohmann::json(nullptr)).is_null(),
            "E1 must not fabricate profile data");
      // The GET seam is not a substitute for the POST-only detail endpoint.
      CHECK(getCalls == 0, "E1 detail path must not use the GET seam");

      // The refused branch must not have rotated or invented any credential.
      const auto session = SessionRepository(db).Load();
      CHECK(session.has_value(), "E1 session missing");
      CHECK(session->token == "seed-token", "E1 token changed");
      CHECK(session->t1 == "seed-t1", "E1 t1 changed");
      CHECK(session->vipToken.empty(), "E1 invented a vip token");
      CHECK(session->vipType == 0, "E1 invented a vip type");
      CHECK(session->userId == "42", "E1 userId changed");
      CHECK(session->nickname == "seed-nick", "E1 nickname changed");
      db.Close();
      std::cout << "  [ok] E1 (lazy-refresh entry, GET-only)" << std::endl;
    }

    // E2: the session already carries a vip_token, so the lazy refresh is
    // skipped and the detail POST is the only transport the route needs. Same
    // refusal, and the credential is preserved.
    {
      const auto path = TestDbPath(L"detail-getonly-direct");
      Database db;
      db.Open(path);
      db.Initialize();
      SeedRegisteredDevice(db);
      {
        SessionInfo stored;
        stored.token = "seed-token";
        stored.userId = "42";
        stored.t1 = "seed-t1";
        stored.vipToken = "seed-vip";  // skips the lazy refresh
        stored.vipType = 1;
        SessionRepository(db).Save(stored);
      }

      int getCalls = 0;
      CompatApiHandlers handlers;
      handlers.sessionHttpGet =
          [&](const std::string&,
              const std::unordered_map<std::string, std::string>&) -> HttpResult {
        ++getCalls;
        return HttpResult{200, R"({"status":1})", ""};
      };

      CompatApi api(db, handlers);
      const auto resp = api.Handle("GET", "/user/detail", {}, {}, "");
      std::fprintf(stderr, "[SessionChain] E2 response: %s\n", resp.body.dump().c_str());
      CHECK(resp.httpStatus == 200, "E2 http status");
      CHECK(resp.body.value("status", 0) == 0, "E2 must fail explicitly");
      CHECK(resp.body.value("error_code", std::string{}) == "native_detail_no_post_transport",
            "E2 must name the missing POST transport");
      CHECK(getCalls == 0, "E2 detail path must not use the GET seam");
      const auto session = SessionRepository(db).Load();
      CHECK(session.has_value(), "E2 session missing");
      CHECK(session->token == "seed-token", "E2 token changed");
      CHECK(session->vipToken == "seed-vip", "E2 vip token changed");
      CHECK(session->vipType == 1, "E2 vip type changed");
      db.Close();
      std::cout << "  [ok] E2 (detail only, GET-only)" << std::endl;
    }

    // ── F. device recovery matrix (20017 investigation) ────────────────────
    //
    // DeviceRepository::Clear() persists `{}` and DeviceRepository::Load()
    // happily hands that empty object back as if a device existed. The QR
    // chain then runs on an identity with no guid/dfid/mid/uuid (the 22:45
    // log shows exactly that). This matrix pins the recovery contract:
    //   * missing record        -> fresh identity, persisted
    //   * `{}` record (poison)  -> fresh identity, persisted  (RED today)
    //   * legacy uuid-only      -> guid backfilled, re-registration flagged
    //   * legal unregistered    -> guid kept, no regeneration, no dfid minted
    //   * complete registered   -> untouched
    // and every recovered identity must be stable across repeated reads and
    // a database close/reopen.
    {
      std::cout << "[SessionChain] F: device recovery matrix" << std::endl;

      // F1. missing record -> fresh identity, stable on re-read.
      {
        const auto path = TestDbPath(L"dev-missing");
        Database db;
        db.Open(path);
        db.Initialize();
        DeviceRepository repo(db);
        DeviceService service(repo);
        const auto first = service.EnsureDeviceReady();
        CHECK(!first.guid.empty(), "F1 missing record must mint a guid");
        CHECK(first.dfid.empty() || first.dfid == "-", "F1 fresh dfid stays placeholder");
        CHECK(!first.registered, "F1 fresh device is unregistered");
        const auto second = service.EnsureDeviceReady();
        CHECK(second.guid == first.guid, "F1 guid stable on immediate re-read");
        db.Close();
        // Reopen: identity must survive.
        Database db2;
        db2.Open(path);
        db2.Initialize();
        DeviceRepository repo2(db2);
        DeviceService service2(repo2);
        const auto reopened = service2.EnsureDeviceReady();
        CHECK(reopened.guid == first.guid, "F1 guid stable across db reopen");
        db2.Close();
      }

      // F2. `{}` record (what Clear() leaves behind) -> fresh identity.
      {
        const auto path = TestDbPath(L"dev-empty");
        Database db;
        db.Open(path);
        db.Initialize();
        DeviceRepository(db).Save(DeviceInfo{});  // the poisoned tombstone
        DeviceRepository repo(db);
        DeviceService service(repo);
        const auto recovered = service.EnsureDeviceReady();
        CHECK(!recovered.guid.empty(), "F2 empty record must be treated as missing");
        CHECK(!recovered.registered, "F2 recovered identity is unregistered");
        const auto again = service.EnsureDeviceReady();
        CHECK(again.guid == recovered.guid, "F2 guid stable on immediate re-read");
        db.Close();
        Database db2;
        db2.Open(path);
        db2.Initialize();
        DeviceRepository repo2(db2);
        DeviceService service2(repo2);
        const auto reopened = service2.EnsureDeviceReady();
        CHECK(reopened.guid == recovered.guid, "F2 guid stable across db reopen");
        db2.Close();
      }

      // F2b. Real Clear() (the exact tombstone /auth/logout writes) -> fresh
      // identity. F2 simulates the tombstone via Save(DeviceInfo{}); this
      // exercises the repository Clear() path itself.
      {
        const auto path = TestDbPath(L"dev-clear");
        Database db;
        db.Open(path);
        db.Initialize();
        DeviceInfo full;
        full.dfid = "cleardfid0000000000000000001";
        full.mid = "123456789012345678901234567890123456789";
        full.guid = "clear-guid-1234-5678-901234567890";
        full.uuid = "clear-uuid";
        full.registered = true;
        const auto profile = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Concept);
        full.appid = profile.appid;
        full.clientver = profile.clientver;
        DeviceRepository(db).Save(full);

        DeviceRepository(db).Clear();
        DeviceRepository repo(db);
        DeviceService service(repo);
        const auto recovered = service.EnsureDeviceReady();
        CHECK(!recovered.guid.empty() && recovered.guid != full.guid,
              "F2b real Clear() must be treated as missing -> fresh guid");
        CHECK(!recovered.registered, "F2b recovered identity is unregistered");
        const auto again = service.EnsureDeviceReady();
        CHECK(again.guid == recovered.guid, "F2b fresh guid stable on re-read");
        db.Close();
        Database db2;
        db2.Open(path);
        db2.Initialize();
        DeviceRepository repo2(db2);
        DeviceService service2(repo2);
        const auto reopened = service2.EnsureDeviceReady();
        CHECK(reopened.guid == recovered.guid, "F2b fresh guid stable across db reopen");
        db2.Close();
      }

      // F2c. logout -> QR key chain: /auth/logout wipes session+device, then
      // /login/qr/key provisions a fresh unregistered device for the next QR
      // login. Proves the recovery loop end to end through the real routes.
      {
        const auto path = TestDbPath(L"dev-logout-qr");
        Database db;
        db.Open(path);
        db.Initialize();
        DeviceInfo seeded;
        seeded.dfid = "logoutdfid000000000000000001";
        seeded.guid = "logout-guid-1234-5678-901234567890";
        seeded.registered = true;
        const auto profile = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Concept);
        seeded.appid = profile.appid;
        seeded.clientver = profile.clientver;
        DeviceRepository(db).Save(seeded);
        SessionInfo seededSession;
        seededSession.userId = "42";
        seededSession.token = "old-token";
        SessionRepository(db).Save(seededSession);

        const auto logout = HandleAuthLogout(db);
        CHECK(logout.httpStatus == 200, "F2c logout http status");
        CHECK(!SessionRepository(db).Load().has_value(), "F2c session cleared");
        const auto wiped = DeviceRepository(db).Load();
        CHECK(!wiped.has_value() || (wiped->dfid.empty() && wiped->guid.empty()),
              "F2c device wiped to an empty record");

        int qrKeyCalls = 0;
        DeviceInfo capturedQrDevice;
        const auto resp = HandleLoginQrKey(
            db, [&](const DeviceInfo& device) {
              ++qrKeyCalls;
              capturedQrDevice = device;
              return nlohmann::json{
                  {"status", 1},
                  {"data", {{"qrcode", "fixture-qr"}, {"key", "fixture-key"}}}};
            });
        CHECK(resp.httpStatus == 200, "F2c qr key http status");
        CHECK(qrKeyCalls == 1, "F2c qr key handler called exactly once");
        CHECK(capturedQrDevice.dfid != "logoutdfid000000000000000001",
              "F2c QR key must not reuse the wiped dfid");
        const auto fresh = DeviceRepository(db).Load();
        CHECK(fresh.has_value() && !fresh->guid.empty() &&
                  fresh->guid != "logout-guid-1234-5678-901234567890",
              "F2c fresh device provisioned for the next QR login");
        CHECK(fresh.has_value() && !fresh->registered, "F2c fresh device is unregistered");
        db.Close();
      }

      // F3. legacy uuid-only record -> guid backfilled, re-registration flagged.
      {
        const auto path = TestDbPath(L"dev-legacy");
        Database db;
        db.Open(path);
        db.Initialize();
        DeviceInfo legacy;
        legacy.uuid = "legacyuuid0123456789legacyuuid0123456789";
        legacy.dfid = "legacydfid0000000000001";
        legacy.registered = true;
        DeviceRepository(db).Save(legacy);
        DeviceRepository repo(db);
        DeviceService service(repo);
        const auto migrated = service.EnsureDeviceReady();
        CHECK(migrated.guid == legacy.uuid, "F3 guid backfilled from uuid");
        CHECK(!migrated.registered, "F3 migration requires re-registration");
        const auto again = service.EnsureDeviceReady();
        CHECK(again.guid == legacy.uuid, "F3 backfilled guid stable on re-read");
        db.Close();
      }

      // F4. legal unregistered device (guid set, dfid placeholder) -> untouched.
      {
        const auto path = TestDbPath(L"dev-legal");
        Database db;
        db.Open(path);
        db.Initialize();
        DeviceInfo legal;
        legal.guid = "legal-guid-1234-5678-901234567890";
        legal.dfid = "-";
        legal.registered = false;
        DeviceRepository(db).Save(legal);
        DeviceRepository repo(db);
        DeviceService service(repo);
        const auto kept = service.EnsureDeviceReady();
        CHECK(kept.guid == legal.guid, "F4 legal unregistered guid must not regenerate");
        CHECK(kept.dfid.empty() || kept.dfid == "-", "F4 no dfid minted for unregistered");
        CHECK(!kept.registered, "F4 registered flag untouched");
        db.Close();
      }

      // F5. complete registered device -> untouched.
      {
        const auto path = TestDbPath(L"dev-full");
        Database db;
        db.Open(path);
        db.Initialize();
        DeviceInfo full;
        full.dfid = "fulldfid00000000000000001";
        full.mid = "1234567890123456789012345678901234567890";
        full.uuid = "fulluuid00000000000000000000000000001";
        full.guid = "full-guid-1234-5678-901234567890";
        full.registered = true;
        // A genuinely complete record always carries the profile identity;
        // NormalizeDeviceInfo flips registered=false on an appid mismatch.
        const auto profile = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Concept);
        full.appid = profile.appid;
        full.clientver = profile.clientver;
        DeviceRepository(db).Save(full);
        DeviceRepository repo(db);
        DeviceService service(repo);
        const auto kept = service.EnsureDeviceReady();
        CHECK(kept.guid == full.guid, "F5 registered device guid untouched");
        CHECK(kept.dfid == full.dfid, "F5 registered dfid untouched");
        CHECK(kept.mid == full.mid, "F5 registered mid untouched");
        CHECK(kept.registered, "F5 registered flag kept");
        db.Close();
      }

      std::cout << "  [ok] F (device recovery matrix)" << std::endl;
    }

    // ── G. QR route: register seam + ordering branches (20017) ─────────────
    //
    // The QR success path must register an unregistered device BEFORE the
    // credential refresh: LoginService::RefreshSession only reaches the
    // Concept family when the device object it receives is registered with a
    // real dfid. With the pre-fix ordering (refresh first, register last,
    // into a throwaway copy) the refresh ran on the unregistered device and
    // the Concept family never fired (the 22:45 log shape).
    //
    // familyCalls counts login_by_token attempts: 1 = Standard only,
    // 2 = Standard + Concept (Concept is gated on a registered device).
    {
      std::cout << "[SessionChain] G: QR register seam and ordering" << std::endl;
      using echo::core::DeviceInfo;
      using echo::core::HandleLoginQrCheck;
      using echo::core::QueryMap;

      int familyCalls = 0;
      const auto qrPayload = [](bool withVipToken) {
        nlohmann::json data{{"status", 4},
                            {"userid", "42"},
                            {"token", "login-token"},
                            {"nickname", "fixture-user"}};
        if (withVipToken) data["vip_token"] = "qr-vip";
        return nlohmann::json{{"status", 4}, {"data", data}};
      };
      const auto qrHandler = [&](const DeviceInfo&, std::string) {
        return qrPayload(false);
      };
      auto makeVerbs = [&](bool refreshFails) {
        echo::core::LoginHttpGet get =
            [](const std::string&,
               const std::unordered_map<std::string, std::string>&) -> HttpResult {
          return UnmappedTransport();
        };
        echo::core::LoginHttpPost post =
            // familyCalls by reference (outer block outlives the route call);
            // refreshFails BY VALUE — capturing the factory parameter by
            // reference would dangle the moment makeVerbs returns.
            [&familyCalls, refreshFails](
                const std::string& url, const std::string&,
                const std::unordered_map<std::string, std::string>&) -> HttpResult {
          if (Contains(url, "login_by_token")) {
            ++familyCalls;
            if (refreshFails) {
              return HttpResult{200, R"({"status":0,"error_code":20018})", ""};
            }
            if (familyCalls == 1) {
              return HttpResult{200,
                  R"({"status":1,"data":{"token":"std-token","vip_token":"","vip_type":0,"t1":"std-t1"}})",
                  ""};
            }
            return HttpResult{200,
                R"({"status":1,"data":{"token":"concept-token","vip_token":"vip-concept","vip_type":1,"t1":"concept-t1"}})",
                ""};
          }
          return UnmappedTransport();
        };
        return std::make_pair(get, post);
      };
      int registerOkCalls = 0;
      const auto registerOk = [&](const DeviceInfo&, const std::string&,
                                 const std::string&, std::string*) -> std::string {
        ++registerOkCalls;
        return "qrdfid00000000000000000001";
      };

      // G1. unregistered device + registration succeeds: the refresh must run
      // on the UPDATED device so the Concept family is reached.
      {
        const auto path = TestDbPath(L"qr-g1");
        Database db;
        db.Open(path);
        db.Initialize();
        DeviceInfo seed;
        seed.guid = "g1-guid-1234-5678-901234567890";
        DeviceRepository(db).Save(seed);

        int regCalls = 0;
        familyCalls = 0;
        auto verbs = makeVerbs(false);
        const auto resp = HandleLoginQrCheck(
            db, QueryMap{{"key", "k1"}}, qrHandler, verbs.first, verbs.second,
            [&](const DeviceInfo&, const std::string&, const std::string&,
                std::string*) -> std::string {
              ++regCalls;
              return "qrdfid00000000000000000001";
            });
        CHECK(resp.httpStatus == 200, "G1 http status");
        CHECK(regCalls == 1, "G1 register called once");
        CHECK(familyCalls == 2,
              "G1 refresh must run after registration (Concept family reached)");
        const auto dev = DeviceRepository(db).Load();
        CHECK(dev.has_value() && dev->registered, "G1 device persisted registered");
        CHECK(dev.has_value() && dev->dfid == "qrdfid00000000000000000001",
              "G1 new dfid persisted");
        const auto sess = SessionRepository(db).Load();
        CHECK(sess.has_value() && sess->token == "concept-token", "G1 rotated token saved");
        CHECK(sess.has_value() && sess->vipToken == "vip-concept", "G1 vip token saved");
        db.Close();
      }

      // G2. registration fails: never fake readiness; refresh stays
      // Standard-only (unchanged behavior); QR token still saved.
      {
        const auto path = TestDbPath(L"qr-g2");
        Database db;
        db.Open(path);
        db.Initialize();
        DeviceInfo seed;
        seed.guid = "g2-guid-1234-5678-901234567890";
        DeviceRepository(db).Save(seed);

        int regCalls = 0;
        familyCalls = 0;
        auto verbs = makeVerbs(false);
        HandleLoginQrCheck(
            db, QueryMap{{"key", "k2"}}, qrHandler, verbs.first, verbs.second,
            [&](const DeviceInfo&, const std::string&, const std::string&,
                std::string* error) -> std::string {
              ++regCalls;
              if (error) *error = "fixture: register rejected";
              return "";
            });
        CHECK(regCalls == 1, "G2 register attempted once");
        CHECK(familyCalls == 1, "G2 failed registration keeps refresh Standard-only");
        const auto dev = DeviceRepository(db).Load();
        CHECK(dev.has_value() && !dev->registered, "G2 device not faked as registered");
        const auto sess = SessionRepository(db).Load();
        CHECK(sess.has_value() && sess->token == "std-token", "G2 QR/rotated token still saved");
        CHECK(sess.has_value() && sess->vipToken.empty(), "G2 no vip_token minted");
        db.Close();
      }

      // G3. registration succeeds, refresh fails: session keeps the QR token,
      // device stays registered.
      {
        const auto path = TestDbPath(L"qr-g3");
        Database db;
        db.Open(path);
        db.Initialize();
        DeviceInfo seed;
        seed.guid = "g3-guid-1234-5678-901234567890";
        DeviceRepository(db).Save(seed);

        int regCalls = 0;
        familyCalls = 0;
        auto verbs = makeVerbs(true);
        HandleLoginQrCheck(
            db, QueryMap{{"key", "k3"}}, qrHandler, verbs.first, verbs.second,
            [&](const DeviceInfo&, const std::string&, const std::string&,
                std::string*) -> std::string {
              ++regCalls;
              return "qrdfid00000000000000000001";
            });
        CHECK(regCalls == 1, "G3 register attempted once");
        const auto dev = DeviceRepository(db).Load();
        CHECK(dev.has_value() && dev->registered, "G3 device registered despite refresh failure");
        const auto sess = SessionRepository(db).Load();
        CHECK(sess.has_value() && sess->token == "login-token", "G3 QR token kept on refresh failure");
        CHECK(sess.has_value() && sess->vipToken.empty(), "G3 no vip_token on refresh failure");
        db.Close();
      }

      // G4. QR response carries vip_token: no refresh, but an unregistered
      // device still registers.
      {
        const auto path = TestDbPath(L"qr-g4");
        Database db;
        db.Open(path);
        db.Initialize();
        DeviceInfo seed;
        seed.guid = "g4-guid-1234-5678-901234567890";
        DeviceRepository(db).Save(seed);

        int regCalls = 0;
        familyCalls = 0;
        auto verbs = makeVerbs(false);
        HandleLoginQrCheck(
            db, QueryMap{{"key", "k4"}},
            [&](const DeviceInfo&, std::string) { return qrPayload(true); },
            verbs.first, verbs.second,
            [&](const DeviceInfo&, const std::string&, const std::string&,
                std::string*) -> std::string {
              ++regCalls;
              return "qrdfid00000000000000000001";
            });
        CHECK(regCalls == 1, "G4 register runs for unregistered device");
        CHECK(familyCalls == 0, "G4 QR vip_token must skip the refresh");
        const auto sess = SessionRepository(db).Load();
        CHECK(sess.has_value() && sess->vipToken == "qr-vip", "G4 QR vip_token persisted");
        db.Close();
      }

      // G5. already-registered device: no registration call, both refresh
      // families run.
      {
        const auto path = TestDbPath(L"qr-g5");
        Database db;
        db.Open(path);
        db.Initialize();
        DeviceInfo seed;
        seed.guid = "g5-guid-1234-5678-901234567890";
        seed.dfid = "g5dfid0000000000000000001";
        seed.registered = true;
        const auto profile = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Concept);
        seed.appid = profile.appid;
        seed.clientver = profile.clientver;
        DeviceRepository(db).Save(seed);

        int regCalls = 0;
        familyCalls = 0;
        auto verbs = makeVerbs(false);
        HandleLoginQrCheck(
            db, QueryMap{{"key", "k5"}}, qrHandler, verbs.first, verbs.second, registerOk);
        // G5 修复：注册接缝必须真实计数。此前 registerOk 无计数，断言恒真，
        // 无法证明「已注册设备跳过注册」。
        CHECK(registerOkCalls == 0, "G5 registered device must skip registration (counted seam)");
        CHECK(regCalls == 0, "G5 unused local counter stays zero");
        CHECK(familyCalls == 2, "G5 refresh runs both families on registered device");
        db.Close();
      }

      std::cout << "  [ok] G (QR register seam and ordering)" << std::endl;
    }
  }

  // ── H. B02 late-writer session races ────────────────────────────────────
  // Every scenario models the same interleaving deterministically: the route
  // loads its session snapshot, the transport seam (the "network wait")
  // performs an identity-changing write, and the route's late write must be
  // rejected instead of resurrecting the stale account. Each case is
  // discriminating against the old unconditional full-snapshot SaveSession.
  {
    std::cout << "[SessionChain] H: B02 late-writer races (logout / login B /"
                 " re-login / refresh / profile order)" << std::endl;

    auto makeUser = [](const std::string& tag, const std::string& token,
                       const std::string& nickname, const std::string& vipToken) {
      SessionInfo s;
      s.userId = tag;
      s.token = token;
      s.t1 = "t1-" + tag;
      s.nickname = nickname;
      s.pic = "https://img.example/" + tag + ".png";
      s.vipToken = vipToken;
      s.vipType = vipToken.empty() ? 0 : 1;
      return s;
    };

    // H0. Mechanism evidence (RED for the old route code): an UNCONDITIONAL
    // full-snapshot save after the network wait — exactly what the old
    // routes did via ctx.SaveSession(*session + patch) — resurrects the
    // stale account over the newer one. H1-H6 then pin that the fixed
    // routes never take this path: their conditional commits are rejected.
    {
      const auto path = TestDbPath(L"b02-h0");
      Database db;
      db.Open(path);
      db.Initialize();
      SessionRepository repo(db);
      repo.Save(makeUser("42", "token-a", "user-a", "vip-a"));
      const auto snap = repo.LoadSnapshot();                    // route loads
      repo.Save(makeUser("77", "token-b", "user-b", "vip-b"));  // login B mid-flight
      SessionInfo stale = *snap.session;                        // OLD route pattern:
      stale.nickname = "user-a-late";                           // full stale snapshot
      repo.Save(stale);                                         // written unconditionally
      const auto after = repo.Load();
      CHECK(after.has_value() && after->userId == "42" && after->nickname == "user-a-late",
            "H0 mechanism: unconditional full-snapshot save resurrects the stale account "
            "(this is what H1-H6 prove the routes no longer do)");
      db.Close();
    }

    // H1. Logout during the /user/detail network wait: the route's late
    // profile patch must NOT resurrect the logged-out account.
    {
      const auto path = TestDbPath(L"b02-h1");
      Database db;
      db.Open(path);
      db.Initialize();
      SeedRegisteredDevice(db);
      SessionRepository(db).Save(makeUser("42", "token-a", "user-a", "vip-a"));

      CompatApiHandlers handlers;
      handlers.sessionHttpPost =
          [&](const std::string& url, const std::string&,
              const std::unordered_map<std::string, std::string>&) -> HttpResult {
        if (Contains(url, "get_my_info")) {
          SessionRepository(db).Clear();  // the logout lands mid-flight
          return HttpResult{200,
              R"({"status":1,"data":{"userid":"42","nickname":"user-a-late","pic":"https://img.example/late.png"}})",
              ""};
        }
        std::cerr << "[SessionChain] unmodelled POST " << url << std::endl;
        return UnmappedTransport();
      };
      handlers.sessionHttpGet = [](const std::string&,
                                   const std::unordered_map<std::string, std::string>&) {
        return UnmappedTransport();
      };

      CompatApi api(db, handlers);
      const auto resp = api.Handle("GET", "/user/detail", {}, {}, "");
      CHECK(resp.httpStatus == 200, "H1 route responds");
      const auto after = SessionRepository(db).Load();
      CHECK(!after.has_value(),
            "H1 late profile patch must not resurrect a logged-out session");
      db.Close();
    }

    // H2. Login as account B during the /user/detail network wait: A's late
    // nickname/pic must not overwrite B's identity.
    {
      const auto path = TestDbPath(L"b02-h2");
      Database db;
      db.Open(path);
      db.Initialize();
      SeedRegisteredDevice(db);
      SessionRepository(db).Save(makeUser("42", "token-a", "user-a", "vip-a"));

      CompatApiHandlers handlers;
      handlers.sessionHttpPost =
          [&](const std::string& url, const std::string&,
              const std::unordered_map<std::string, std::string>&) -> HttpResult {
        if (Contains(url, "get_my_info")) {
          SessionRepository(db).Save(makeUser("77", "token-b", "user-b", "vip-b"));
          return HttpResult{200,
              R"({"status":1,"data":{"userid":"42","nickname":"user-a-late","pic":"https://img.example/late.png"}})",
              ""};
        }
        std::cerr << "[SessionChain] unmodelled POST " << url << std::endl;
        return UnmappedTransport();
      };
      handlers.sessionHttpGet = [](const std::string&,
                                   const std::unordered_map<std::string, std::string>&) {
        return UnmappedTransport();
      };

      CompatApi api(db, handlers);
      const auto resp = api.Handle("GET", "/user/detail", {}, {}, "");
      CHECK(resp.httpStatus == 200, "H2 route responds");
      const auto after = SessionRepository(db).Load();
      CHECK(after.has_value(), "H2 session exists after login B");
      CHECK(after->userId == "77", "H2 account B survives the late A response");
      CHECK(after->token == "token-b", "H2 B token untouched");
      CHECK(after->nickname == "user-b", "H2 B nickname not overwritten by A's late detail");
      CHECK(after->pic == "https://img.example/77.png", "H2 B pic not overwritten");
      CHECK(after->vipToken == "vip-b", "H2 B vip token untouched");
      db.Close();
    }

    // H3. Same-account re-login during the wait: the re-issued token must
    // win; the late response must not restore the pre-relogin credential set.
    {
      const auto path = TestDbPath(L"b02-h3");
      Database db;
      db.Open(path);
      db.Initialize();
      SeedRegisteredDevice(db);
      SessionRepository(db).Save(makeUser("42", "token-old", "user-a", "vip-old"));

      CompatApiHandlers handlers;
      handlers.sessionHttpPost =
          [&](const std::string& url, const std::string&,
              const std::unordered_map<std::string, std::string>&) -> HttpResult {
        if (Contains(url, "get_my_info")) {
          // Re-login of the SAME account rotates the credential bundle.
          SessionRepository(db).Save(makeUser("42", "token-new", "user-a-fresh", "vip-new"));
          return HttpResult{200,
              R"({"status":1,"data":{"userid":"42","nickname":"user-a-late","pic":"https://img.example/late.png"}})",
              ""};
        }
        std::cerr << "[SessionChain] unmodelled POST " << url << std::endl;
        return UnmappedTransport();
      };
      handlers.sessionHttpGet = [](const std::string&,
                                   const std::unordered_map<std::string, std::string>&) {
        return UnmappedTransport();
      };

      CompatApi api(db, handlers);
      const auto resp = api.Handle("GET", "/user/detail", {}, {}, "");
      CHECK(resp.httpStatus == 200, "H3 route responds");
      const auto after = SessionRepository(db).Load();
      CHECK(after.has_value(), "H3 session exists after re-login");
      CHECK(after->token == "token-new",
            "H3 re-issued token wins (userId equality alone is not enough)");
      CHECK(after->vipToken == "vip-new", "H3 re-issued vip token wins");
      CHECK(after->nickname == "user-a-fresh",
            "H3 re-login profile wins over the late pre-relogin response");
      db.Close();
    }

    // H4. Lazy vip_token refresh landing after a login B: the refreshed
    // credentials belong to the OLD login and must not be grafted onto B.
    {
      const auto path = TestDbPath(L"b02-h4");
      Database db;
      db.Open(path);
      db.Initialize();
      SeedRegisteredDevice(db);
      // No vip_token: the route lazily refreshes before fetching the detail.
      SessionRepository(db).Save(makeUser("42", "token-a", "user-a", ""));

      CompatApiHandlers handlers;
      handlers.sessionHttpPost =
          [&](const std::string& url, const std::string&,
              const std::unordered_map<std::string, std::string>&) -> HttpResult {
        if (Contains(url, "login_by_token")) {
          // Account B logs in while the refresh is in flight.
          SessionRepository(db).Save(makeUser("77", "token-b", "user-b", "vip-b"));
          // The refresh then completes with OLD-login credentials.
          return HttpResult{200,
              R"({"status":1,"data":{"token":"token-a2","vip_token":"vip-a2","vip_type":1,"t1":"t1-a2"}})",
              ""};
        }
        if (Contains(url, "get_my_info")) {
          return HttpResult{200,
              R"({"status":1,"data":{"userid":"42","nickname":"user-a-late","pic":"https://img.example/late.png"}})",
              ""};
        }
        std::cerr << "[SessionChain] unmodelled POST " << url << std::endl;
        return UnmappedTransport();
      };
      handlers.sessionHttpGet = [](const std::string&,
                                   const std::unordered_map<std::string, std::string>&) {
        return UnmappedTransport();
      };

      CompatApi api(db, handlers);
      const auto resp = api.Handle("GET", "/user/detail", {}, {}, "");
      CHECK(resp.httpStatus == 200, "H4 route responds");
      const auto after = SessionRepository(db).Load();
      CHECK(after.has_value(), "H4 session exists");
      CHECK(after->userId == "77", "H4 account B survives");
      CHECK(after->token == "token-b", "H4 refreshed OLD token must not overwrite B");
      CHECK(after->vipToken == "vip-b", "H4 refreshed OLD vip token must not overwrite B");
      CHECK(after->t1 == "t1-77", "H4 refreshed OLD t1 must not overwrite B");
      CHECK(after->nickname == "user-b", "H4 B nickname intact");
      db.Close();
    }

    // H5. Two same-account profile responses arriving out of order: both are
    // same-generation patches, the final profile must be one of the two
    // upstream states (documented arrival-order semantics), never a mixed
    // identity and never a rejection artifact.
    {
      const auto path = TestDbPath(L"b02-h5");
      Database db;
      db.Open(path);
      db.Initialize();
      SeedRegisteredDevice(db);
      SessionRepository(db).Save(makeUser("42", "token-a", "user-a", "vip-a"));

      CompatApiHandlers handlers;
      handlers.sessionHttpPost =
          [&](const std::string& url, const std::string&,
              const std::unordered_map<std::string, std::string>&) -> HttpResult {
        if (Contains(url, "get_my_info")) {
          return HttpResult{200,
              R"({"status":1,"data":{"userid":"42","nickname":"older-nick","pic":"https://img.example/older.png"}})",
              ""};
        }
        std::cerr << "[SessionChain] unmodelled POST " << url << std::endl;
        return UnmappedTransport();
      };
      handlers.sessionHttpGet = [](const std::string&,
                                   const std::unordered_map<std::string, std::string>&) {
        return UnmappedTransport();
      };

      CompatApi api(db, handlers);
      const auto first = api.Handle("GET", "/user/detail", {}, {}, "");
      CHECK(first.httpStatus == 200, "H5 first request responds");
      const auto mid = SessionRepository(db).Load();
      CHECK(mid.has_value() && mid->nickname == "older-nick",
            "H5 same-generation patch applies");

      // The second (newer upstream state) response arrives; the first one's
      // transport then has nothing further to say.
      handlers.sessionHttpPost =
          [&](const std::string& url, const std::string&,
              const std::unordered_map<std::string, std::string>&) -> HttpResult {
        if (Contains(url, "get_my_info")) {
          return HttpResult{200,
              R"({"status":1,"data":{"userid":"42","nickname":"newer-nick","pic":"https://img.example/newer.png"}})",
              ""};
        }
        std::cerr << "[SessionChain] unmodelled POST " << url << std::endl;
        return UnmappedTransport();
      };
      const auto second = api.Handle("GET", "/user/detail", {}, {}, "");
      CHECK(second.httpStatus == 200, "H5 second request responds");
      const auto after = SessionRepository(db).Load();
      CHECK(after.has_value() && after->userId == "42", "H5 same account preserved");
      const bool oneOfTwo = (after->nickname == "older-nick" || after->nickname == "newer-nick");
      CHECK(oneOfTwo, "H5 final nickname is one of the two upstream states");
      db.Close();
    }

    // H6. The /user/playlist nickname/pic late write is gated the same way.
    {
      const auto path = TestDbPath(L"b02-h6");
      Database db;
      db.Open(path);
      db.Initialize();
      SeedRegisteredDevice(db);
      SessionRepository(db).Save(makeUser("42", "token-a", "user-a", "vip-a"));

      CompatApiHandlers handlers;
      handlers.sessionHttpPost =
          [&](const std::string& url, const std::string&,
              const std::unordered_map<std::string, std::string>&) -> HttpResult {
        if (Contains(url, "get_all_list")) {
          SessionRepository(db).Save(makeUser("77", "token-b", "user-b", "vip-b"));
          return HttpResult{200,
              R"({"status":1,"data":{"info":[{"list_create_username":"user-a-late","create_user_pic":"https://img.example/late.png"}]}})",
              ""};
        }
        std::cerr << "[SessionChain] unmodelled POST " << url << std::endl;
        return UnmappedTransport();
      };
      handlers.sessionHttpGet = [](const std::string&,
                                   const std::unordered_map<std::string, std::string>&) {
        return UnmappedTransport();
      };

      CompatApi api(db, handlers);
      const auto resp = api.Handle("GET", "/user/playlist", {{"page", "1"}}, {}, "");
      CHECK(resp.httpStatus == 200, "H6 route responds");
      const auto after = SessionRepository(db).Load();
      CHECK(after.has_value() && after->userId == "77", "H6 account B survives");
      CHECK(after->nickname == "user-b",
            "H6 playlist late nickname must not overwrite account B");
      CHECK(after->pic == "https://img.example/77.png", "H6 B pic intact");
      db.Close();
    }

    std::cout << "  [ok] H (B02 late-writer races)" << std::endl;
  }

  // ── I. Lazy VIP refresh claims are bounded by generation, process epoch,
  // and a deterministic 30-second cooldown. No sleep or real network is used.
  {
    std::cout << "[SessionChain] I: lazy VIP refresh claim lifecycle" << std::endl;
    const auto path = TestDbPath(L"lazy-vip-refresh");
    Database db;
    db.Open(path);
    db.Initialize();
    SessionRepository repo(db);
    SessionInfo stored;
    stored.userId = "42";
    stored.token = "token-current";
    stored.t1 = "t1-current";
    const auto generation = repo.Save(stored);

    const auto first = repo.TryClaimLazyVipRefresh(generation, 1'000, "process-a");
    CHECK(first.has_value(), "I claim succeeds for valid empty-vip session");
    CHECK(first->session.userId == "42" && first->session.token == "token-current" &&
              first->session.t1 == "t1-current",
          "I ticket carries credentials from the current protected row");
    CHECK(!repo.TryClaimLazyVipRefresh(generation, 1'000, "process-a"),
          "I same process cannot claim while its lease is in flight");

    const auto recovered =
        repo.TryClaimLazyVipRefresh(generation, 1'000, "process-b");
    CHECK(recovered.has_value(), "I a new process epoch reclaims stale in-flight work");
    CHECK(!repo.FinishLazyVipRefresh(*first, 1'100),
          "I old process finish cannot overwrite a recovered claim");

    CHECK(repo.PatchIfGeneration(generation, [](SessionInfo& current) {
            current.nickname = "same-generation-patch";
          }),
          "I same-generation profile patch applies during the claim");
    auto claimedPayload = db.GetJson("session.info");
    CHECK(claimedPayload && claimedPayload->contains("lazy_vip_refresh") &&
              (*claimedPayload)["lazy_vip_refresh"].value("claim_id", "") ==
                  recovered->claimId,
          "I PatchIfGeneration preserves lazy refresh claim metadata");

    CHECK(repo.FinishLazyVipRefresh(*recovered, 2'000),
          "I matching completion records cooldown even when VIP remains empty");
    CHECK(!repo.FinishLazyVipRefresh(*recovered, 2'500),
          "I duplicate finish is a no-op and cannot extend cooldown");
    CHECK(!repo.TryClaimLazyVipRefresh(generation, 31'999, "process-b"),
          "I cooldown suppresses retries until its exact deadline");
    const auto afterCooldown =
        repo.TryClaimLazyVipRefresh(generation, 32'000, "process-b");
    CHECK(afterCooldown.has_value(), "I cooldown expires without sleeping");
    CHECK(repo.FinishLazyVipRefresh(*afterCooldown, 32'000),
          "I expired retry claim completes");

    // Any backwards clock movement makes the stored deadline more than the
    // allowed 30-second window away, so stale metadata cannot suppress forever.
    const auto afterClockRollback =
        repo.TryClaimLazyVipRefresh(generation, 31'999, "process-b");
    CHECK(afterClockRollback.has_value(), "I clock rollback does not extend cooldown");
    CHECK(repo.FinishLazyVipRefresh(*afterClockRollback, 31'999),
          "I rollback claim completes");

    auto malformed = db.GetJson("session.info");
    CHECK(malformed.has_value(), "I protected session payload remains present");
    (*malformed)["lazy_vip_refresh"] = {
        {"epoch", "process-b"},
        {"claim_id", 7},
        {"in_flight", true},
        {"retry_after_ms", "not-a-timestamp"},
    };
    db.SetJson("session.info", *malformed);
    const auto afterMalformed =
        repo.TryClaimLazyVipRefresh(generation, 40'000, "process-b");
    CHECK(afterMalformed.has_value(), "I malformed metadata is treated as reclaimable");
    CHECK(repo.FinishLazyVipRefresh(*afterMalformed, 40'000),
          "I malformed metadata recovery completes");

    const auto beforeSave =
        repo.TryClaimLazyVipRefresh(generation, 70'000, "process-c");
    CHECK(beforeSave.has_value(), "I can claim before authoritative save");
    const auto newGeneration = repo.Save(stored);
    const auto savedPayload = db.GetJson("session.info");
    CHECK(newGeneration > generation && savedPayload &&
              !savedPayload->contains("lazy_vip_refresh"),
          "I authoritative Save advances generation and clears old cooldown");
    CHECK(!repo.FinishLazyVipRefresh(*beforeSave, 70'001),
          "I finish from pre-Save generation is rejected");

    const auto beforeClear =
        repo.TryClaimLazyVipRefresh(newGeneration, 80'000, "process-c");
    CHECK(beforeClear.has_value(), "I can claim before logout");
    repo.Clear();
    const auto clearedPayload = db.GetJson("session.info");
    CHECK(clearedPayload && !clearedPayload->contains("lazy_vip_refresh"),
          "I logout clears lazy refresh metadata");
    CHECK(!repo.FinishLazyVipRefresh(*beforeClear, 80'001),
          "I finish from pre-logout generation is rejected");

    const auto overflowGeneration = repo.Save(stored);
    const auto beforeOverflow =
        repo.TryClaimLazyVipRefresh(overflowGeneration,
                                    std::numeric_limits<std::int64_t>::max() - 1,
                                    "process-overflow");
    CHECK(beforeOverflow.has_value(), "I claim accepts a near-maximum test clock");
    CHECK(repo.FinishLazyVipRefresh(*beforeOverflow,
                                    std::numeric_limits<std::int64_t>::max() - 1),
          "I finish saturates cooldown arithmetic instead of overflowing");
    CHECK(!repo.TryClaimLazyVipRefresh(overflowGeneration,
                                       std::numeric_limits<std::int64_t>::max() - 1,
                                       "process-overflow"),
          "I saturated deadline still suppresses within its safe window");
    CHECK(repo.TryClaimLazyVipRefresh(overflowGeneration,
                                      std::numeric_limits<std::int64_t>::max(),
                                      "process-overflow")
              .has_value(),
          "I saturated cooldown is expired at the maximum timestamp");
    db.Close();
    std::cout << "  [ok] generation / epoch / cooldown / metadata lifecycle" << std::endl;
  }

  std::cout << "[SessionChain] All tests passed!" << std::endl;
  return 0;
}
