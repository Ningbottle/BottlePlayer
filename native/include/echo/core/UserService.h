#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"

namespace echo::core {

using UserHttpGet = std::function<HttpResult(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers)>;

using UserHttpPost = std::function<HttpResult(
    const std::string& url,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& headers)>;

// Retrieves KuGou user profile and VIP status.
// Both methods require a valid session token and user ID obtained after login.
class UserService {
 public:
  // Default: uses real HTTP client.
  UserService();
  // Injectable constructors for testing.
  explicit UserService(UserHttpPost httpPost);
  UserService(UserHttpGet httpGet, UserHttpPost httpPost);

  // Calls POST https://gateway.kugou.com/v3/get_my_info (x-router: usercenter.kugou.com).
  // Returns normalised user profile: nickname, avatar, fan_count, follow_count, etc.
  nlohmann::json GetUserDetail(const std::string& userId, const std::string& token) const;

  // Calls GET https://kugouvip.kugou.com/v1/get_union_vip.
  // Returns VIP status, type and expiry timestamps.
  nlohmann::json GetUserVip(const std::string& userId, const std::string& token) const;

  // Claims 1-day VIP automatically for the user.
  // Calls POST https://gateway.kugou.com/youth/v1/recharge/receive_vip_listen_song.
  nlohmann::json ClaimVip(const std::string& userId, const std::string& token) const;

 private:
  UserHttpGet httpGet_;
  UserHttpPost httpPost_;
};

}  // namespace echo::core
