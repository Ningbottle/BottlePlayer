#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "echo/core/LoginService.h"

namespace {

std::string QueryValue(const std::string& url, const std::string& key) {
  const auto marker = key + "=";
  const auto start = url.find(marker);
  if (start == std::string::npos) return {};
  const auto valueStart = start + marker.size();
  const auto end = url.find('&', valueStart);
  return url.substr(valueStart, end == std::string::npos ? std::string::npos
                                                         : end - valueStart);
}

}  // namespace

int main() {
  std::vector<std::string> urls;
  echo::core::LoginService service(
      [&](const std::string& url,
          const std::unordered_map<std::string, std::string>&) {
        urls.push_back(url);
        return echo::core::HttpResult{200, R"({"status":1})", ""};
      });

  echo::core::DeviceInfo device;
  device.dfid = "abcdefghijklmnopqrstuvwx";
  device.mid = "123456789012345678901234567890123456789";
  device.clientver = "11440";

  service.BeginQrLogin(device);
  service.PollQrLogin(device, "qr-key");

  if (urls.size() != 2) {
    std::cerr << "[LoginContract] expected two requests" << std::endl;
    return 1;
  }

  const bool beginMatchesReference =
      urls[0].find("https://login-user.kugou.com/v2/qrcode?") == 0 &&
      QueryValue(urls[0], "appid") == "1001" &&
      QueryValue(urls[0], "clientver") == "20489" &&
      QueryValue(urls[0], "qrcode_txt").find("appid%3D1005") != std::string::npos;
  const bool pollMatchesReference =
      urls[1].find("https://login-user.kugou.com/v2/get_userinfo_qrcode?") == 0 &&
      QueryValue(urls[1], "appid") == "1005" &&
      QueryValue(urls[1], "clientver") == "20489";

  if (!beginMatchesReference || !pollMatchesReference) {
    std::cerr << "[LoginContract] QR login must mint a standard Android token"
              << std::endl;
    return 1;
  }

  std::cout << "[LoginContract] passed" << std::endl;
  return 0;
}
