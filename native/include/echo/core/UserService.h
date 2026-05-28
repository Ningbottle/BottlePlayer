#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"

#include "echo/core/Dto.h"

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
  nlohmann::json GetUserDetail(const DeviceInfo& device, const std::string& userId, const std::string& token) const;
  nlohmann::json GetUserDetail(const std::string& userId, const std::string& token) const;

  // Calls GET https://kugouvip.kugou.com/v1/get_union_vip.
  // Returns VIP status, type and expiry timestamps.
  nlohmann::json GetUserVip(const DeviceInfo& device, const std::string& userId, const std::string& token) const;
  nlohmann::json GetUserVip(const std::string& userId, const std::string& token) const;

  // Claims 1-day VIP automatically for the user.
  // Calls POST https://gateway.kugou.com/youth/v1/recharge/receive_vip_listen_song.
  // DEPRECATED: upstream consistently returns 51002 (ad SDK credential required).
  // Kept as archive point; callers should use ClaimYouthListenSong instead.
  [[deprecated("kugou_vip_legacy_disabled: use ClaimYouthListenSong")]]
  nlohmann::json ClaimVip(const DeviceInfo& device, const std::string& userId, const std::string& token) const;
  [[deprecated("kugou_vip_legacy_disabled: use ClaimYouthListenSong")]]
  nlohmann::json ClaimVip(const std::string& userId, const std::string& token) const;

  // Upgrade VIP reward — the "watch 5s ad then get listen-song VIP" endpoint.
  // Calls POST /youth/v1/listen_song/upgrade_vip_reward with ad_type=1.
  // KuGou validates this against an ad-completion token from its official
  // Android SDK; pure HTTP calls usually return error_code 131001/304xxx.
  // DEPRECATED: same reason as ClaimVip.
  [[deprecated("kugou_vip_legacy_disabled: use ClaimYouthListenSong")]]
  nlohmann::json UpgradeVipReward(const DeviceInfo& device, const std::string& userId, const std::string& token) const;

  // ── kgcheckin 概念版 m 端点（Cookie-only GET，不需要广告 SDK 凭证）──

  // GET https://m.kugou.com/youth/listen/song
  // 听歌领 VIP（每日一次）。Cookie-only 鉴权。
  nlohmann::json ClaimYouthListenSong(const std::string& userId, const std::string& token) const;

  // GET https://m.kugou.com/youth/vip
  // 看广告领 VIP（需循环调用，每次间隔 30s，最多 8 次）。Cookie-only 鉴权。
  nlohmann::json ClaimYouthAdVip(const std::string& userId, const std::string& token) const;

 private:
  UserHttpGet httpGet_;
  UserHttpPost httpPost_;
};

}  // namespace echo::core
