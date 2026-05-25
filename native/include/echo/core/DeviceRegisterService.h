#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/Dto.h"
#include "echo/core/HttpClient.h"

namespace echo::core {

using DeviceRegisterHttpPost = std::function<HttpResult(
    const std::string& url,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& headers)>;

// Registers the current device with KuGou's risk service so that subsequent
// endpoints (/song/url, /user/playlist, /user/vip/detail upstream) accept us
// as a trusted device. Returns a KuGou-issued `dfid` on success.
//
// Reference: MakcRe/KuGouMusicApi module/register_dev.js
//   POST https://userservice.kugou.com/risk/v2/r_register_dev
//        ?part=1&platid=1&p=<RSA-PKCS1-v1.5({aes,uid,token})>
//   Body: AES-CBC base64 of device fingerprint JSON
//   Response: binary → base64 → AES-CBC decrypt with our key → {status, data:{dfid}}
class DeviceRegisterService {
 public:
  DeviceRegisterService();
  explicit DeviceRegisterService(DeviceRegisterHttpPost httpPost);

  // Calls /risk/v2/r_register_dev with the given device + auth.
  // On success, returns the new `dfid` (32 hex chars from KuGou).
  // On failure, returns empty string; details available in `error`.
  std::string Register(
      const DeviceInfo& device,
      const std::string& userId,
      const std::string& token,
      std::string* error = nullptr) const;

 private:
  DeviceRegisterHttpPost httpPost_;
};

}  // namespace echo::core
