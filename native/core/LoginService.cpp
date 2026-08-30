#include "echo/core/LoginService.h"
#include "echo/core/Crypto.h"
#include "echo/core/DeviceService.h"
#include "echo/core/KuGouProfile.h"
#include "echo/core/StringUtils.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace echo::core {
namespace {


std::string BuildSignedUrl(
    const std::string& baseUrl,
    const std::unordered_map<std::string, std::string>& params) {
  const std::string signature = SignatureWebParams(params);
  std::ostringstream urlStream;
  urlStream << baseUrl << "?";
  bool first = true;
  for (const auto& [key, value] : params) {
    if (!first) urlStream << "&";
    urlStream << key << "=" << UrlEncode(value);
    first = false;
  }
  urlStream << "&signature=" << signature;
  return urlStream.str();
}

nlohmann::json MakeErrorJson(const std::string& errorMsg, long statusCode = 0) {
  return {
      {"status", 0},
      {"error", errorMsg},
      {"status_code", statusCode}
  };
}

}  // namespace

LoginService::LoginService()
    : LoginService([](
          const std::string& url,
          const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        return client.Get(url, headers);
      }) {}

LoginService::LoginService(LoginHttpGet httpGet) : httpGet_(std::move(httpGet)) {}

nlohmann::json LoginService::BeginQrLogin(const DeviceInfo& device) const {
  const auto profile = GetKuGouProfile(KuGouEdition::Standard);
  // For /v2/qrcode, KuGou expects appid=1001 or 1014 in the GET parameters,
  // but qrcode_txt must carry the standard Android app id so the mobile app
  // authorizes the token for standard Android endpoints (like /v7/get_all_list).
  // 注意：曾实验切换 Concept(3116) 以兑现概念版 VIP，但该假设未经隔离验证，
  // 且会导致老 Standard token 与新 Concept token 行为分裂——回退为已验证的 Standard。
  std::unordered_map<std::string, std::string> params = {
      {"appid", QrLoginAppId},
      {"clientver", profile.clientver},
      {"type", "1"},
      {"plat", "4"},
      {"qrcode_txt", "https://h5.kugou.com/apps/loginQRCode/html/index.html?appid=" +
                         profile.appid + "&"},
      {"srcappid", "2919"},
      {"clienttime", std::to_string(std::time(nullptr))},
      {"mid", ResolveAndroidMid(device)},
      {"uuid", "-"},
      {"dfid", device.dfid}
  };

  const std::string url = BuildSignedUrl("https://login-user.kugou.com/v2/qrcode", params);

  const auto result = httpGet_(
      url,
      {
          {"Accept", "application/json"},
          {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"},
      });

  if (!result.error.empty()) {
    return MakeErrorJson(result.error, result.statusCode);
  }

  try {
    auto j = nlohmann::json::parse(result.body);
    // 移除 debug_url，因为它包含设备指纹和签名，会泄露安全信息
    // j["debug_url"] = url;
    
    // 添加脱敏的调试信息（不含敏感参数）
    #ifdef _DEBUG
    nlohmann::json debug_info = {
      {"endpoint", "qrcode"},
      {"status", j.value("status", 0)}
    };
    j["_debug"] = debug_info;
    #endif
    
    return j;
  } catch (const nlohmann::json::exception& e) {
    return MakeErrorJson(std::string("JSON parse error: ") + e.what(), result.statusCode);
  }
}

nlohmann::json LoginService::PollQrLogin(const DeviceInfo& device, const std::string& key) const {
  const auto profile = GetKuGouProfile(KuGouEdition::Standard);
  std::unordered_map<std::string, std::string> params = {
      {"plat", "4"},
      {"appid", profile.appid},
      {"clientver", profile.clientver},
      {"qrcode", key},
      {"srcappid", "2919"},
      {"clienttime", std::to_string(std::time(nullptr))},
      {"mid", ResolveAndroidMid(device)},
      {"uuid", "-"},
      {"dfid", device.dfid}
  };

  const std::string url = BuildSignedUrl("https://login-user.kugou.com/v2/get_userinfo_qrcode", params);

  const auto result = httpGet_(
      url,
      {
          {"Accept", "application/json"},
          {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"},
      });

  if (!result.error.empty()) {
    return MakeErrorJson(result.error, result.statusCode);
  }

  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return MakeErrorJson(std::string("JSON parse error: ") + e.what(), result.statusCode);
  }
}

}  // namespace echo::core
