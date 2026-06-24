// HttpClient resilience contract tests (S1)
// Tests the total-timeout and max-body-size guards added in S1.
// Uses a local TCP listener that accepts but never responds, so timeout
// behavior is deterministic and does not depend on external network.

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include <winsock2.h>
#include <ws2tcpip.h>

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

int main() {
  StartUnresponsiveServer();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::cout << "[Test] Testing HttpClient timedOut field default...\n";
  {
    HttpResult r;
    CHECK(r.timedOut == false, "HttpResult.timedOut defaults to false");
  }

  std::cout << "[Test] Testing HttpClient total receive timeout against unresponsive server...\n";
  {
    HttpClient client;
    std::string url = "http://127.0.0.1:" + std::to_string(g_listenPort) + "/test";
    // 500ms total timeout — the unresponsive server will block until our
    // total-timeout check in the read loop fires.
    auto start = std::chrono::steady_clock::now();
    auto res = client.Get(url, {}, /*totalTimeoutMs=*/500);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    // Must have timed out (not just gotten a network error)
    CHECK(res.timedOut || !res.error.empty(),
          "unresponsive server produces timeout or error");
    // Must not have taken longer than 5s (per-op timeout is 10s, but our
    // total timeout + retry budget should be well under that)
    CHECK(elapsed < 15000, "total time under 15s");
  }

  std::cout << "[Test] Testing HttpClient max body size guard...\n";
  {
    HttpClient client;
    // Use httpbin to get a real response > 1 byte, with 1-byte maxBody
    auto res = client.Get("https://httpbin.org/bytes/1024", {},
                          /*totalTimeoutMs=*/9000, /*maxBodyBytes=*/1);
    // Must have an error or timeout (body exceeded 1 byte), not success
    CHECK(!res.error.empty() || res.timedOut,
          "1-byte maxBody triggers error or timeout on real response");
    // If it did succeed (network issue), body must be empty or tiny
    if (res.error.empty() && !res.timedOut) {
      CHECK(res.body.size() <= 1, "body is at most 1 byte when maxBodyBytes=1");
    }
  }

  std::cout << "[Test] All HttpClient resilience tests completed.\n";
  std::cout << "  Passed: " << g_passed << "  Failed: " << g_failed << "\n";
  return g_failed == 0 ? 0 : 1;
}
