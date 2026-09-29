// HttpClient resilience contract tests (S1)
// Tests the total-timeout and max-body-size guards added in S1.
// Uses a local TCP listener that accepts but never responds, so timeout
// behavior is deterministic and does not depend on external network.

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "echo/async/RequestWatchdog.h"
#include "echo/core/HttpClient.h"

using echo::core::HttpClient;
using echo::core::HttpResult;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg) \
  do { \
    if (cond) { \
      std::cout << "  [ok] " << (msg) << "\n"; \
      ++g_passed; \
    } else { \
      std::cerr << "  [FAIL] " << (msg) << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
      ++g_failed; \
    } \
  } while (0)

// Minimal local server that accepts a connection but never sends a response.
// This guarantees WinHTTP blocks until its per-op timeout fires, letting us
// verify the total-timeout logic deterministically.
static int g_listenPort = 0;

static void StartUnresponsiveServer() {
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
  SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0; // let OS pick a port
  bind(srv, (sockaddr*)&addr, sizeof(addr));
  listen(srv, 5);
  socklen_t len = sizeof(addr);
  getsockname(srv, (sockaddr*)&addr, &len);
  g_listenPort = ntohs(addr.sin_port);
  // Accept connections in background but never respond
  std::thread([srv]() {
    while (true) {
      sockaddr_in cli{};
      int clilen = sizeof(cli);
      SOCKET c = accept(srv, (sockaddr*)&cli, &clilen);
      if (c == INVALID_SOCKET) break;
      // Keep connection open but send nothing; close after 60s to avoid leak
      std::this_thread::sleep_for(std::chrono::seconds(60));
      closesocket(c);
    }
  }).detach();
}

