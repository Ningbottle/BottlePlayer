#include "echo/core/UserService.h"
#include "echo/core/Crypto.h"
#include "echo/core/DeviceService.h"
#include "echo/core/KuGouAndroidRequest.h"
#include "echo/core/KuGouProfile.h"
#include "echo/diagnostics/EchoDiagnostics.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace echo::core {
namespace {

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

  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  KuGouAndroidRequest req;
  req.endpoint = "https://gateway.kugou.com/v3/get_my_info";
  req.profile = profile;
  req.params = {
      {"plat", "1"},
      {"userid", userId},
      {"token", token},
  };
  req.body = body;

  const std::string url = BuildSignedUrl(req);

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

  // Use the device fingerprint already normalized by DeviceService.
  // Do NOT re-derive from dfid here — that breaks the guid bloodline
  // required by concept-edition endpoints.
  const std::string dfid = device.dfid.empty() ? "-" : device.dfid;
  const std::string mid = ResolveAndroidMid(device);
  const std::string uuid = "-";

  auto DoGetVip = [&](const KuGouProfileParams& profile) -> nlohmann::json {
    KuGouAndroidRequest req;
    req.endpoint = "https://kugouvip.kugou.com/v1/get_union_vip";
    req.profile = profile;
    req.params = {
        {"busi_type", "concept"},
        {"userid", userId.empty() ? "0" : userId},
        {"token", token},
        {"opt_product_types", "dvip,qvip"},
        {"product_type", "svip"},
    };
    req.device = device;

    const std::string url = BuildSignedUrl(req);
    auto headers = BuildAndroidHeaders(req);

    const auto result = httpGet_(url, std::move(headers));

    if (!result.error.empty()) {
      ECHO_LOG("UserVip", std::string("network error: ") + result.error);
      return MakeError(result.error, result.statusCode);
    }

    try {
      auto json = nlohmann::json::parse(result.body);
      json["debug_profile_appid"] = profile.appid;
      return json;
    } catch (const nlohmann::json::exception& e) {
      return MakeError(std::string("JSON parse error: ") + e.what());
    }
  };

  auto conceptResult = DoGetVip(GetKuGouProfile(KuGouEdition::Concept));
  if (conceptResult.value("status", 0) == 1) {
    bool hasVipFields = false;
    if (conceptResult.contains("data") && conceptResult["data"].is_object()) {
      auto& d = conceptResult["data"];
      hasVipFields = d.contains("is_vip") || d.contains("busi_vip") || d.contains("vip_type");

      // 修正顶层 is_vip：busi_vip list 里有 is_vip=1 且未过期的记录时，data.is_vip 应为 1
      if (hasVipFields && d.contains("busi_vip") && d["busi_vip"].is_array()) {
        const auto now = std::time(nullptr);
        bool realVip = false;
        for (const auto& item : d["busi_vip"]) {
          if (item.value("is_vip", 0) != 1) continue;
          const std::string endTime = item.value("vip_end_time", "");
          if (endTime.empty()) {
            realVip = true;
            break;
          }
          std::tm tm{};
          std::istringstream ss(endTime);
          ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
          if (!ss.fail()) {
            auto itemTime = std::mktime(&tm);
            if (itemTime != -1 && itemTime > now) {
              realVip = true;
              break;
            }
          }
        }
        if (realVip) {
          d["is_vip"] = 1;
          ECHO_LOG("UserVip", "corrected is_vip to 1 (active busi_vip found)");
        }
      }
    }
    if (hasVipFields) {
      return conceptResult;
    }
  }

  return conceptResult;
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
  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  KuGouAndroidRequest req;
  req.endpoint = "https://gateway.kugou.com/youth/v1/recharge/receive_vip_listen_song";
  req.profile = profile;
  req.params = {
      {"plat", "1"},
      {"userid", userId},
      {"token", token},
      {"source_id", "90139"},
      {"receive_day", receiveDay},
  };
  req.device = device;

  const std::string url = BuildSignedUrl(req);

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
  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  KuGouAndroidRequest req;
  req.endpoint = "https://gateway.kugou.com/youth/v1/listen_song/upgrade_vip_reward";
  req.profile = profile;
  req.params = {
      {"plat", "1"},
      {"userid", userId},
      {"token", token},
      {"kugouid", userId},
      {"ad_type", "1"},
  };
  req.device = device;

