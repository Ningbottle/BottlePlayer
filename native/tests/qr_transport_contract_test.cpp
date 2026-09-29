// EchoQrTransportContractTest (B1 review round 2) — /login/qr/check must drive
// its poll through the injected transport seam, and an offline test must never
// reach the real network.
//
// The poll is deliberately NOT stubbed with the high level loginQrCheck handler:
// every block leaves that handler unset, so the route really builds a poller,
// really calls LoginService::PollQrLogin, and the assertions below are written
// against the URLs that poller used. The poll endpoint is
// login-user.kugou.com/v2/get_userinfo_qrcode and carries qrcode=<key>.
//
// Covered:
//   E1 full injection      — poll and both refresh families run on the seam;
//   E2 GET only            — poll works; the unwired refresh must not invent a
//                            VIP credential and must not call out;
//   E3 POST only           — the unwired poll fails loudly and the wired POST is
//                            never used for it;
//   E4 unmodelled upstream — a transport failure fails the route, and no second
//                            request is attempted;
//   E5 both verbs unwired  — the service level guards fire with no transport.
//
// The production default (nothing injected -> real transport) is deliberately
// NOT exercised: doing so would perform a live network request, which is exactly
// what this file exists to prevent. It is evidenced by construction instead —
// no production call site assigns CompatApiHandlers::sessionHttpGet/Post, so the
// default branch is the only production path.

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "echo/core/CompatApi.h"
#include "echo/core/CompatApiUtils.h"
#include "echo/core/DeviceService.h"
#include "echo/core/Dto.h"
#include "echo/core/HttpClient.h"
#include "echo/core/LoginService.h"
#include "echo/storage/Database.h"
#include "echo/storage/DeviceRepository.h"
#include <echo/storage/SessionRepository.h>

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

#define CHECK(cond, msg)                                                    \
  do {                                                                      \
    if (!(cond)) {                                                          \
      std::fprintf(stderr, "[QrTransport] CHECK FAILED: %s [%s:%d]\n", msg, \
                   __FILE__, __LINE__);                                     \
      std::fflush(stderr);                                                  \
      std::cout.flush();                                                    \
      return 1;                                                             \
    }                                                                       \
  } while (0)

