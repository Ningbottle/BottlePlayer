#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"

namespace echo::core {

using LyricHttpGet = std::function<HttpResult(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers)>;

class LyricService {
 public:
  LyricService();
  explicit LyricService(LyricHttpGet httpGet);

  nlohmann::json Search(std::string hash) const;
  nlohmann::json GetDetail(std::string id, std::string accessKey) const;

 private:
  LyricHttpGet httpGet_;
};

}  // namespace echo::core
