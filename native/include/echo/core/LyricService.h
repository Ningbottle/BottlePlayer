#pragma once

#include <functional>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"

namespace echo::core {

using LyricHttpGet = std::function<HttpResult(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers)>;
using LyricClock = std::function<std::chrono::steady_clock::time_point()>;

class LyricService {
 public:
  LyricService();
  explicit LyricService(LyricHttpGet httpGet, LyricClock clock = {});

  nlohmann::json Search(std::string hash) const;
  nlohmann::json GetDetail(std::string id, std::string accessKey) const;

 private:
  struct Cache;
  LyricHttpGet httpGet_;
  LyricClock clock_;
  std::shared_ptr<Cache> cache_;
};

}  // namespace echo::core
