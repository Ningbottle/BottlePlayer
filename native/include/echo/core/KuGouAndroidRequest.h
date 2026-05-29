#pragma once
#include <string>
#include <map>
#include <unordered_map>
#include "echo/core/KuGouProfile.h"
#include "echo/core/Dto.h"

namespace echo::core {

struct KuGouAndroidRequest {
  std::string endpoint;
  KuGouProfileParams profile;
  bool includeSongUrlKey = false;
  std::map<std::string, std::string> params;
  DeviceInfo device;
  std::string body;
};

std::string ResolveAndroidMid(const DeviceInfo& device);
std::string BuildSignedUrl(const KuGouAndroidRequest& req);
std::unordered_map<std::string, std::string> BuildAndroidHeaders(const KuGouAndroidRequest& req);

}
