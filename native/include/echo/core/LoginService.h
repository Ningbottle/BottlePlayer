#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"
#include "echo/core/Dto.h"

namespace echo::core {

using LoginHttpGet = std::function<HttpResult(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers)>;

class LoginService {
 public:
  LoginService();
  explicit LoginService(LoginHttpGet httpGet);

  nlohmann::json BeginQrLogin(const DeviceInfo& device) const;
  nlohmann::json PollQrLogin(const DeviceInfo& device, const std::string& key) const;

 private:
  LoginHttpGet httpGet_;
};

}  // namespace echo::core
