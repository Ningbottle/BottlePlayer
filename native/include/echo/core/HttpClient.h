#pragma once

#include <string>
#include <unordered_map>

namespace echo::core {

struct HttpResult {
  long statusCode = 0;
  std::string body;
  std::string error;
};

class HttpClient {
 public:
  HttpResult Get(
      const std::string& url,
      const std::unordered_map<std::string, std::string>& headers = {}) const;

  HttpResult Post(
      const std::string& url,
      const std::string& body,
      const std::unordered_map<std::string, std::string>& headers = {}) const;
};

/// Graceful shutdown: close all pooled WinHTTP session/connect handles.
/// Safe to call multiple times (idempotent). Call from EchoShutdown.
void CloseHttpConnectionPool();

}  // namespace echo::core

