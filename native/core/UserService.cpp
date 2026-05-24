#include "echo/core/UserService.h"
#include "echo/core/Crypto.h"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace echo::core {
namespace {

std::string BuildSignedQueryString(
    const std::string& baseUrl,
    std::unordered_map<std::string, std::string> params,
    const std::string& body = "") {
  params["signature"] = SignatureAndroidParams(params, body);
  std::ostringstream urlStream;
  urlStream << baseUrl << "?";
  bool first = true;
  for (const auto& [key, value] : params) {
    if (!first) urlStream << "&";
    urlStream << key << "=" << value;
    first = false;
  }
  return urlStream.str();
}

nlohmann::json MakeError(const std::string& message, long statusCode = 0) {
  return {
      {"status", 0},
      {"error", message},
      {"status_code", statusCode},
      {"data", nullptr},
  };
}

}  // namespace

UserService::UserService()
    : UserService(
          [](const std::string& url,
             const std::unordered_map<std::string, std::string>& headers) {
            HttpClient client;
            return client.Get(url, headers);
          },
          [](const std::string& url,
             const std::string& body,
             const std::unordered_map<std::string, std::string>& headers) {
            HttpClient client;
            return client.Post(url, body, headers);
          }) {}

UserService::UserService(UserHttpPost httpPost)
    : UserService(
          [](const std::string& url,
             const std::unordered_map<std::string, std::string>& headers) {
            HttpClient client;
            return client.Get(url, headers);
          },
          std::move(httpPost)) {}

UserService::UserService(UserHttpGet httpGet, UserHttpPost httpPost)
    : httpGet_(std::move(httpGet)), httpPost_(std::move(httpPost)) {}

nlohmann::json UserService::GetUserDetail(
    const std::string& userId,
    const std::string& token) const {
  if (userId.empty() || userId == "0" || token.empty()) {
    return MakeError("not logged in");
  }

  const auto clienttime = std::to_string(std::time(nullptr));

  // Build RSA-encrypted `p` parameter: {"token":"...","clienttime":<ts>}
  // Matches JS: cryptoRSAEncrypt({ token, clienttime: clienttime_ms }).toUpperCase()
  std::ostringstream payloadStream;
  payloadStream << "{\"token\":\"" << token
                << "\",\"clienttime\":" << clienttime << "}";
  const std::string rsaPayload = payloadStream.str();
  const std::string pk = RsaRawEncrypt(rsaPayload);

  // POST body as JSON.
  nlohmann::json dataPayload = {
      {"visit_time", std::stoll(clienttime)},
      {"usertype", 1},
      {"p", pk},
      {"userid", std::stoll(userId)},
  };
  const std::string body = dataPayload.dump();

  // Android-signed query params.
  std::unordered_map<std::string, std::string> params = {
      {"appid", "1014"},
      {"clientver", "20000"},
      {"clienttime", clienttime},
      {"plat", "1"},
      {"userid", userId},
      {"token", token},
  };
  const std::string url = BuildSignedQueryString(
      "https://gateway.kugou.com/v3/get_my_info", params, body);

  const auto result = httpPost_(
      url,
      body,
      {
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
          {"x-router", "usercenter.kugou.com"},
      });

  if (!result.error.empty()) return MakeError(result.error, result.statusCode);

  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error: ") + e.what());
  }
}

nlohmann::json UserService::GetUserVip(
    const std::string& userId,
    const std::string& token) const {
  const auto clienttime = std::to_string(std::time(nullptr));
  std::unordered_map<std::string, std::string> params = {
      {"appid", "1014"},
      {"clientver", "20000"},
      {"clienttime", clienttime},
      {"plat", "1"},
      {"busi_type", "concept"},
  };
  if (!userId.empty() && userId != "0") params["userid"] = userId;
  if (!token.empty()) params["token"] = token;

  const std::string url = BuildSignedQueryString(
      "https://kugouvip.kugou.com/v1/get_union_vip", params);

  const auto result = httpGet_(
      url,
      {
          {"Accept", "application/json"},
          {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
      });

  if (!result.error.empty()) return MakeError(result.error, result.statusCode);

  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error: ") + e.what());
  }
}

nlohmann::json UserService::ClaimVip(
    const std::string& userId,
    const std::string& token) const {
  if (userId.empty() || userId == "0" || token.empty()) {
    return MakeError("not logged in");
  }

  const auto clienttime = std::to_string(std::time(nullptr));
  
  nlohmann::json dataPayload = {
      {"source_id", 90139},
      {"receive_day", 1},
  };
  const std::string body = dataPayload.dump();

  std::unordered_map<std::string, std::string> params = {
      {"appid", "1014"},
      {"clientver", "20000"},
      {"clienttime", clienttime},
      {"plat", "1"},
      {"userid", userId},
      {"token", token},
  };

  const std::string url = BuildSignedQueryString(
      "https://gateway.kugou.com/youth/v1/recharge/receive_vip_listen_song", params, body);

  const auto result = httpPost_(
      url,
      body,
      {
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
      });

  if (!result.error.empty()) return MakeError(result.error, result.statusCode);

  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error: ") + e.what());
  }
}

}  // namespace echo::core
