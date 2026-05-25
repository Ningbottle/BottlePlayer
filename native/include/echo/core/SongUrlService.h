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

class SongUrlService {
 public:
  SongUrlService();
  explicit SongUrlService(SongUrlHttpGet httpGet);

  nlohmann::json Resolve(
      std::string hash,
      std::string albumId,
      std::string albumAudioId,
      std::string quality,
      std::string ppageId,
      std::string userId,
      std::string token,
      const DeviceInfo& device) const;

  // Convenience overload for tests / simple callers — uses empty quality/auth/device defaults.
  nlohmann::json Resolve(std::string hash, std::string albumId, std::string albumAudioId) const;

 private:
  SongUrlHttpGet httpGet_;
};

}  // namespace echo::core
