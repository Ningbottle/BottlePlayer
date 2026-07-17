#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <unordered_map>

namespace echo::core {

struct HttpResult {
  long statusCode = 0;
  std::string body;
  std::string error;
  bool timedOut = false;
};

// RAII: bind a cancellation flag for the current worker thread so nested
// HttpClient::Get/Post calls (from services) can observe scheduler deadlines
// without plumbing cancelled* through every service method.
class HttpClientCancellationScope {
 public:
  explicit HttpClientCancellationScope(const std::atomic_bool* cancelled);
  ~HttpClientCancellationScope();
  HttpClientCancellationScope(const HttpClientCancellationScope&) = delete;
  HttpClientCancellationScope& operator=(const HttpClientCancellationScope&) = delete;

 private:
  const std::atomic_bool* previous_ = nullptr;
};

class HttpClient {
 public:
  // cancelled: optional cooperative cancel. nullptr falls back to the
  // thread-local flag set by HttpClientCancellationScope (if any).
  HttpResult Get(
      const std::string& url,
      const std::unordered_map<std::string, std::string>& headers = {},
      long totalTimeoutMs = 9000,
      std::size_t maxBodyBytes = 10 * 1024 * 1024,
      const std::atomic_bool* cancelled = nullptr) const;

  // Post is NOT automatically retried (non-idempotent; playhistory upload).
  // Only a single attempt is made. cancelled same as Get.
  HttpResult Post(
      const std::string& url,
      const std::string& body,
      const std::unordered_map<std::string, std::string>& headers = {},
      long totalTimeoutMs = 9000,
      std::size_t maxBodyBytes = 10 * 1024 * 1024,
      const std::atomic_bool* cancelled = nullptr) const;
};

void CloseHttpConnectionPool();

// Number of WinHTTP request handles opened but not yet closed.
// For resilience tests / diagnostics (P0-A handle-leak regression).
long HttpClientLiveRequestHandleCount();

}  // namespace echo::core

