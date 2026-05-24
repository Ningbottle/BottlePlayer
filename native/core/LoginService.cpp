#include "echo/core/LoginService.h"
#include "echo/core/Crypto.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace echo::core {
namespace {

std::string UrlEncode(std::string_view value) {
  std::ostringstream stream;
  stream << std::uppercase << std::hex;
  for (const unsigned char ch : value) {
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
        ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      stream << static_cast<char>(ch);
    } else {
      stream << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }
  }
  return stream.str();
}

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
  std::unordered_map<std::string, std::string> params = {
      {"appid", device.appid},
      {"type", "1"},
      {"plat", "4"},
      {"qrcode_txt", "https://h5.kugou.com/apps/loginQRCode/html/index.html?appid=" + device.appid + "&"},
      {"srcappid", "2919"},
      {"clienttime", std::to_string(std::time(nullptr))},
      {"mid", device.mid},
      {"uuid", device.uuid},
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
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return MakeErrorJson(std::string("JSON parse error: ") + e.what(), result.statusCode);
  }
}

nlohmann::json LoginService::PollQrLogin(const DeviceInfo& device, const std::string& key) const {
  std::unordered_map<std::string, std::string> params = {
      {"plat", "4"},
      {"appid", device.appid},
      {"qrcode", key},
      {"srcappid", "2919"},
      {"clienttime", std::to_string(std::time(nullptr))},
      {"mid", device.mid},
      {"uuid", device.uuid},
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