namespace {

using echo::core::CompatApi;
using echo::core::CompatApiHandlers;
using echo::core::DeviceInfo;
using echo::core::DeviceService;
using echo::core::HttpResult;
using echo::storage::Database;
using echo::storage::DeviceRepository;
using echo::storage::SessionRepository;

std::filesystem::path TestDbPath(const wchar_t* tag) {
  auto path = std::filesystem::temp_directory_path() /
              (std::wstring(L"echomusic-qr-transport-") + tag + L".db");
  std::filesystem::remove(path);
  std::filesystem::remove(path.wstring() + L"-wal");
  std::filesystem::remove(path.wstring() + L"-shm");
  return path;
}

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

// Every transport branch the block does not model answers with this, so an
// unmapped request can never quietly look like a success.
HttpResult UnmappedTransport() {
  return HttpResult{0, "", "fixture: unmapped transport call"};
}

DeviceInfo SeedRegisteredDevice(Database& db) {
  DeviceRepository repo(db);
  DeviceService service(repo);
  DeviceInfo device = service.EnsureDeviceReady();
  device.dfid = "qr-dfid-fixture-0001";
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

// The upstream QR poll answer for a scanned code.
const char* kPollSignedIn =
    R"({"status":4,"data":{"status":4,"userid":"42","token":"login-token",)"
    R"("nickname":"fixture-qr","pic":"https://img.example/q.png"}})";

}  // namespace

int main() {
  std::cout << "[QrTransport] started" << std::endl;
#if defined(_MSC_VER)
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  _set_error_mode(_OUT_TO_STDERR);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

  // ── E1. full injection: the poll and both refresh families use the seam ──
  {
    std::cout << "[QrTransport] E1: poll and both refresh families run on the seam"
              << std::endl;
    const auto path = TestDbPath(L"full");
    Database db;
    db.Open(path);
    db.Initialize();
    SeedRegisteredDevice(db);

    int pollGetCalls = 0;
    int refreshPostCalls = 0;
    std::string pollUrl;
    std::vector<std::string> unexpected;

    CompatApiHandlers handlers;
    // loginQrCheck is intentionally left unset.
    handlers.sessionHttpGet =
        [&](const std::string& url,
            const std::unordered_map<std::string, std::string>&) -> HttpResult {
      if (!Contains(url, "get_userinfo_qrcode")) {
        unexpected.push_back(url);
        return UnmappedTransport();
      }
      ++pollGetCalls;
      pollUrl = url;
      return HttpResult{200, kPollSignedIn, ""};
    };
    handlers.sessionHttpPost =
        [&](const std::string& url, const std::string&,
            const std::unordered_map<std::string, std::string>&) -> HttpResult {
      if (!Contains(url, "login_by_token")) {
        unexpected.push_back(url);
        return UnmappedTransport();
      }
      ++refreshPostCalls;
      if (refreshPostCalls == 1) {
        return HttpResult{200,
            R"({"status":1,"data":{"token":"std-token","vip_token":"","vip_type":0,"t1":"std-t1"}})",
            ""};
      }
      return HttpResult{200,
          R"({"status":1,"data":{"token":"concept-token","vip_token":"vip-concept","vip_type":1,"t1":"concept-t1"}})",
          ""};
    };

    CompatApi api(db, handlers);
    const auto resp = api.Handle("GET", "/login/qr/check", {{"key", "k1"}}, {}, "");

    CHECK(unexpected.empty(), "a request outside the modelled endpoints was made");
    CHECK(pollGetCalls == 1, "the poll did not run through the injected GET");
    CHECK(Contains(pollUrl, "get_userinfo_qrcode"), "poll hit the wrong endpoint");
    CHECK(Contains(pollUrl, "qrcode=k1"), "poll did not carry the scanned key");
    CHECK(refreshPostCalls == 2, "the refresh did not run both families on the seam");
    CHECK(resp.httpStatus == 200, "qr check http status");
    CHECK(resp.body.value("status", 0) == 4, "qr check login status 4");
    CHECK(!HasCredentialKey(resp.body), "qr response leaked a credential key");

    const auto session = SessionRepository(db).Load();
    CHECK(session.has_value(), "session missing after a signed-in poll");
    CHECK(session->userId == "42", "userId from the poll");
    CHECK(session->token == "concept-token", "rotated token not persisted");
    CHECK(session->vipToken == "vip-concept", "rotated vip token not persisted");
    CHECK(session->vipType == 1, "rotated vip type not persisted");
    CHECK(session->nickname == "fixture-qr", "nickname from the poll");

    db.Close();
    std::cout << "  [ok] E1" << std::endl;
  }

  // ── E2. GET only: the poll runs, the unwired refresh stays off the wire ──
  {
    std::cout << "[QrTransport] E2: GET-only injection keeps the refresh off the wire"
              << std::endl;
    const auto path = TestDbPath(L"getonly");
    Database db;
    db.Open(path);
    db.Initialize();
    SeedRegisteredDevice(db);

    int pollGetCalls = 0;
    std::string pollUrl;
    CompatApiHandlers handlers;
    handlers.sessionHttpGet =
        [&](const std::string& url,
            const std::unordered_map<std::string, std::string>&) -> HttpResult {
      if (!Contains(url, "get_userinfo_qrcode")) return UnmappedTransport();
      ++pollGetCalls;
      pollUrl = url;
      return HttpResult{200, kPollSignedIn, ""};
    };
    // sessionHttpPost deliberately absent.

    CompatApi api(db, handlers);
    const auto resp = api.Handle("GET", "/login/qr/check", {{"key", "k2"}}, {}, "");

    CHECK(pollGetCalls == 1, "the poll did not run through the injected GET");
    CHECK(Contains(pollUrl, "qrcode=k2"), "poll did not carry the scanned key");
    CHECK(resp.httpStatus == 200, "GET-only qr check http status");
    CHECK(resp.body.value("status", 0) == 4, "GET-only qr check login status");
    CHECK(!HasCredentialKey(resp.body), "GET-only response leaked a credential key");

    const auto session = SessionRepository(db).Load();
    CHECK(session.has_value(), "the polled session was not stored");
    CHECK(session->token == "login-token", "polled token not stored");
    // The refresh had no POST verb, so it produced nothing. A missing verb must
    // read as "no entitlement", never as a credential minted out of thin air.
    CHECK(session->vipToken.empty(), "an unwired refresh invented a vip token");
    CHECK(session->vipType == 0, "an unwired refresh invented a vip type");

    db.Close();
    std::cout << "  [ok] E2" << std::endl;
  }

  // ── E3. POST only: the unwired poll fails loudly, POST stays untouched ──
  {
    std::cout << "[QrTransport] E3: POST-only injection fails the poll loudly"
              << std::endl;
    const auto path = TestDbPath(L"postonly");
    Database db;
    db.Open(path);
    db.Initialize();
    SeedRegisteredDevice(db);

    int postCalls = 0;
    CompatApiHandlers handlers;
    handlers.sessionHttpPost =
        [&](const std::string&, const std::string&,
            const std::unordered_map<std::string, std::string>&) -> HttpResult {
      ++postCalls;
      return HttpResult{200, R"({"status":0,"error_code":20018})", ""};
    };
    // sessionHttpGet deliberately absent.

    CompatApi api(db, handlers);
    const auto resp = api.Handle("GET", "/login/qr/check", {{"key", "k3"}}, {}, "");

    // The guard message can only come from LoginService, which proves the real
    // GET client was never consulted.
    CHECK(Contains(resp.body.value("error", std::string{}), "No HTTP GET handler available"),
          "unwired poll did not report the missing GET seam");
    CHECK(resp.body.value("status", 0) != 4, "unwired poll reported a login");
    CHECK(postCalls == 0, "the poll used the POST verb");
    CHECK(!SessionRepository(db).Load().has_value(), "a failed poll stored a session");

    db.Close();
    std::cout << "  [ok] E3" << std::endl;
  }

  // ── E4. unmodelled upstream: the route fails, nothing else is attempted ──
  {
    std::cout << "[QrTransport] E4: a transport failure fails the route" << std::endl;
    const auto path = TestDbPath(L"unmodelled");
    Database db;
    db.Open(path);
    db.Initialize();
    SeedRegisteredDevice(db);

    int getCalls = 0;
    int postCalls = 0;
    std::string requestedUrl;
    CompatApiHandlers handlers;
    handlers.sessionHttpGet =
        [&](const std::string& url,
            const std::unordered_map<std::string, std::string>&) -> HttpResult {
      ++getCalls;
      requestedUrl = url;
      return UnmappedTransport();
    };
    handlers.sessionHttpPost =
        [&](const std::string&, const std::string&,
            const std::unordered_map<std::string, std::string>&) -> HttpResult {
      ++postCalls;
      return UnmappedTransport();
    };

    CompatApi api(db, handlers);
    const auto resp = api.Handle("GET", "/login/qr/check", {{"key", "k4"}}, {}, "");

    CHECK(getCalls == 1, "the poll should attempt the injected GET exactly once");
    CHECK(Contains(requestedUrl, "get_userinfo_qrcode"), "poll hit the wrong endpoint");
    CHECK(postCalls == 0, "a failed poll still triggered a refresh");
    CHECK(resp.body.value("status", 0) != 4, "a failed poll reported a login");
    CHECK(!resp.body.value("error", std::string{}).empty(),
          "a failed poll must report an error");
    CHECK(!SessionRepository(db).Load().has_value(), "a failed poll stored a session");

    db.Close();
    std::cout << "  [ok] E4" << std::endl;
  }

  // ── E5. service level: every verb missing fails, nothing is invoked ─────
  {
    std::cout << "[QrTransport] E5: absent verbs fail at the service level"
              << std::endl;
    echo::core::LoginService bare(echo::core::LoginHttpGet{},
                                 echo::core::LoginHttpPost{});

    const auto poll = bare.PollQrLogin(DeviceInfo{}, "k5");
    CHECK(poll.value("status", -1) == 0, "bare poll must not report a login");
    CHECK(!poll.value("error", std::string{}).empty(), "bare poll must fail loudly");

    const auto begin = bare.BeginQrLogin(DeviceInfo{});
    CHECK(begin.value("status", -1) == 0, "bare begin must not report a code");
    CHECK(!begin.value("error", std::string{}).empty(), "bare begin must fail loudly");

    const auto refreshed = bare.RefreshSession(DeviceInfo{}, "42", "tok", "t1");
    CHECK(!refreshed.has_value(), "bare refresh must not produce a credential");

    std::cout << "  [ok] E5" << std::endl;
  }

  std::cout << "[QrTransport] All tests passed!" << std::endl;
  return 0;
}
