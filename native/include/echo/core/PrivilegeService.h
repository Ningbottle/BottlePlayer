#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"

namespace echo::core {

using PrivilegeHttpGet = std::function<HttpResult(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers)>;

class PrivilegeService {
 public:
  PrivilegeService();
  explicit PrivilegeService(PrivilegeHttpGet httpGet);

  nlohmann::json GetLite(std::string hash, std::string albumId = "") const;

 private:
  PrivilegeHttpGet httpGet_;
};

}  // namespace echo::core
