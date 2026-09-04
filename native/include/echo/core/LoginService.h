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

  // 刷新登录（MakcRe login_token.js 同约）：用当前 token 换发 vip_token，
  // v6/priv_url 需要它才下发会员音质。失败返回空串。
  std::string RefreshVipToken(const DeviceInfo& device,
                              const std::string& userId,
                              const std::string& token) const;

 private:
  LoginHttpGet httpGet_;
};

}  // namespace echo::core
