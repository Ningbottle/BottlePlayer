#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"
#include "echo/core/Dto.h"

namespace echo::core {

using SongUrlHttpGet = std::function<HttpResult(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers)>;

using SongUrlHttpPost = std::function<HttpResult(
    const std::string& url,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& headers)>;

// Timeout-aware transports let a Resolve() fallback chain pass one shared
// request deadline through each upstream leg. The legacy mock signatures stay
// available below and are adapted without changing existing tests/callers.
using SongUrlHttpGetWithTimeout = std::function<HttpResult(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers,
    long totalTimeoutMs)>;

using SongUrlHttpPostWithTimeout = std::function<HttpResult(
    const std::string& url,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& headers,
    long totalTimeoutMs)>;

using SongUrlClockNow = std::function<std::chrono::steady_clock::time_point()>;

class SongUrlService {
 public:
  SongUrlService();
  explicit SongUrlService(SongUrlHttpGet httpGet);
  SongUrlService(SongUrlHttpGet httpGet, SongUrlHttpPost httpPost);
  SongUrlService(SongUrlHttpGetWithTimeout httpGet,
                 SongUrlHttpPostWithTimeout httpPost,
                 SongUrlClockNow now);

  // POST /v6/priv_url — VIP-aware endpoint via tracker.kugou.com (HTTP, not HTTPS).
  // Returns the same normalized shape as Resolve().  Falls back internally on
  // HTTP errors so callers always get a usable JSON object.
  nlohmann::json ResolveV6PrivUrl(
      std::string hash,
      std::string albumAudioId,
      std::string userId,
      std::string token,
      std::string vipToken,
      int vipType,
      const DeviceInfo& device) const;

  nlohmann::json Resolve(
      std::string hash,
      std::string albumId,
      std::string albumAudioId,
      std::string quality,
      std::string ppageId,
      std::string userId,
      std::string token,
      const DeviceInfo& device,
      std::string vipToken = "") const;

  nlohmann::json Resolve(
      std::string hash,
      std::string albumId,
      std::string albumAudioId,
      std::string quality,
      std::string ppageId,
      std::string userId,
      std::string token,
      const DeviceInfo& device,
      std::string vipToken,
      int vipType) const;

  // Convenience overload for tests / simple callers — uses empty quality/auth/device defaults.
  nlohmann::json Resolve(std::string hash, std::string albumId, std::string albumAudioId) const;

 private:
  nlohmann::json ResolveV6PrivUrlWithTimeout(
      std::string hash,
      std::string albumAudioId,
      std::string userId,
      std::string token,
      std::string vipToken,
      int vipType,
      const DeviceInfo& device,
      long stageBudgetMs,
      std::chrono::steady_clock::time_point deadline) const;

  SongUrlHttpGetWithTimeout httpGet_;
  SongUrlHttpPostWithTimeout httpPost_;
  SongUrlClockNow now_;
};

}  // namespace echo::core
