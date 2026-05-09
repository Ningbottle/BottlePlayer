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
};

}  // namespace echo::core

