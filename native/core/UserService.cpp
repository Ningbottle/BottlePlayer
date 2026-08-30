#include "echo/core/UserService.h"
#include "echo/core/Crypto.h"
#include "echo/core/DeviceService.h"
#include "echo/core/KuGouAndroidRequest.h"
#include "echo/core/KuGouProfile.h"
#include "echo/core/SafeStoll.h"
#include "echo/diagnostics/EchoDiagnostics.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>

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

std::string UpstreamErrorMessage(const nlohmann::json& json) {
  for (const char* key : {"error_msg", "error", "msg", "message"}) {
    if (json.contains(key) && json[key].is_string()) {
      const auto message = json[key].get<std::string>();
      if (!message.empty()) return message;
    }
  }
  return {};
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
      {"visit_time", SafeStoll(clienttime)},
      {"usertype", 1},
      {"p", pk},
      {"userid", SafeStoll(userId)},
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
    // 移除 debug_url，因为它包含 token 和 userid，会泄露用户凭证
    // json["debug_url"] = url;

    // 添加脱敏的调试信息（不含敏感参数）
    #ifdef _DEBUG
    // 仅在 Debug 模式下添加请求摘要（不含 token/userid）
    nlohmann::json debug_info = {
      {"endpoint", "get_my_info"},
      {"status", json.value("status", 0)}
    };
    json["_debug"] = debug_info;
    #endif

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
        // get_union_vip 只认 busi_type；product_type/opt_product_types 会被上游判 params invalid
        {"clienttime", clienttime},
        {"uuid", uuid},
    };
    req.device = device;

    const std::string url = BuildSignedUrl(req);
    auto headers = BuildAndroidHeaders(req);
    // kugouvip.kugou.com 要求会话 Cookie（参考 youth_union_vip 与可用的领取端点）
    headers["Cookie"] = "token=" + token + "; userid=" + userId + "; KugooID=" + userId;

    const auto result = httpGet_(url, std::move(headers));

    if (!result.error.empty()) {
      ECHO_LOG("UserVip", std::string("network error: ") + result.error);
      return MakeError(result.error, result.statusCode);
    }

    try {
      auto json = nlohmann::json::parse(result.body);
      if (json.value("status", 0) == 1 && json.contains("data") && json["data"].is_object()) {
        const auto& data = json["data"];
        const auto field = [&](const char* key) {
          return data.contains(key) ? data[key].dump() : std::string("missing");
        };
        std::ostringstream diagnostic;
        diagnostic << "vip_fields is_vip=" << field("is_vip")
                   << " vip_type=" << field("vip_type")
                   << " vip_level=" << field("vip_level")
                   << " svip_level=" << field("svip_level")
                   << " vip_end_time=" << field("vip_end_time")
                   << " busi_vip=";
        if (data.contains("busi_vip") && data["busi_vip"].is_array()) {
          diagnostic << '[';
          bool first = true;
          for (const auto& item : data["busi_vip"]) {
            if (!item.is_object()) continue;
            if (!first) diagnostic << ',';
            first = false;
            diagnostic << "{product_type:"
                       << (item.contains("product_type") ? item["product_type"].dump() : "missing")
                       << ",is_vip:"
                       << (item.contains("is_vip") ? item["is_vip"].dump() : "missing")
                       << ",vip_end_time:"
                       << (item.contains("vip_end_time") ? item["vip_end_time"].dump() : "missing")
                       << '}';
          }
          diagnostic << ']';
        } else {
          diagnostic << "missing";
        }
        ECHO_LOG("UserVip", diagnostic.str());
      } else {
        // 上游未返回权威 VIP 数据：打出状态与响应截断，定位签名/参数问题
        const std::string bodyPreview = result.body.substr(0, 400);
        ECHO_LOG("UserVip", std::string("upstream rejected status=")
          + std::to_string(json.value("status", -1))
          + " body=" + bodyPreview
          + " | userid=" + (userId.empty() ? std::string("EMPTY") : userId)
          + " token=" + (token.empty() ? std::string("EMPTY") : std::string("PRESENT(len=") + std::to_string(token.size()) + ")"));
      }
      #ifdef _DEBUG
      json["_debug_profile_appid"] = profile.appid;
      #endif
      return json;
    } catch (const nlohmann::json::exception& e) {
      return MakeError(std::string("JSON parse error: ") + e.what());
    }
  };

  // 实测（2026-08-04，本账户）：Standard profile 被上游 20017(params invalid) 拒绝，
  // Concept 正常返回；参考实现的默认 Standard 配置实测同样被拒。
  // busi_type=concept 与 Concept clientver 配套，按实测证据保留 Concept。
  auto conceptResult = DoGetVip(GetKuGouProfile(KuGouEdition::Concept));
  if (conceptResult.value("status", 0) == 1) {
    bool hasVipFields = false;
    if (conceptResult.contains("data") && conceptResult["data"].is_object()) {
      auto& d = conceptResult["data"];
      hasVipFields = d.contains("is_vip") || d.contains("busi_vip") || d.contains("vip_type");

      // 修正顶层 is_vip：busi_vip list 里有 is_vip=1 且未过期的记录时，data.is_vip 应为 1。
      // 只认解锁歌曲的产品（svip/music/musicpack）；tvip 是听书权益，对音乐 App 不算 VIP。
      if (hasVipFields && d.contains("busi_vip") && d["busi_vip"].is_array()) {
        const auto now = std::time(nullptr);
        bool realVip = false;
        for (const auto& item : d["busi_vip"]) {
          if (item.value("is_vip", 0) != 1) continue;
          const std::string product = item.value("product_type", "");
          if (product != "svip" && product != "music" && product != "musicpack") continue;
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
  // The youth reward endpoint uses the standard Android signing identity and
  // only overrides clientver for its report protocol.
  auto profile = GetKuGouProfile(KuGouEdition::Standard);
  profile.clientver = "10566";

  KuGouAndroidRequest req;
  req.endpoint = "https://gateway.kugou.com/youth/v2/report/listen_song";
  req.profile = profile;
  req.device = device;
  req.body = body;
  // Note: old code didn't send 'plat' param, so we don't add it here
  req.params["clienttime"] = std::to_string(std::time(nullptr));
  // This endpoint's Android client contract deliberately uses a sentinel
  // UUID, even when the registered device has a persistent UUID.
  req.params["uuid"] = "-";
  if (!userId.empty() && userId != "0") req.params["userid"] = userId;
  if (!token.empty()) req.params["token"] = token;

  const std::string url = BuildSignedUrl(req);
  const std::string cookie = "token=" + token + "; userid=" + userId + "; KugooID=" + userId;
  auto headers = BuildAndroidHeaders(req);
  headers["Cookie"] = cookie;
  headers["Content-Type"] = "application/json; charset=utf-8";
  headers["User-Agent"] =
      "Android13-1070-10566-201-0-ReportPlaySongToServerProtocol-wifi";

  const auto result = httpPost_(
      url,
      body,
      std::move(headers));

  if (!result.error.empty()) {
    ECHO_LOG("YouthListen", std::string("network error: ") + result.error);
    return MakeError(result.error, result.statusCode);
  }

  try {
    auto json = nlohmann::json::parse(result.body);
    const auto upstreamError = UpstreamErrorMessage(json);
    if (json.value("status", 0) != 1) {
      std::ostringstream diagnostic;
      diagnostic << "upstream status=" << json.value("status", 0)
                 << " error_code="
                 << (json.contains("error_code") ? json["error_code"].dump() : "missing")
                 << " message=" << (upstreamError.empty() ? "<empty>" : upstreamError)
                 << " data_type="
                 << (json.contains("data") ? json["data"].type_name() : "missing");
      ECHO_LOG("YouthListen", diagnostic.str());
    }
    nlohmann::json out = {
        {"status", json.value("status", 0)},
        {"error_code", json.contains("error_code") ? json["error_code"] : nlohmann::json(0)},
        {"error_msg", upstreamError},
        {"error", upstreamError},
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
