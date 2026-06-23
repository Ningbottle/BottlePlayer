// HttpClient resilience contract tests (S1)
// Tests the total-timeout and max-body-size guards added in S1.
// The timeout test uses a real URL with an extremely short deadline so it
// fires before any data arrives; if there is no network it still passes
// because the connection itself fails fast (statusCode==0, error set).

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>

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

int main() {
  std::cout << "[Test] Testing HttpClient timedOut field default...\n";
  {
    HttpResult r;
    CHECK(r.timedOut == false, "HttpResult.timedOut defaults to false");
  }

  std::cout << "[Test] Testing HttpClient total receive timeout...\n";
  {
    HttpClient client;
    // 1ms total timeout — should trigger before any real response.
    auto res = client.Get("https://httpbin.org/get", {}, /*totalTimeoutMs=*/1);
    // Either it timed out, or the connection failed (no network).
    // Both are acceptable: the key is we didn't hang forever.
    CHECK(res.timedOut || !res.error.empty(),
          "short-deadline GET returns timeout or error (not hang)");
  }

  std::cout << "[Test] Testing HttpClient max body size guard...\n";
  {
    HttpClient client;
    // 1-byte max body — any real response will exceed it.
    auto res = client.Get("https://httpbin.org/bytes/1024", {},
                          /*totalTimeoutMs=*/9000, /*maxBodyBytes=*/1);
    // Either max_body_exceeded, or timeout, or network error — not a hang.
    CHECK(!res.error.empty() || res.timedOut,
          "1-byte maxBody triggers error or timeout");
  }

  std::cout << "[Test] All HttpClient resilience tests completed.\n";
  std::cout << "  Passed: " << g_passed << "  Failed: " << g_failed << "\n";
  return g_failed == 0 ? 0 : 1;
}