  const std::string url = BuildSignedUrl(req);

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

nlohmann::json UserService::ClaimYouthListenSong(
    const std::string& userId, const std::string& token) const {
  return ClaimYouthListenSong(DeviceInfo{}, userId, token);
}

nlohmann::json UserService::ClaimYouthListenSong(
    const DeviceInfo& device, const std::string& userId, const std::string& token) const {
  if (userId.empty() || userId == "0" || token.empty()) {
    return MakeError("not logged in");
  }

  const std::string body = R"({"mixsongid":666075191})";

  // listen_song report uses a distinct clientver from the global concept profile.
  auto profile = GetKuGouProfile(KuGouEdition::Concept);
  profile.clientver = "10566";

  KuGouAndroidRequest req;
  req.endpoint = "https://gateway.kugou.com/youth/v2/report/listen_song";
  req.profile = profile;
  req.device = device;
  req.body = body;
  // Note: old code didn't send 'plat' param, so we don't add it here
  if (!userId.empty() && userId != "0") req.params["userid"] = userId;
  if (!token.empty()) req.params["token"] = token;

  const std::string url = BuildSignedUrl(req);
  const std::string cookie = "token=" + token + "; userid=" + userId + "; KugooID=" + userId;

  const auto result = httpPost_(
      url,
      body,
      {
          {"Cookie", cookie},
          {"Content-Type", "application/json; charset=utf-8"},
          {"User-Agent", "Android13-1070-10566-201-0-ReportPlaySongToServerProtocol-wifi"},
      });

  if (!result.error.empty()) {
    ECHO_LOG("YouthListen", std::string("network error: ") + result.error);
    return MakeError(result.error, result.statusCode);
  }

  try {
    auto json = nlohmann::json::parse(result.body);
    nlohmann::json out = {
        {"status", json.value("status", 0)},
        {"error_code", json.value("error_code", 0)},
        {"error_msg", json.value("error_msg", "")},
        {"data", json.contains("data") && json["data"].is_object() ? json["data"] : nlohmann::json::object()},
    };

    // 放宽拦截：为 130012 业务限制注入明确的提示，让前端不再只显示“网络异常”
    if (out["status"] == 0 && out["error_code"] == 130012 && out["error_msg"] == "") {
      out["error_msg"] = "今日已通过广告领过，或需要去酷狗官方 App 内先听完整歌曲 (Err: 130012)";
    }

    if (json.contains("data") && json["data"].is_object()) {
      auto& d = json["data"];
      out["data"]["ad_vip_end_time"] = d.value("ad_vip_end_time", 0);
      out["data"]["server_time"]     = d.value("server_time", std::time(nullptr));
      out["data"]["vip_end_time"]    = d.value("vip_end_time", "");
    }
    return out;
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error: ") + e.what());
  }
}

nlohmann::json UserService::ClaimYouthAdVip(
    const std::string& userId, const std::string& token) const {
  return ClaimYouthAdVip(DeviceInfo{}, userId, token);
}

nlohmann::json UserService::ClaimYouthAdVip(
    const DeviceInfo& device, const std::string& userId, const std::string& token) const {
  if (userId.empty() || userId == "0" || token.empty()) {
    return MakeError("not logged in");
  }

  const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();

  nlohmann::json bodyJson = {
      {"ad_id", 12307537187},
      {"play_end", nowMs},
      {"play_start", nowMs - 30000},
  };
  const std::string body = bodyJson.dump();

  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  KuGouAndroidRequest req;
  req.endpoint = "https://gateway.kugou.com/youth/v1/ad/play_report";
  req.profile = profile;
  req.device = device;
  req.body = body;
  // Note: old code didn't send 'plat' param, so we don't add it here
  if (!userId.empty() && userId != "0") req.params["userid"] = userId;
  if (!token.empty()) req.params["token"] = token;

  const std::string url = BuildSignedUrl(req);
  const std::string cookie = "token=" + token + "; userid=" + userId + "; KugooID=" + userId;

  const auto result = httpPost_(
      url,
      body,
      {
          {"Cookie", cookie},
          {"Content-Type", "application/json; charset=utf-8"},
          {"User-Agent", "Android13-1070-10566-201-0-ReportPlaySongToServerProtocol-wifi"},
      });

  if (!result.error.empty()) {
    ECHO_LOG("YouthAdVip", std::string("network error: ") + result.error);
    return MakeError(result.error, result.statusCode);
  }

  try {
    auto json = nlohmann::json::parse(result.body);
    nlohmann::json out = {
        {"status", json.value("status", 0)},
        {"error_code", json.value("error_code", 0)},
        {"error_msg", json.value("error_msg", "")},
        {"data", json.contains("data") && json["data"].is_object() ? json["data"] : nlohmann::json::object()},
    };
    
    // 放宽拦截
    if (out["status"] == 0 && out["error_msg"] == "") {
        if (out["error_code"] == 130012) {
             out["error_msg"] = "今日领取可能已达上限，或存在互斥冲突 (Err: 130012)";
        } else {
             out["error_msg"] = "广告 SDK 凭证校验失败或网络问题 (Err: " + std::to_string(out.value("error_code", 0)) + ")";
        }
    }
    
    return out;
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error: ") + e.what());
  }
}

}  // namespace echo::core
