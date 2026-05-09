#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"

namespace echo::core {

using RankHttpGet = std::function<HttpResult(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers)>;

class RankService {
 public:
  RankService();
  explicit RankService(RankHttpGet httpGet);

  nlohmann::json List() const;
  nlohmann::json GetSongs(int rankId, int page, int pageSize) const;

 private:
  RankHttpGet httpGet_;
};

}  // namespace echo::core