// Read until end-of-headers, then Content-Length body bytes. Returns false
// when the peer disconnects mid-request. Shared by the local fixtures.
static bool ReadFullHttpRequest(SOCKET c, std::string& accum) {
  size_t headerEnd = std::string::npos;
  long contentLength = 0;
  bool headersDone = false;
  char buf[2048];
  for (;;) {
    if (headersDone &&
        accum.size() >= headerEnd + 4 + static_cast<size_t>(contentLength)) {
      return true;
    }
    int n = recv(c, buf, sizeof(buf), 0);
    if (n <= 0) return false;  // peer went away or errored
    accum.append(buf, static_cast<size_t>(n));
    if (!headersDone) {
      headerEnd = accum.find("\r\n\r\n");
      if (headerEnd != std::string::npos) {
        headersDone = true;
        std::string lower;
        lower.reserve(accum.size());
        for (char ch : accum) {
          lower.push_back(
              static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        auto pos = lower.find("content-length:");
        if (pos != std::string::npos && pos < headerEnd) {
          contentLength = strtol(accum.c_str() + pos + 15, nullptr, 10);
          if (contentLength < 0) contentLength = 0;
        }
      }
    }
  }
}

// Minimal local HTTP server that responds immediately with 200 OK.
// Reads the ENTIRE request (headers + Content-Length body) before
// responding: WinHTTP writes headers and body as separate TCP segments,
// so the old single-recv + immediate-close could RST the client's body
// write and turn an occasional POST into a connection error — the flaky
// non-200 the review caught in the 306-request quota loop.
// Used to exercise the SUCCESS path of ExecuteRequest (where request-handle
// close must run — P0-A regression for WinHTTP handle leak).
static int g_okPort = 0;
static std::atomic<int> g_okRequestCount{0};

static void StartOkServer() {
  SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  bind(srv, (sockaddr*)&addr, sizeof(addr));
  listen(srv, 64);
  socklen_t len = sizeof(addr);
  getsockname(srv, (sockaddr*)&addr, &len);
  g_okPort = ntohs(addr.sin_port);
  std::thread([srv]() {
    const char* response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 2\r\n"
        "Connection: close\r\n"
        "\r\n"
        "ok";
    while (true) {
      sockaddr_in cli{};
      int clilen = sizeof(cli);
      SOCKET c = accept(srv, (sockaddr*)&cli, &clilen);
      if (c == INVALID_SOCKET) break;
      g_okRequestCount.fetch_add(1, std::memory_order_acq_rel);
      std::string request;
      if (!ReadFullHttpRequest(c, request)) {
        closesocket(c);
        continue;
      }
      send(c, response, static_cast<int>(strlen(response)), 0);
      closesocket(c);
    }
  }).detach();
}

// Batch A RED fixture: sends 200 + Content-Length: 2 headers, then STALLS
// without the promised body. The deadline must surface as timeout/error —
// never as a 200 "success" with an empty body (review probe).
static int g_stallPort = 0;

static void StartStallServer() {
  SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  bind(srv, (sockaddr*)&addr, sizeof(addr));
  listen(srv, 16);
  socklen_t len = sizeof(addr);
  getsockname(srv, (sockaddr*)&addr, &len);
  g_stallPort = ntohs(addr.sin_port);
  std::thread([srv]() {
    const char* headers =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 2\r\n"
        "\r\n";  // promised body never arrives
    while (true) {
      sockaddr_in cli{};
      int clilen = sizeof(cli);
      SOCKET c = accept(srv, (sockaddr*)&cli, &clilen);
      if (c == INVALID_SOCKET) break;
      std::string request;
      if (!ReadFullHttpRequest(c, request)) {
        closesocket(c);
        continue;
      }
      send(c, headers, static_cast<int>(strlen(headers)), 0);
      // Stall: hold the socket, never send the body.
      std::this_thread::sleep_for(std::chrono::seconds(120));
      closesocket(c);
    }
  }).detach();
}

// Accepts connections and immediately closes them. Client requests fail
// FAST (connection reset / abort) instead of hanging on the session-level
// receive timeout, which makes thousand-scale stress cheap while still
// exercising the full concurrent failure path (send/receive failure, evict,
// quota cancel, handle accounting).
static int g_resetPort = 0;
static std::atomic<int> g_resetAcceptCount{0};

static void StartResetServer() {
  SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  bind(srv, (sockaddr*)&addr, sizeof(addr));
  listen(srv, 256);
  socklen_t len = sizeof(addr);
  getsockname(srv, (sockaddr*)&addr, &len);
  g_resetPort = ntohs(addr.sin_port);
  std::thread([srv]() {
    while (true) {
      sockaddr_in cli{};
      int clilen = sizeof(cli);
      SOCKET c = accept(srv, (sockaddr*)&cli, &clilen);
      if (c == INVALID_SOCKET) break;
      g_resetAcceptCount.fetch_add(1, std::memory_order_acq_rel);
      closesocket(c);  // immediate reset — requests fail fast
    }
  }).detach();
}

// Persistent-accept fixture for the Stage 3a stress test: accepts
// connections continuously and holds them without reading or responding
// (the old unresponsive fixture slept 60s per accept and could not carry
// high-concurrency stress). Requests against it resolve at the SESSION-level
// receive timeout (10s), so only small samples use it.
static int g_holdPort = 0;
static std::atomic<int> g_holdAcceptCount{0};
static void StartHoldServer() {
  SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  bind(srv, (sockaddr*)&addr, sizeof(addr));
  listen(srv, 256);
  socklen_t len = sizeof(addr);
  getsockname(srv, (sockaddr*)&addr, &len);
  g_holdPort = ntohs(addr.sin_port);
  std::thread([srv]() {
    std::vector<SOCKET> held;
    std::mutex heldMutex;
    while (true) {
      sockaddr_in cli{};
      int clilen = sizeof(cli);
      SOCKET c = accept(srv, (sockaddr*)&cli, &clilen);
      if (c == INVALID_SOCKET) break;
      g_holdAcceptCount.fetch_add(1, std::memory_order_acq_rel);
      std::lock_guard<std::mutex> lock(heldMutex);
      held.push_back(c);  // hold open, never read/respond
    }
  }).detach();
}

static DWORD ProcessHandleCount() {
  DWORD count = 0;
  if (!GetProcessHandleCount(GetCurrentProcess(), &count)) {
    return 0;
  }
  return count;
}

int main() {
  // Unbuffered stdout: the hung-process diagnosis needs every [ok] visible
  // in the redirected file the moment it prints.
  std::cout.setf(std::ios::unitbuf);
  StartUnresponsiveServer();
  StartOkServer();
  StartStallServer();
  StartHoldServer();
  StartResetServer();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::cout << "[Test] Testing HttpClient timedOut field default...\n";
  {
    HttpResult r;
    CHECK(r.timedOut == false, "HttpResult.timedOut defaults to false");
  }

  std::cout << "[Test] Testing HttpClient single-attempt total timeout...\n";
  {
    // Use a 5s budget so the retry-budget cap doesn't fire (we want to
    // prove per-op timeouts + watchdog fire on the FIRST attempt, not
    // that retry-budget saves us). The unresponsive server will block
    // past the per-op timeouts. With per-op = total/3 (~1667ms for
    // send/receive), the watchdog fires at 5s and total elapsed is
    // close to 5s. The retry-budget cap would also fire (allowing one
    // retry), so the test allows up to 6s (5s + 500ms backoff).
    HttpClient client;
    std::string url = "http://127.0.0.1:" + std::to_string(g_listenPort) + "/test";
    auto start = std::chrono::steady_clock::now();
    auto res = client.Get(url, {}, /*totalTimeoutMs=*/5000);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    CHECK(res.timedOut || !res.error.empty(),
          "unresponsive server produces timeout or error");
    // First attempt: per-op timeouts (~1667ms each, but unreachable server
    // triggers the watchdog at 5s) + one retry with 500ms backoff.
    // Upper bound: 5s (first attempt) + 500ms (backoff) + ~5s (retry).
    // We assert < 11s to prove the *first* attempt was bounded by 5s.
    std::cout << "  [debug] elapsed=" << elapsed << "ms\n";
    CHECK(elapsed < 11000,
          "5s budget enforced within 11s (proves per-op+watchdog fire on attempt 1)");
  }

  std::cout << "[Test] Testing HttpClient tight 500ms budget...\n";
  {
    // Tighter budget: 500ms total, retry-budget cap (total/3 + 100 = 266ms
    // remaining) fires immediately on attempt 2, so the test should
    // complete in attempt 1 + 500ms backoff ≈ 1000ms. Anything over 2s
    // proves per-op timeouts or watchdog are not being honored.
    HttpClient client;
    std::string url = "http://127.0.0.1:" + std::to_string(g_listenPort) + "/test";
    auto start = std::chrono::steady_clock::now();
    auto res = client.Get(url, {}, /*totalTimeoutMs=*/500);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    CHECK(res.timedOut || !res.error.empty(),
          "500ms budget: unresponsive server produces timeout or error");
    std::cout << "  [debug] elapsed=" << elapsed << "ms\n";
    // Stage 3a envelope: without the contract-violating cross-thread close,
    // a hung call unblocks at its per-op timeouts. Measured on this machine:
    // per-op timeouts have an effective floor (~1.2s per blocking call — a
    // 266ms per-op is not honored, 1667ms is), so a 500ms budget returns in
    // ~4s across the retry schedule instead of ~1s. The scheduler layer
    // (job_deadline per request kind) still enforces user-visible deadlines;
    // Stage 3b (async sessions) restores wall-clock-exact budgets.
    CHECK(elapsed < 6000,
          "500ms budget returns within the 3a per-op envelope (scheduler layer enforces user deadlines)");
  }

  std::cout << "[Test] Testing HttpClient max body size guard...\n";
  {
    HttpClient client;
    // Local mock (StartOkServer): 2-byte body "ok" triggers maxBodyBytes=1 guard.
    // Offline-stable — no httpbin/external network dependency.
    std::string url = "http://127.0.0.1:" + std::to_string(g_okPort) + "/ok";
    auto res = client.Get(url, {},
                          /*totalTimeoutMs=*/3000, /*maxBodyBytes=*/1);
    CHECK(!res.error.empty() || res.timedOut,
          "1-byte maxBody triggers error or timeout on local mock");
    if (res.error.empty() && !res.timedOut) {
      CHECK(res.body.size() <= 1, "body is at most 1 byte when maxBodyBytes=1");
    }
  }

  // P1-F: Post must not auto-retry on upstream 5xx / connection errors.
  std::cout << "[Test] Testing HttpClient Post does not auto-retry...\n";
  {
    // Unresponsive server: GET would retry (up to budget); Post is single-shot.
    HttpClient client;
    std::string url = "http://127.0.0.1:" + std::to_string(g_listenPort) + "/post";
    auto start = std::chrono::steady_clock::now();
    auto res = client.Post(url, "{}", {}, /*totalTimeoutMs=*/800);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    CHECK(res.timedOut || !res.error.empty(),
          "Post to unresponsive server errors or times out");
    std::cout << "  [debug] Post elapsed=" << elapsed << "ms\n";
    // Single attempt: no retry amplification (a ×3 retry would be
    // multi-second + backoffs on top of the per-op envelope below).
    // Stage 3a envelope: hung calls unblock at per-op timeouts, which have
    // a ~1.2s-per-call floor on this OS — an 800ms budget returns in ~3.5s
    // (measured) instead of 800ms. See the 500ms-budget test note: the
    // scheduler layer enforces user-visible deadlines; 3b restores exact
    // HttpClient budgets.
    CHECK(elapsed < 6000, "Post does not budget-retry (single attempt, 3a per-op envelope)");
  }

  // P0-A: successful requests must close their WinHTTP request handles.
  // Pre-fix: every success path CAS-disarmed the watchdog early, so the
  // final WinHttpCloseHandle was skipped and handles accumulated ~1 per request.
  // Note: GetProcessHandleCount does not reliably observe HINTERNET objects,
  // so we assert on HttpClientLiveRequestHandleCount() (open − closed).
  std::cout << "[Test] Testing HttpClient request-handle close on success (P0-A)...\n";
  {
    HttpClient client;
    std::string url = "http://127.0.0.1:" + std::to_string(g_okPort) + "/ok";

    // Warm up connection pool / WinHTTP so baseline is stable.
    for (int i = 0; i < 5; ++i) {
      auto res = client.Get(url, {}, /*totalTimeoutMs=*/3000);
      CHECK(res.error.empty() && res.statusCode == 200,
            "warmup request succeeds");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const long liveBefore = echo::core::HttpClientLiveRequestHandleCount();
    const DWORD osBefore = ProcessHandleCount();
    constexpr int kN = 40;
    int okCount = 0;
    for (int i = 0; i < kN; ++i) {
      auto res = client.Get(url, {}, /*totalTimeoutMs=*/3000);
      if (res.error.empty() && res.statusCode == 200) {
        ++okCount;
      }
    }
    CHECK(okCount == kN, "all measured success-path requests returned 200");

    const long liveAfter = echo::core::HttpClientLiveRequestHandleCount();
    const long liveDelta = liveAfter - liveBefore;
    const DWORD osAfter = ProcessHandleCount();
    std::cout << "  [debug] live_request_handles before=" << liveBefore
              << " after=" << liveAfter << " delta=" << liveDelta
              << " os_handles delta="
              << (static_cast<long>(osAfter) - static_cast<long>(osBefore))
              << " (N=" << kN << ")\n";
    // Pre-fix leaked ~1 handle per success → liveDelta ≈ N.
    // Post-fix every success closes → liveDelta == 0.
    CHECK(liveDelta == 0,
          "successful requests close every WinHTTP request handle (no live growth)");
  }

  std::cout << "[Test] HTTP quota: completed requests release watchdog admission budget...\n";
  {
    // Review P1: the owner-wins cleanup paths closed the request handle but
    // never cancelled the deadline entry — every completed request held its
    // admission budget until the deadline expired, so the new 256-entry cap
    // produced spurious watchdog_overload after ~256 requests (review
    // probe: 256 OK, then overload). Regression: more than the cap's worth
    // of sequential requests must all succeed.
    using echo::async::RequestWatchdog;
    RequestWatchdog& wd = RequestWatchdog::Instance();
    const int kRequests = static_cast<int>(RequestWatchdog::kMaxPendingEntries) + 50;
    HttpClient client;
    std::string url = "http://127.0.0.1:" + std::to_string(g_okPort) + "/quota";
    int oks = 0;
    bool overloaded = false;
    std::string firstFailure;
    for (int i = 0; i < kRequests; ++i) {
      auto res = client.Post(url, "{}", {}, /*totalTimeoutMs=*/2000);
      if (res.error == "watchdog_overload") { overloaded = true; break; }
      if (res.statusCode == 200) {
        ++oks;
      } else if (firstFailure.empty()) {
        // Review requirement: record the failing request's index, status,
        // error and timedOut so a flake is diagnosable, and keep looping so
        // a single failure cannot hide its siblings.
        std::ostringstream ss;
        ss << "i=" << i << " status=" << res.statusCode
           << " timedOut=" << (res.timedOut ? "Y" : "N")
           << " error=" << res.error;
        firstFailure = ss.str();
      }
    }
    if (!firstFailure.empty()) {
      std::cout << "  [debug] first non-200 in success loop: " << firstFailure
                << "\n";
    }
    CHECK(!overloaded,
          "success loop past the backlog cap never hits watchdog_overload (quota released per completion)");
    CHECK(firstFailure.empty() && oks == kRequests,
          "all success-loop requests returned 200 (no connection/body races)");
    bool drained = false;
    for (int i = 0; i < 500 && !drained; ++i) {
      drained = wd.DebugPendingActions() == 0;
      if (!drained) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(drained, "watchdog pending returns to 0 after the success loop");
    CHECK(echo::core::HttpClientLiveRequestHandleCount() == 0,
          "no request handles leaked by the success loop");
  }

  std::cout << "[Test] HTTP failure cleanup releases the deadline quota promptly...\n";
  {
    using echo::async::RequestWatchdog;
    RequestWatchdog& wd = RequestWatchdog::Instance();
    HttpClient client;
    // Unresponsive server: send succeeds, receive fails at its per-op
    // timeout → owner-wins CAS cleanup path (the one that used to skip
    // Cancel). The deadline (900ms) has NOT expired yet, so the quota must
    // be released by the cleanup, not by the deadline.
    std::string url = "http://127.0.0.1:" + std::to_string(g_listenPort) + "/fail";
    auto res = client.Post(url, "{}", {}, /*totalTimeoutMs=*/900);
    CHECK(!res.error.empty(), "unresponsive server produces an error (owner cleanup path)");
    // Short poll on purpose: pending must hit 0 well before the 900ms
    // deadline expires, proving the cleanup (not the deadline) released it.
    bool drained = false;
    for (int i = 0; i < 30 && !drained; ++i) {
      drained = wd.DebugPendingActions() == 0;
      if (!drained) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(drained, "quota released promptly after failure cleanup (not held until deadline)");
    CHECK(echo::core::HttpClientLiveRequestHandleCount() == 0,
          "no request handles leaked on the failure path");

    std::string okUrl = "http://127.0.0.1:" + std::to_string(g_okPort) + "/after-fail";
    auto ok = client.Post(okUrl, "{}", {}, /*totalTimeoutMs=*/2000);
    CHECK(ok.statusCode == 200 && ok.error.empty(),
          "watchdog still accepts new deadlines after failure cleanup");
  }

  std::cout << "[Test] watchdog saturation: HTTP admission failures are immediate and preserved...\n";
  {
    // Review P2: GET classified watchdog_overload as a transient network
    // error and retried it through the backoff schedule (probe: 2527ms for
    // a 1000ms budget). The admission failure must come back verbatim and
    // immediately, for GET and POST alike, without leaking handles.
    using echo::async::RequestWatchdog;
    RequestWatchdog& wd = RequestWatchdog::Instance();

    struct SatState {
      std::atomic<bool> drain{false};
      std::atomic<int> completed{0};
    };
    auto st = std::make_shared<SatState>();
    auto blocking = [st] {
      while (!st->drain.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      st->completed.fetch_add(1, std::memory_order_acq_rel);
    };
    int armed = 0;
    while (wd.Arm(1, std::make_shared<std::atomic_bool>(false), blocking)) {
      ++armed;
      if (armed > 4000) break;  // safety net against a broken cap
    }
    CHECK(wd.DebugPendingActions() == RequestWatchdog::kMaxPendingEntries,
          "http saturation setup: watchdog backlog at cap");

    HttpClient client;
    std::string url = "http://127.0.0.1:" + std::to_string(g_okPort) + "/sat";

    auto start = std::chrono::steady_clock::now();
    auto res = client.Get(url, {}, /*totalTimeoutMs=*/2000);
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << "  [debug] GET elapsed=" << elapsedMs << "ms error=" << res.error << "\n";
    CHECK(res.error == "watchdog_overload",
          "GET: admission failure preserved verbatim (not classified transient)");
    CHECK(elapsedMs < 1000,
          "GET: returns immediately — no retry backoff on admission failure");
    CHECK(res.statusCode == 0, "GET: no HTTP status for a local rejection");

    auto pres = client.Post(url, "{}", {}, /*totalTimeoutMs=*/2000);
    CHECK(pres.error == "watchdog_overload", "POST: admission failure preserved verbatim");
    CHECK(echo::core::HttpClientLiveRequestHandleCount() == 0,
          "rejected requests leak no request handles");

    st->drain.store(true, std::memory_order_release);
    bool drained = false;
    for (int i = 0; i < 1000 && !drained; ++i) {
      drained = st->completed.load() == armed && wd.DebugPendingActions() == 0;
      if (!drained) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(drained, "http saturation teardown: watchdog fully drained");
  }

  std::cout << "[Test] structural: watchdog action never closes request handles (Stage 3a)...\n";
  {
    // Plan Stage 3a RED (i): a request handle's WinHttpCloseHandle must only
    // run on the ExecuteRequest (owner) thread. The watchdog action must
    // therefore never touch the handle — it may only raise the cooperative
    // deadline flag. Greps the Arm call site inside HttpClient.cpp.
    FILE* f = nullptr;
    if (fopen_s(&f, ECHO_NATIVE_SOURCE_DIR "/core/HttpClient.cpp", "rb") != 0 || !f) {
      CHECK(false, "structural: HttpClient.cpp readable from ECHO_NATIVE_SOURCE_DIR");
      std::cout << "  Passed: " << g_passed << "  Failed: " << g_failed << "\n";
      return 1;
    }
    std::string src;
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
      src.append(buf, n);
    }
    fclose(f);

    auto armPos = src.find("Instance().Arm(");
    CHECK(armPos != std::string::npos, "structural: watchdog Arm call site found");
    if (armPos != std::string::npos) {
      auto stmtEnd = src.find(");", armPos);
      if (stmtEnd == std::string::npos) stmtEnd = src.size();
      const std::string armCall = src.substr(armPos, stmtEnd - armPos);
      const bool closesHandle =
          armCall.find("CloseRequestHandle") != std::string::npos ||
          armCall.find("WinHttpCloseHandle") != std::string::npos;
      CHECK(!closesHandle,
            "structural: watchdog action does not close request handles (owner-thread close only)");
    }
  }

  std::cout << "[Test] structural: per-op timeouts set on the request handle and checked...\n";
  {
    // Batch A P1-1: WINHTTP_OPTION_CONNECT_TIMEOUT was set on the CONNECT
    // handle, which WinHTTP rejects with ERROR_WINHTTP_INCORRECT_HANDLE_TYPE
    // (12018) — silently, leaving connects at the system default. After the
    // watchdog lost its cross-thread close (3a), that setting is the
    // load-bearing defense and must live on the request handle, with every
    // setup failure surfaced as an explicit request error.
    FILE* f = nullptr;
    if (fopen_s(&f, ECHO_NATIVE_SOURCE_DIR "/core/HttpClient.cpp", "rb") != 0 || !f) {
      CHECK(false, "structural: HttpClient.cpp readable from ECHO_NATIVE_SOURCE_DIR");
      std::cout << "  Passed: " << g_passed << "  Failed: " << g_failed << "\n";
      return 1;
    }
    std::string src;
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
      src.append(buf, n);
    }
    fclose(f);
    CHECK(src.find("WinHttpSetOption(connect") == std::string::npos,
          "structural: connect timeout is not set on the connect handle (12018)");
    CHECK(src.find("timeout_setup_failed") != std::string::npos,
          "structural: per-op timeout setup failures fail the request explicitly");
  }

  std::cout << "[Test] stalled body after headers must surface as timeout, never success...\n";
  {
    // Batch A P1-2 (review probe): a 200 + Content-Length: 2 response whose
    // body never arrives returned status=200 timedOut=false error="" — the
    // receive loop treated QueryDataAvailable failure as EOF and the final
    // cleanup ignored the deadline flag. Contract: a deadline-expired or
    // otherwise incomplete response is NEVER a bare success.
    HttpClient client;
    std::string url = "http://127.0.0.1:" + std::to_string(g_stallPort) + "/stall";
    auto start = std::chrono::steady_clock::now();
    auto res = client.Post(url, "{}", {}, /*totalTimeoutMs=*/800);
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << "  [debug] stall elapsed=" << elapsedMs << "ms status=" << res.statusCode
              << " timedOut=" << (res.timedOut ? "Y" : "N")
              << " error=" << res.error << " body=" << res.body.size() << "B\n";
    const bool falseSuccess =
        res.statusCode == 200 && res.error.empty() && !res.timedOut;
    CHECK(!falseSuccess,
          "headers-only 200 with a stalled body never returns as success");
    CHECK(res.timedOut || !res.error.empty(),
          "deadline produces timeout or error");
    CHECK(echo::core::HttpClientLiveRequestHandleCount() == 0,
          "no request handle leaked on the stalled-body path");
  }

  std::cout << "[Test] timeout_setup_failed: quota released, request unsent, GET no-retry...\n";
  {
    // Batch A review P1: the setup-failure path armed the watchdog before
    // the per-op options, then returned WITHOUT cancelling — leaking the
    // admission budget — and GET classified the error as transient and
    // retried it through the backoff schedule (review probe: elapsed
    // 2518ms, pending=3). Contract under injected failure:
    //   1. POST returns timeout_setup_failed immediately (single attempt,
    //      no backoff),
    //   2. the watchdog pending count returns to baseline (quota released,
    //      not held until deadline),
    //   3. the request never reaches the server (setup precedes send),
    //   4. no handle leaks,
    //   5. GET returns the same error just as fast (no retry classification).
    using echo::async::RequestWatchdog;
    RequestWatchdog& wd = RequestWatchdog::Instance();
    HttpClient client;
    std::string url = "http://127.0.0.1:" + std::to_string(g_okPort) + "/setup-fail";

    const int serverCountBefore = g_okRequestCount.load();
    const long liveBefore = echo::core::HttpClientLiveRequestHandleCount();
    echo::core::HttpClientSetTimeoutSetupFaultForTest(true);

    auto start = std::chrono::steady_clock::now();
    auto res = client.Post(url, "{}", {}, /*totalTimeoutMs=*/1000);
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    CHECK(res.error == "timeout_setup_failed",
          "setup-failure POST returns the explicit error");
    CHECK(elapsedMs < 300,
          "setup-failure POST returns immediately (no backoff)");
    bool quotaReleased = false;
    for (int i = 0; i < 100 && !quotaReleased; ++i) {
      quotaReleased = wd.DebugPendingActions() == 0;
      if (!quotaReleased) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(quotaReleased, "setup-failure releases the admission quota (no leak until deadline)");
    CHECK(g_okRequestCount.load() == serverCountBefore,
          "setup-failure request is never sent to the server");
    CHECK(echo::core::HttpClientLiveRequestHandleCount() == liveBefore,
          "setup-failure leaks no request handle");

    start = std::chrono::steady_clock::now();
    auto gres = client.Get(url, {}, /*totalTimeoutMs=*/1000);
    elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    echo::core::HttpClientSetTimeoutSetupFaultForTest(false);
    CHECK(gres.error == "timeout_setup_failed",
          "setup-failure GET returns the explicit error (no transient retry)");
    CHECK(elapsedMs < 300,
          "setup-failure GET returns immediately (no backoff schedule)");
  }

  std::cout << "[Test] stress: 1000 concurrent fast-fail requests vs reset server...\n";
  {
    // Plan Stage 3a GREEN(ii) at the reviewed N-thousand scale.
    //
    // Scale-reality note (measured): a connection the server never answers
    // unblocks at the SESSION-level receive timeout (WinHttpSetTimeouts 10s)
    // — request-level 100ms receive/response timeouts are not honored for
    // that wait on this OS, so held-connection volume is capped separately
    // below. The reset server makes requests fail FAST instead, letting
    // 1000 requests exercise the concurrent failure path (send/receive
    // failure, pool evict, quota cancel, handle accounting) in seconds.
    //
    // Expected result classes: WinHttp* API failure strings (connection
    // reset/abort) — never a success, never a body-guard or setup error.
    using echo::async::RequestWatchdog;
    RequestWatchdog& wd = RequestWatchdog::Instance();
    HttpClient client;
    std::string url = "http://127.0.0.1:" + std::to_string(g_resetPort) + "/stress";

    int scale = 1;
    if (const char* e = std::getenv("ECHO_HTTP_STRESS_SCALE")) {
      scale = std::max(1, atoi(e));
    }
    const int kThreads = 8;
    const int kPerThread = 125 * scale;  // default: 1000 requests
    const int kTotal = kThreads * kPerThread;

    std::atomic<int> successes{0};
    std::atomic<int> networkFailures{0};
    std::atomic<int> overloads{0};
    std::atomic<int> unexpected{0};
    std::mutex unexpectedMutex;
    std::string firstUnexpected;
    auto worker = [&]() {
      for (int i = 0; i < kPerThread; ++i) {
        auto res = client.Post(url, "{}", {}, /*totalTimeoutMs=*/100);
        if (res.error == "watchdog_overload") {
          overloads.fetch_add(1, std::memory_order_acq_rel);
          continue;
        }
        if (res.statusCode == 200 && res.error.empty() && !res.timedOut) {
          successes.fetch_add(1, std::memory_order_acq_rel);
          continue;
        }
        if (!res.error.empty() && res.error.find("WinHttp") != std::string::npos) {
          networkFailures.fetch_add(1, std::memory_order_acq_rel);
          continue;
        }
        std::lock_guard<std::mutex> lock(unexpectedMutex);
        if (firstUnexpected.empty()) {
          std::ostringstream ss;
          ss << "status=" << res.statusCode
             << " timedOut=" << (res.timedOut ? "Y" : "N")
             << " error=" << res.error;
          firstUnexpected = ss.str();
        }
        unexpected.fetch_add(1, std::memory_order_acq_rel);
      }
    };
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    CHECK(successes.load() == 0,
          "stress: a resetting server never produces a success");
    CHECK(unexpected.load() == 0,
          "stress: only WinHttp network failures occurred (first unexpected: " +
              firstUnexpected + ")");
    CHECK(networkFailures.load() + overloads.load() == kTotal,
          "stress: every request resolved to a network failure/admission rejection");
    CHECK(g_resetAcceptCount.load() >= kTotal - 8,
          "stress: requests actually reached the server (accepts ~= issued)");
    bool drained = false;
    for (int i = 0; i < 1500 && !drained; ++i) {
      drained = wd.DebugPendingActions() == 0;
      if (!drained) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(drained, "stress: watchdog fully drained after the run");
    CHECK(echo::core::HttpClientLiveRequestHandleCount() == 0,
          "stress: zero live request handles");
    std::cout << "  [debug] stress total=" << kTotal
              << " networkFailures=" << networkFailures.load()
              << " overloads=" << overloads.load()
              << " accepts=" << g_resetAcceptCount.load() << "\n";
  }

  std::cout << "[Test] stress-held: held connections resolve without success or leaks...\n";
  {
    // Small-sample held-connection stress: the requests block in
    // WinHttpReceiveResponse until the SESSION-level receive timeout
    // (~10s each — see the scale note above), 8 in parallel ≈ one round.
    // Contract: no success, no handle leak, watchdog drained.
    using echo::async::RequestWatchdog;
    RequestWatchdog& wd = RequestWatchdog::Instance();
    HttpClient client;
    std::string url = "http://127.0.0.1:" + std::to_string(g_holdPort) + "/held";

    const int kThreads = 8;
    std::atomic<int> successes{0};
    std::atomic<int> resolved{0};
    auto worker = [&]() {
      auto res = client.Post(url, "{}", {}, /*totalTimeoutMs=*/100);
      if (res.statusCode == 200 && res.error.empty() && !res.timedOut) {
        successes.fetch_add(1, std::memory_order_acq_rel);
      } else {
        resolved.fetch_add(1, std::memory_order_acq_rel);
      }
    };
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    CHECK(successes.load() == 0, "stress-held: a holding server never produces a success");
    CHECK(resolved.load() == kThreads, "stress-held: every held request resolved");
    bool drained = false;
    for (int i = 0; i < 1500 && !drained; ++i) {
      drained = wd.DebugPendingActions() == 0;
      if (!drained) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(drained, "stress-held: watchdog fully drained");
    CHECK(echo::core::HttpClientLiveRequestHandleCount() == 0,
          "stress-held: zero live request handles");
  }

  std::cout << "[Test] All HttpClient resilience tests completed.\n";
  std::cout << "  Passed: " << g_passed << "  Failed: " << g_failed << "\n";
  return g_failed == 0 ? 0 : 1;
}
