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
    const DeviceInfo& device,
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
      {"appid", "3116"},
      {"clientver", "11440"},
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
    auto json = nlohmann::json::parse(result.body);
    json["debug_url"] = url;
    return json;
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error: ") + e.what());
  }
}

nlohmann::json UserService::GetUserVip(
    const DeviceInfo& device,
    const std::string& userId,
    const std::string& token) const {
  const auto clienttime = std::to_string(std::time(nullptr));
  // Android-signed requests need the full device fingerprint; KuGou rejects
  // the request silently (status:0 / empty data) when dfid/mid/uuid are absent.
  std::unordered_map<std::string, std::string> params = {
      {"appid", "3116"},
      {"clientver", "11440"},
      {"clienttime", clienttime},
      {"plat", "1"},
      {"busi_type", "concept"},
      {"dfid", device.dfid.empty() ? "-" : device.dfid},
      {"mid", device.mid.empty() ? "0" : device.mid},
      {"uuid", device.uuid.empty() ? "-" : device.uuid},
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
    const DeviceInfo& device,
    const std::string& userId,
    const std::string& token) const {
  if (userId.empty() || userId == "0" || token.empty()) {
    return MakeError("not logged in");
  }

  const auto nowEpoch = std::time(nullptr);
  const auto clienttime = std::to_string(nowEpoch);

  // KuGou expects receive_day as today's date in YYYY-MM-DD form (error_code
  // 304001 "鏃ユ湡鏍煎紡閿欒" comes back for raw integers).
  std::string receiveDay;
  {
    std::tm tmNow{};
#if defined(_WIN32)
    localtime_s(&tmNow, &nowEpoch);
#else
    tmNow = *std::localtime(&nowEpoch);
#endif
    std::ostringstream ds;
    ds << (tmNow.tm_year + 1900) << '-'
       << std::setw(2) << std::setfill('0') << (tmNow.tm_mon + 1) << '-'
       << std::setw(2) << std::setfill('0') << tmNow.tm_mday;
    receiveDay = ds.str();
  }

  // KuGouMusicApi reference (module/youth_day_vip.js + util/request.js):
  //   POST /youth/v1/recharge/receive_vip_listen_song
  //   鈥?encryptType=android => params live in URL query string, NOT body
  //   鈥?android defaults inject dfid, mid, uuid, appid, clientver, clienttime
  //   鈥?signature = md5(salt + sorted(k=v) + body + salt) where body="" here
  std::unordered_map<std::string, std::string> params = {
      {"appid", "3116"},
      {"clientver", "11440"},
      {"clienttime", clienttime},
      {"plat", "1"},
      {"dfid", device.dfid.empty() ? "-" : device.dfid},
      {"mid", device.mid.empty() ? "0" : device.mid},
      {"uuid", device.uuid.empty() ? "-" : device.uuid},
      {"userid", userId},
      {"token", token},
      {"source_id", "90139"},
      {"receive_day", receiveDay},
  };

  const std::string url = BuildSignedQueryString(
      "https://gateway.kugou.com/youth/v1/recharge/receive_vip_listen_song", params);

  const auto result = httpPost_(
      url,
      /*body=*/"",
      {
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
      });

  if (!result.error.empty()) return MakeError(result.error, result.statusCode);

  try {
    auto json = nlohmann::json::parse(result.body);
    // Normalize the response so the UI always sees a usable shape.
    // KuGou's success response is `{status:1, data:{...}, error_msg:""}`.
    // On failure it returns `{status:0, error_msg:"浠婃棩宸查鍙? / "骞垮憡鏈鐪? / ...}`.
    if (json.value("status", 0) != 1) {
      std::string msg = json.value("error_msg", json.value("error", std::string{}));
      if (msg.empty()) msg = "广告 VIP 升级失败（需要酷狗官方 App 内的广告 SDK 凭证）";
      return {
          {"status", 0},
          {"error", msg},
          {"error_code", "kugou_vip_claim_failed"},
          {"data", json.contains("data") ? json["data"] : nlohmann::json(nullptr)},
          {"raw", json},
      };
    }
    return json;
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error: ") + e.what());
  }
}

nlohmann::json UserService::GetUserDetail(
    const std::string& userId, const std::string& token) const {
  return GetUserDetail(DeviceInfo{}, userId, token);
}

nlohmann::json UserService::GetUserVip(
    const std::string& userId, const std::string& token) const {
  return GetUserVip(DeviceInfo{}, userId, token);
}

nlohmann::json UserService::ClaimVip(
    const std::string& userId, const std::string& token) const {
  return ClaimVip(DeviceInfo{}, userId, token);
}

nlohmann::json UserService::UpgradeVipReward(
    const DeviceInfo& device,
    const std::string& userId,
    const std::string& token) const {
  if (userId.empty() || userId == "0" || token.empty()) {
    return MakeError("not logged in");
  }

  const auto clienttime = std::to_string(std::time(nullptr));

  // Reference (MakcRe/KuGouMusicApi module/youth_day_vip_upgrade.js):
  //   POST /youth/v1/listen_song/upgrade_vip_reward
  //   params: kugouid=<userid>, ad_type=1
  // android encryptType => params live in URL query, no body.
  std::unordered_map<std::string, std::string> params = {
      {"appid", "3116"},
      {"clientver", "11440"},
      {"clienttime", clienttime},
      {"plat", "1"},
      {"dfid", device.dfid.empty() ? "-" : device.dfid},
      {"mid", device.mid.empty() ? "0" : device.mid},
      {"uuid", device.uuid.empty() ? "-" : device.uuid},
      {"userid", userId},
      {"token", token},
      {"kugouid", userId},
      {"ad_type", "1"},
  };

  const std::string url = BuildSignedQueryString(
      "https://gateway.kugou.com/youth/v1/listen_song/upgrade_vip_reward", params);

  const auto result = httpPost_(
      url,
      /*body=*/"",
      {
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
      });

  if (!result.error.empty()) return MakeError(result.error, result.statusCode);

  try {
    auto json = nlohmann::json::parse(result.body);
    if (json.value("status", 0) != 1) {
      std::string msg = json.value("error_msg", json.value("error", std::string{}));
      if (msg.empty()) msg = "广告 VIP 升级失败（需要酷狗官方 App 内的广告 SDK 凭证）";
      return {
          {"status", 0},
          {"error", msg},
          {"error_code", "kugou_vip_upgrade_failed"},
          {"raw", json},
      };
    }
    return json;
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error: ") + e.what());
  }
}

}  // namespace echo::core
