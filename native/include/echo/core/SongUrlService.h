#pragma once

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

class SongUrlService {
 public:
  SongUrlService();
  explicit SongUrlService(SongUrlHttpGet httpGet);
  SongUrlService(SongUrlHttpGet httpGet, SongUrlHttpPost httpPost);

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

  // Convenience overload for tests / simple callers — uses empty quality/auth/device defaults.
  nlohmann::json Resolve(std::string hash, std::string albumId, std::string albumAudioId) const;

 private:
  SongUrlHttpGet httpGet_;
  SongUrlHttpPost httpPost_;
};

}  // namespace echo::core
