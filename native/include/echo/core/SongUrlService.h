#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"

namespace echo::core {

using SongUrlHttpGet = std::function<HttpResult(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers)>;

class SongUrlService {
 public:
  SongUrlService();
  explicit SongUrlService(SongUrlHttpGet httpGet);

  nlohmann::json Resolve(std::string hash, std::string quality, std::string ppageId = "") const;

 private:
  SongUrlHttpGet httpGet_;
};

}  // namespace echo::core
