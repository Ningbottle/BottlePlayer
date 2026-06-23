#pragma once

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

class HttpClient {
 public:
  HttpResult Get(
      const std::string& url,
      const std::unordered_map<std::string, std::string>& headers = {},
      long totalTimeoutMs = 9000,
      std::size_t maxBodyBytes = 10 * 1024 * 1024) const;

  HttpResult Post(
      const std::string& url,
      const std::string& body,
      const std::unordered_map<std::string, std::string>& headers = {},
      long totalTimeoutMs = 9000,
      std::size_t maxBodyBytes = 10 * 1024 * 1024) const;
};

void CloseHttpConnectionPool();

}  // namespace echo::core

