#pragma once

#include <atomic>
#include <cstddef>
#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include "echo/execution/OperationContext.h"

namespace echo::core {

struct HttpResult {
  long statusCode = 0;
  std::string body;
  std::string error;
  bool timedOut = false;
  unsigned long winHttpErrorCode = 0;
  int attemptCount = 1;
  long elapsedMs = 0;
};

// RAII: bind a cancellation flag for the current worker thread so nested
// HttpClient::Get/Post calls (from services) can observe scheduler deadlines
// without plumbing cancelled* through every service method.
class HttpClientCancellationScope {
 public:
  explicit HttpClientCancellationScope(const std::atomic_bool* cancelled, long remainingBudgetMs = 0,
      const std::atomic_bool* callerCancelled = nullptr);
  ~HttpClientCancellationScope();
  HttpClientCancellationScope(const HttpClientCancellationScope&) = delete;
  HttpClientCancellationScope& operator=(const HttpClientCancellationScope&) = delete;

 private:
  const std::atomic_bool* previous_ = nullptr;
  std::chrono::steady_clock::time_point previousDeadline_;
  const std::atomic_bool* previousCaller_ = nullptr;
  echo::execution::OperationContext previousOperation_;
};

// Also usable at persistence boundaries, after the final network wait.
// HttpRouteDeadlineExpired separates "the route ran out of time" from "someone
// cancelled this request": both stop the work, but they mean different things to
// the caller and to the diagnostics.
bool HttpRequestCanContinue();
bool HttpRouteDeadlineExpired();

// Default whole-request budget for a retried GET (ms). The budget is shared
// across every attempt AND the backoff, not per attempt. Named (not a literal
// in the default argument) so the per-kind scheduler deadlines can assert
// they never preempt it — see include/echo/core/RequestDeadlines.h.
inline constexpr long kHttpGetDefaultBudgetMs = 9000;

// Slice of the shared retry budget handed to one attempt. Giving an attempt
// everything that is left lets a single stall consume the retries the caller
// asked for, so the earlier attempts carry the reserve for the ones behind
// them. Exposed for the retry-budget probe.
long SingleAttemptCap(long left, int attempt, int maxAttempts, const long* backoffMs, long backoffCount);

class HttpClient {
 public:
  // cancelled: optional cooperative cancel. nullptr falls back to the
  // thread-local flag set by HttpClientCancellationScope (if any).
  // Set retryAllowed=false for GET endpoints with remote side effects.
  HttpResult Get(
      const std::string& url,
      const std::unordered_map<std::string, std::string>& headers = {},
      long totalTimeoutMs = kHttpGetDefaultBudgetMs,
      std::size_t maxBodyBytes = 10 * 1024 * 1024,
      const std::atomic_bool* cancelled = nullptr,
      bool retryAllowed = true) const;

  // Post is NOT automatically retried (non-idempotent; playhistory upload).
  // Only a single attempt is made. cancelled same as Get.
  HttpResult Post(
      const std::string& url,
      const std::string& body,
      const std::unordered_map<std::string, std::string>& headers = {},
      long totalTimeoutMs = 9000,
      std::size_t maxBodyBytes = 10 * 1024 * 1024,
      const std::atomic_bool* cancelled = nullptr) const;

  // Playback/lyrics only: reserve retry headroom within one wall-clock budget.
  // GET remains the sole retry owner; POST still makes exactly one attempt.
  HttpResult GetInteractive(
      const std::string& url,
      const std::unordered_map<std::string, std::string>& headers = {},
      long totalTimeoutMs = 4000,
      std::size_t maxBodyBytes = 10 * 1024 * 1024,
      const std::atomic_bool* cancelled = nullptr) const;
  HttpResult PostInteractive(
      const std::string& url,
      const std::string& body,
      const std::unordered_map<std::string, std::string>& headers = {},
      long totalTimeoutMs = 2000,
      std::size_t maxBodyBytes = 10 * 1024 * 1024,
      const std::atomic_bool* cancelled = nullptr) const;
};

// Legacy phase-budget arithmetic retained for native callers. Production
// requests use asynchronous I/O with one wall-clock deadline for all phases.
//
// WinHTTP applies these SEQUENTIALLY, not concurrently: a host that is slow at
// every stage costs resolve + connect + send + receive, and receive is a
// per-read bound rather than a total. A caller that needs to fit N requests
// inside one deadline must therefore size each one's budget against that sum,
// not against the deadline.
// Milliseconds. Declared as long rather than DWORD so this header stays free of
// <windows.h>.
struct RequestTimeouts {
  long resolve = 0;
  long connect = 0;
  long send = 0;
  long receive = 0;
};

// totalTimeoutMs <= 0 returns all zeros.
RequestTimeouts DeriveRequestTimeouts(long totalTimeoutMs);

void CloseHttpConnectionPool();

inline constexpr std::size_t kHttpConnectionPoolCapacity = 64;
std::size_t HttpClientCachedConnectionCount();

// Number of WinHTTP request handles opened but not yet closed.
// For resilience tests / diagnostics (P0-A handle-leak regression).
long HttpClientLiveRequestHandleCount();

// Includes closing async requests until the final callback has returned and
// their buffers/parent leases have been reclaimed by a non-callback thread.
long HttpClientLiveAsyncRequestCount();

// Test-only fault injection: forces the per-op timeout setup to fail so the
// timeout_setup_failed path (quota release, explicit error, GET no-retry,
// request never sent) can be exercised deterministically. Never set in
// production; tests must reset it to false afterwards.
void HttpClientSetTimeoutSetupFaultForTest(bool enabled);

// Thread-local, test-only transient transport failure. Bypasses WinHTTP so
// application retry/backoff contracts are independent of OS reconnect behavior.
// Default is false. Disable after use; the counter counts injected attempts.
void HttpClientSetTransientFaultForTest(bool enabled);
long HttpClientTransientFaultCountForTest();

}  // namespace echo::core
