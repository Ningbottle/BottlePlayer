#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"
#include "echo/core/Dto.h"

namespace echo::core {

using LoginHttpGet = std::function<HttpResult(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers)>;

using LoginHttpPost = std::function<HttpResult(
    const std::string& url,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& headers)>;

struct LoginRefreshResult {
  std::string token;
  std::string vipToken;
  std::string t1;
  int vipType = 0;
};

class LoginService {
 public:
  LoginService();
  explicit LoginService(LoginHttpGet httpGet);
  LoginService(LoginHttpGet httpGet, LoginHttpPost httpPost);

  nlohmann::json BeginQrLogin(const DeviceInfo& device) const;
  nlohmann::json PollQrLogin(const DeviceInfo& device, const std::string& key) const;

  // 刷新登录（MakcRe login_token.js 同约）：服务端会同时换发普通 token、
  // vip_token、vip_type 与 t1；这些字段必须作为一个会话整体保存。
  std::optional<LoginRefreshResult> RefreshSession(
      const DeviceInfo& device,
      const std::string& userId,
      const std::string& token,
      const std::string& t1 = {}) const;

  // Compatibility helper for callers that only need vip_token.
  std::string RefreshVipToken(const DeviceInfo& device,
                              const std::string& userId,
                              const std::string& token) const;

 private:
  LoginHttpGet httpGet_;
  LoginHttpPost httpPost_;
};

}  // namespace echo::core
