#include "echo/core/LoginService.h"
#include "echo/core/Crypto.h"
#include "echo/core/DeviceService.h"
#include "echo/core/KuGouAndroidRequest.h"
#include "echo/core/KuGouProfile.h"
#include "echo/core/StringUtils.h"
#include "echo/diagnostics/EchoDiagnostics.h"
#include "echo/diagnostics/Redaction.h"

#include <chrono>
#include <cstdlib>
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

std::string LoginService::RefreshVipToken(
    const DeviceInfo& device,
    const std::string& userId,
    const std::string& token) const {
  using namespace std::chrono;
  const auto ms = duration_cast<milliseconds>(
      system_clock::now().time_since_epoch()).count();
  const long long sec = ms / 1000;

  // p3 = AES({clienttime, token})，固定 key/iv（login_token.js 同约）。
  const std::string p3 = AesCbcEncryptBase64(
      "{\"clienttime\":" + std::to_string(sec) + ",\"token\":\"" + token + "\"}",
      "90b8382a1bb4ccdcf063102053fd75b8", "f063102053fd75b8");

  // encryptParams = AES({}, md5(tempKey)[0:32], 后16位)；tempKey 同时进 RSA 包装。
  static const char* kChars = "abcdefghijklmnopqrstuvwxyz0123456789";
  std::string tempKey;
  tempKey.reserve(16);
  for (int i = 0; i < 16; ++i) tempKey += kChars[std::rand() % 36];
  const std::string md5 = CalculateMd5(tempKey);
  const std::string aesKey = md5.substr(0, 32);
  const std::string aesIv = aesKey.substr(16, 16);
  const std::string paramsStr = AesCbcEncryptBase64("{}", aesKey, aesIv);
  const std::string pk = RsaRawEncryptRef(
      "{\"clienttime_ms\":" + std::to_string(ms) + ",\"key\":\"" + tempKey + "\"}");
  if (p3.empty() || paramsStr.empty() || pk.empty()) {
    ECHO_LOG("VipToken", "crypto prep failed");
    return {};
  }

  nlohmann::json bodyJson = {
      {"dfid", device.dfid.empty() ? "-" : device.dfid},
      {"p3", p3},
      {"plat", 1},
      {"t1", 0},
      {"t2", 0},
      {"t3", "MCwwLDAsMCwwLDAsMCwwLDA="},
      {"pk", pk},
      {"params", paramsStr},
      {"userid", [userId] {
        try { return std::stoi(userId); } catch (...) { return 0; }
      }()},
      {"clienttime_ms", ms},
  };
  const std::string body = bodyJson.dump();

  KuGouAndroidRequest req;
  req.endpoint = "http://login.user.kugou.com/v5/login_by_token";
  req.profile = GetKuGouProfile(KuGouEdition::Standard);
  req.device = device;
  req.body = body;
  if (!userId.empty()) req.params["userid"] = userId;
  if (!token.empty()) req.params["token"] = token;
  const std::string url = BuildSignedUrl(req);

  HttpClient client;
  const auto result = client.Post(url, body, {
      {"Content-Type", "application/json"},
      {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
  });
  if (!result.error.empty() || result.statusCode < 200 || result.statusCode >= 300) {
    ECHO_LOG("VipToken", std::string("login_by_token http failed: ") + result.error);
    return {};
  }
  try {
    auto json = nlohmann::json::parse(result.body);
    if (json.value("status", 0) != 1 || !json.contains("data") ||
        !json["data"].is_object()) {
      ECHO_LOG("VipToken", "login_by_token status!=1 body=" +
          diagnostics::TruncateForLog(diagnostics::RedactSensitive(result.body)));
      return {};
    }
    auto& data = json["data"];
    if (data.contains("secu_params") && data["secu_params"].is_string()) {
      const auto decrypted = PlaylistAesDecrypt(
          data["secu_params"].get<std::string>(), tempKey);
      auto inner = nlohmann::json::parse(decrypted, nullptr, false);
      if (!inner.is_discarded() && inner.is_object() &&
          inner.contains("vip_token") && inner["vip_token"].is_string() &&
          !inner["vip_token"].get<std::string>().empty()) {
        return inner["vip_token"].get<std::string>();
      }
    }
    if (data.contains("vip_token") && data["vip_token"].is_string()) {
      return data["vip_token"].get<std::string>();
    }
    ECHO_LOG("VipToken", "login_by_token ok but no vip_token in response");
  } catch (const nlohmann::json::exception&) {
    ECHO_LOG("VipToken", "login_by_token bad json");
  }
  return {};
}

}  // namespace echo::core
