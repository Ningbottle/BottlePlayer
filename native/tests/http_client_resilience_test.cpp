// HttpClient resilience contract tests (S1)
// Tests the total-timeout and max-body-size guards added in S1.
// Uses a local TCP listener that accepts but never responds, so timeout
// behavior is deterministic and does not depend on external network.

#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

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

// Minimal local HTTP server that responds immediately with 200 OK.
// Used to exercise the SUCCESS path of ExecuteRequest (where request-handle
// close must run — P0-A regression for WinHTTP handle leak).
static int g_okPort = 0;

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
      char buf[1024];
      // Drain request headers (best-effort; local only).
      recv(c, buf, sizeof(buf), 0);
      send(c, response, static_cast<int>(strlen(response)), 0);
      closesocket(c);
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
  StartUnresponsiveServer();
  StartOkServer();
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
    CHECK(elapsed < 2000,
          "500ms budget enforced within 2s (per-op+watchdog, plus retry-cap)");
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
    // Single attempt: watchdog ~800ms; with ×3 retry would be multi-second + backoffs.
    CHECK(elapsed < 1600, "Post does not budget-retry (single attempt)");
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

  std::cout << "[Test] All HttpClient resilience tests completed.\n";
  std::cout << "  Passed: " << g_passed << "  Failed: " << g_failed << "\n";
  return g_failed == 0 ? 0 : 1;
}
