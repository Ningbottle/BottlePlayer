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
  // History: upstream answered 51002 for this account and the route was
  // disabled with a hardcoded reject. The reference repo (MakcRe/KuGouMusicApi
  // v1.6.0) was updated on 2026-08-31 to send content-type
  // application/x-www-form-urlencoded plus the standard Android fingerprint
  // headers, so the route was re-enabled on 2026-09-02 to retest upstream.
  nlohmann::json ClaimVip(const DeviceInfo& device, const std::string& userId, const std::string& token) const;
  nlohmann::json ClaimVip(const std::string& userId, const std::string& token) const;

  // Upgrade VIP reward — the "watch 5s ad then get listen-song VIP" endpoint.
  // Calls POST /youth/v1/listen_song/upgrade_vip_reward with ad_type=1.
  // Re-enabled 2026-09-02 together with ClaimVip; upstream may still validate
  // this against an ad-completion token from the official Android SDK.
  nlohmann::json UpgradeVipReward(const DeviceInfo& device, const std::string& userId, const std::string& token) const;

  // ── 酷狗音乐概念版 VIP 领取（gateway.kugou.com POST + Android signature）──

  // POST https://gateway.kugou.com/youth/v2/report/listen_song
  // 听歌领 VIP（每日一次）。需传入设备指纹生成 Android 签名，clientver 强指定为 10566。
  nlohmann::json ClaimYouthListenSong(const DeviceInfo& device, const std::string& userId, const std::string& token) const;
  nlohmann::json ClaimYouthListenSong(const std::string& userId, const std::string& token) const;

  // POST https://gateway.kugou.com/youth/v1/ad/play_report
  // 看广告领 VIP（需循环调用，每次间隔 30s，最多 8 次）。需传入设备指纹生成 Android 签名。
  nlohmann::json ClaimYouthAdVip(const DeviceInfo& device, const std::string& userId, const std::string& token) const;
  nlohmann::json ClaimYouthAdVip(const std::string& userId, const std::string& token) const;

 private:
  UserHttpGet httpGet_;
  UserHttpPost httpPost_;
};

}  // namespace echo::core
