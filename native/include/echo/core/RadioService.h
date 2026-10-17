#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "echo/core/Dto.h"
#include "echo/core/HttpClient.h"

namespace echo::core {

struct RadioContext {
  std::string userId;
  std::string token;
  std::string hash;
  std::string songId;
  int playedSeconds = 0;
  int remainingSongs = 0;
  bool overplay = false;
  DeviceInfo device;
  std::string action = "play";
  int songPoolId = 0;
};

using RadioHttpPost = std::function<HttpResult(
    const std::string&, const std::string&,
    const std::unordered_map<std::string, std::string>&)>;

// Continuous recommendation is independent from the daily homepage snapshot.
// The upstream accepts context; this service makes no promise about account weights.
class RadioService {
 public:
  RadioService();
  explicit RadioService(RadioHttpPost httpPost);
  nlohmann::json Recommend(const RadioContext& context) const;
 private:
  RadioHttpPost httpPost_;
};
}  // namespace echo::core
