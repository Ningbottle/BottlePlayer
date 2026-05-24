#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"

namespace echo::core {

using UserCloudHttpPost = std::function<HttpResult(
    const std::string& url,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& headers)>;

class UserCloudService {
 public:
  UserCloudService();
  explicit UserCloudService(UserCloudHttpPost httpPost);

  // Calls POST https://mcloudservice.kugou.com/v1/get_list
  nlohmann::json GetList(
      const std::string& userId,
      const std::string& token,
      int page = 1,
      int pageSize = 30) const;

 private:
  UserCloudHttpPost httpPost_;
};

}  // namespace echo::core
