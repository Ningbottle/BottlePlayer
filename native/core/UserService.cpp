#include "echo/core/UserService.h"
#include "echo/core/Crypto.h"
#include "echo/core/DeviceService.h"
#include "echo/core/KuGouAndroidRequest.h"
#include "echo/core/KuGouProfile.h"
#include "echo/core/SafeStoll.h"
#include "echo/diagnostics/EchoDiagnostics.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

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

constexpr const char* kUnspecifiedUpstreamRejection = "上游拒绝，原因未明确";

// Data key NAMES, sorted, comma joined. Values never reach this function's
// output — a successful claim's data can carry credentials.
std::string DataKeyNames(const nlohmann::json& json) {
  if (!json.contains("data") || !json["data"].is_object()) return {};
  std::vector<std::string> keys;
  for (const auto& item : json["data"].items()) keys.push_back(item.key());
  std::sort(keys.begin(), keys.end());
  std::ostringstream joined;
  for (size_t i = 0; i < keys.size(); ++i) {
    if (i) joined << ',';
    joined << keys[i];
  }
  return joined.str();
}

// One line per claim attempt, on the success path as well as the failure path.
// The success branch used to be silent, which is the direct reason a claim that
// genuinely succeeded on 09-18 was invisible in a log file full of failures and
// the signature-family mismatch survived two months.
void LogClaimOutcome(const char* endpointTag,
                     const nlohmann::json& json,
                     const KuGouProfileParams& profile) {
  std::ostringstream line;
  line << "<<< " << endpointTag
       << " status=" << json.value("status", 0)
       << " error_code="
       << (json.contains("error_code") ? json["error_code"].dump()
                                       : std::string("missing"))
       << " data_keys=[" << DataKeyNames(json) << "]"
       << " profile=" << KuGouProfileName(profile);
  const auto message = UpstreamErrorMessage(json);
  if (!message.empty()) line << " upstream_message=" << message;
  ECHO_LOG("VipClaim", line.str());
}

struct ClaimReason {
  long code;
  const char* reason;
  const char* confidence;
};

// `observed` means this account's own log shows that code alongside that
// outcome. `inferred` means the meaning comes from the upstream message text or
// the reference implementation and was never confirmed against a live response.
// `none` means the code is not in the table at all.
//
// The distinction is load-bearing rather than decorative: the 09-03 conclusion
// that "the Concept family is rejected too" was an inference recorded as though
// it had been observed, and on the strength of that every youth endpoint was
// moved onto the wrong signature family.
constexpr ClaimReason kClaimReasons[] = {
    {131001, "already_claimed_today", "observed"},
    {297002, "upgrade_already_claimed", "observed"},
    {51002, "rejected_identity_or_risk", "observed"},
    {130012, "listen_song_not_eligible", "inferred"},
    {304001, "bad_receive_day", "observed"},
    {20017, "session_family_mismatch", "observed"},
    {20018, "session_family_mismatch", "observed"},
};

ClaimReason ClassifyClaimCode(const nlohmann::json& json) {
  if (!json.contains("error_code") || !json["error_code"].is_number()) {
    return {0, "unknown", "none"};
  }
  const auto code = json["error_code"].get<long>();
  for (const auto& entry : kClaimReasons) {
    if (entry.code == code) return entry;
  }
  // An unlisted code stays unlisted. Folding it into the nearest known bucket
  // would hand the UI a confident sentence about something never observed.
  return {code, "unknown", "none"};
}

nlohmann::json NormalizeClaimFailure(
    const nlohmann::json& json,
    const char* localError,
    const char* endpointTag,
    const KuGouProfileParams& profile) {
  const auto upstreamCode =
      json.contains("error_code") ? json["error_code"] : nlohmann::json(0);
  const auto upstreamMsg = UpstreamErrorMessage(json);
  const std::string localFallback =
      upstreamMsg.empty() ? kUnspecifiedUpstreamRejection : "";
  LogClaimOutcome(endpointTag, json, profile);
  const auto classified = ClassifyClaimCode(json);
  return {
      {"status", 0},
      {"error_code", upstreamCode},
      {"error_msg", upstreamMsg},
      {"error", upstreamMsg.empty() ? localFallback : upstreamMsg},
      {"upstream_message", upstreamMsg},
      {"local_fallback_message", localFallback},
      {"local_error", localError},
      {"reason", classified.reason},
      {"reason_confidence", classified.confidence},
      {"data", json.contains("data") ? json["data"] : nlohmann::json(nullptr)},
      {"raw", json},
  };
}

// Product families recognised by the entitlement model. Anything outside these
// grants nothing: an unknown product_type must not reach "premium" just because
// it happens not to be tvip.
constexpr const char* kPremiumProductTypes[] = {"svip", "music", "musicpack"};
constexpr const char* kListenProductTypes[] = {"tvip"};

bool IsListedProduct(const std::string& product, const char* const* list, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (product == list[i]) return true;
  }
  return false;
}

// Interprets a busi_vip vip_end_time. An entry flagged is_vip=1 with no end
// time is treated as active, which is what the pre-existing is_vip correction
// already assumed; an unparseable end time grants nothing.
bool VipEntryActive(const std::string& endTime, std::time_t now) {
  if (endTime.empty()) return true;
  std::tm tm{};
  std::istringstream ss(endTime);
  ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
  if (ss.fail()) return false;
  const auto parsed = std::mktime(&tm);
  return parsed != -1 && parsed > now;
}

struct EntitlementBucket {
  bool active = false;
  std::string end;
  std::string source;
};

// Reduces the busi_vip list to one representative entry per bucket. Prefers an
// active entry with the furthest end time; with none active, keeps the latest
// expiry, because the VIP card has to be able to say "已于 X 到期" rather than
// showing a bare "无".
EntitlementBucket PickEntitlementBucket(
    const nlohmann::json& busiVip,
    const char* const* list,
    size_t listCount,
    std::time_t now) {
  bool haveActive = false;
  EntitlementBucket active;
  EntitlementBucket expired;
  for (const auto& item : busiVip) {
    if (!item.is_object()) continue;
    const std::string product = item.value("product_type", "");
    if (!IsListedProduct(product, list, listCount)) continue;
    const std::string end = item.value("vip_end_time", "");
    const bool isActive =
        item.value("is_vip", 0) == 1 && VipEntryActive(end, now);
    // Upstream's "YYYY-MM-DD HH:MM:SS" sorts correctly as plain text, so the
    // later expiry is the greater string. An active entry with no end time is
    // unbounded and outranks every dated one.
    if (isActive) {
      const bool better = !haveActive ||
                          (end.empty() && !active.end.empty()) ||
                          (!active.end.empty() && end > active.end);
      if (better) {
        active = {true, end, product};
        haveActive = true;
      }
    } else if (end > expired.end) {
      expired = {false, end, product};
    }
  }
  return haveActive ? active : expired;
}

nlohmann::json BucketToJson(const EntitlementBucket& bucket) {
  return {
      {"active", bucket.active},
      {"end", bucket.end},
      {"source", bucket.source},
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

  // get_my_info is POST-only. An absent POST verb is an explicit configuration
  // failure (partial transport injection), never a reason to reach the default
  // network, and calling an empty std::function would abort the process.
  if (!httpPost_) {
    return MakeError("No HTTP POST handler available");
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
    const std::string& token,
    KuGouEdition edition) const {
  const auto clienttime = std::to_string(std::time(nullptr));
  const auto profile = GetKuGouProfile(edition);

  // Use the device fingerprint already normalized by DeviceService.
  // Do NOT re-derive from dfid here — that breaks the guid bloodline
  // required by concept-edition endpoints.
  const std::string dfid = device.dfid.empty() ? "-" : device.dfid;
  const std::string mid = ResolveAndroidMid(device);
  const std::string uuid = "-";

  // Diagnostic (20017 investigation): request-side identity summary before
  // signing. uuid_param is the value that actually enters the signature;
  // uuid_default_source shows what BuildSignedUrl would have used otherwise.
  // profile/appid/clientver/salt are derived from the actual profile object —
  // never hand-written labels.
  {
    std::ostringstream diag;
    diag << "request profile=" << KuGouProfileName(profile)
         << " appid=" << profile.appid
         << " clientver=" << profile.clientver
         << " salt=" << KuGouSaltKindName(profile.saltKind)
         << " dfid_fp="
         << (dfid == "-" ? std::string("-") : CalculateMd5(dfid).substr(0, 8))
         << " dfid_len=" << (dfid == "-" ? 0 : dfid.size())
         << " uuid_param=" << uuid
         << " uuid_default_source=" << (device.guid.empty() ? "none" : "guid")
         << " guid_len=" << device.guid.size()
         << " uuid_len=" << device.uuid.size()
         << " token_len=" << token.size()
         << " userid_present=" << (userId.empty() ? "N" : "Y")
         << " cookie=Y";
    ECHO_LOG("UserVip", diag.str());
  }

  auto DoGetVip = [&](const KuGouProfileParams& signingProfile) -> nlohmann::json {
    KuGouAndroidRequest req;
    req.endpoint = "https://kugouvip.kugou.com/v1/get_union_vip";
    req.profile = signingProfile;
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
      if (json.value("status", 0) == 1 && json.contains("data") && json["data"].is_object()) {        const auto& data = json["data"];
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
      json["_debug_http_status"] = result.statusCode;
      #endif
      return json;
    } catch (const nlohmann::json::exception& e) {
      return MakeError(std::string("JSON parse error: ") + e.what());
    }
  };

  // 生产契约（默认）：整套 Standard profile（1005/20489/标准盐）。2026-09-03
  // 配对实测：令牌与真指纹配对后概念族仍被 20017 全拒，官方 App 正常。
  // edition 参数（默认 kProjectEdition）供签名族 A/B 对照；appid/clientver/盐必须
  // 整套一起换，busi_type=concept 对两族都保留（参考仓 youth_union_vip.js 同约）。
  auto vipResult = DoGetVip(profile);
  if (vipResult.value("status", 0) == 1) {
    bool hasVipFields = false;
    if (vipResult.contains("data") && vipResult["data"].is_object()) {
      auto& d = vipResult["data"];
      hasVipFields = d.contains("is_vip") || d.contains("busi_vip") || d.contains("vip_type");

      // 权益层级：把 busi_vip 归约成「畅听」与「豪华」两个桶，再给出 tier。
      //
      // 顶层 is_vip 的修正保持只认 premium，tvip 不抬升它 —— 这是刻意的。
      // 「有某种会员」和「能完整播放音乐」是两个判断，混成一个就会让领取在界面上
      // 显示成功、实际仍停在 60 秒试听。二者的桥梁由消费方通过 tier 显式表达，
      // 而不是靠 is_vip 顺带暗示。
      //
      // 白名单沿用原修正逻辑（svip/music/musicpack）；此处只是把那段手写循环收进
      // PickEntitlementBucket，让 is_vip 与 tier 不可能各自漂移。
      {
        static const nlohmann::json kNoBusiVip = nlohmann::json::array();
        const auto& busiList =
            (d.contains("busi_vip") && d["busi_vip"].is_array()) ? d["busi_vip"]
                                                                 : kNoBusiVip;
        const auto now = std::time(nullptr);
        const auto premium = PickEntitlementBucket(
            busiList, kPremiumProductTypes,
            sizeof(kPremiumProductTypes) / sizeof(kPremiumProductTypes[0]), now);
        const auto listen = PickEntitlementBucket(
            busiList, kListenProductTypes,
            sizeof(kListenProductTypes) / sizeof(kListenProductTypes[0]), now);
        const char* tier =
            premium.active ? "premium" : (listen.active ? "listen" : "none");
        d["entitlement_summary"] = {
            {"listen", BucketToJson(listen)},
            {"premium", BucketToJson(premium)},
            {"tier", tier},
            // Shipped with the summary because the UI's "部分歌曲需豪华 VIP 才能
            // 完整播放" copy hangs on it; a constant duplicated in ui/ goes stale.
            {"listen_unlocks_playback", kTvipUnlocksPlayback},
        };
        if (premium.active && hasVipFields) {
          d["is_vip"] = 1;
          ECHO_LOG("UserVip", "corrected is_vip to 1 (active premium busi_vip found)");
        }
      }
    }
    if (hasVipFields) {
      return vipResult;
    }
  }

  return vipResult;
}

nlohmann::json UserService::ClaimVip(
    const DeviceInfo& device,
    const std::string& userId,
    const std::string& token,
    KuGouEdition edition) const {
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
  //   鈥?android defaults inject dfid, mid, uuid="-", appid, clientver, clienttime
  //   鈥?signature = md5(salt + sorted(k=v) + body + salt) where body="" here
  //   鈥?2026-08-31 upstream update: content-type must be
  //     application/x-www-form-urlencoded and every request carries the
  //     dfid/clienttime/mid/kg-* fingerprint headers (util/request.js).
  //   The signature profile is not a per-endpoint choice: it follows the
  //   family that minted this session (kProjectEdition), see ADR-0005 追记.
  //   uuid/headers/device are not part of that profile switch.
  const auto profile = GetKuGouProfile(edition);
  KuGouAndroidRequest req;
  req.endpoint = "https://gateway.kugou.com/youth/v1/recharge/receive_vip_listen_song";
  req.profile = profile;
  req.params = {
      {"userid", userId},
      {"token", token},
      {"source_id", "90139"},
      {"receive_day", receiveDay},
      {"uuid", "-"},
  };
  req.device = device;

  const std::string url = BuildSignedUrl(req);

  auto headers = BuildAndroidHeaders(req);
  headers["Content-Type"] = "application/x-www-form-urlencoded";
  headers["User-Agent"] = "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi";

  // Redacted request log: token never leaves in diagnostics.
  ECHO_LOG("VipClaim", std::string(">>> receive_vip_listen_song userid=") + userId
      + " receive_day=" + receiveDay
      + " profile=" + KuGouProfileName(profile)
      + " appid=" + profile.appid
      + " clientver=" + profile.clientver
      + " salt=" + KuGouSaltKindName(profile.saltKind));

  const auto result = httpPost_(
      url,
      /*body=*/"",
      std::move(headers));

  if (!result.error.empty()) return MakeError(result.error, result.statusCode);

  try {
    auto json = nlohmann::json::parse(result.body);
    // Normalize the response so the UI always sees a usable shape.
    // KuGou's success response is `{status:1, data:{...}, error_msg:""}`.
    // On failure it returns `{status:0, error_msg:"浠婃棩宸查鍙? / "骞垮憡鏈鐪? / ...}`.
    if (json.value("status", 0) != 1) {
      return NormalizeClaimFailure(json, "kugou_vip_claim_failed",
                                   "receive_vip_listen_song", profile);
    }
    LogClaimOutcome("receive_vip_listen_song", json, profile);
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
    const std::string& token,
    KuGouEdition edition) const {
  if (userId.empty() || userId == "0" || token.empty()) {
    return MakeError("not logged in");
  }

  const auto clienttime = std::to_string(std::time(nullptr));

  // Reference (MakcRe/KuGouMusicApi module/youth_day_vip_upgrade.js):
  //   POST /youth/v1/listen_song/upgrade_vip_reward
  //   params: kugouid=<userid>, ad_type=1 (+ default token/userid/uuid="-")
  // android encryptType => params live in URL query, no body. The signature
  // family follows the session; see ADR-0005.
  const auto profile = GetKuGouProfile(edition);
  KuGouAndroidRequest req;
  req.endpoint = "https://gateway.kugou.com/youth/v1/listen_song/upgrade_vip_reward";
  req.profile = profile;
  req.params = {
      {"userid", userId},
      {"token", token},
      {"kugouid", userId},
      {"ad_type", "1"},
      {"uuid", "-"},
  };
  req.device = device;

  const std::string url = BuildSignedUrl(req);

  // Same 2026-08-31 reference alignment as ClaimVip: fingerprint headers +
  // form-urlencoded content type, params stay in the signed query string.
  auto headers = BuildAndroidHeaders(req);
  headers["Content-Type"] = "application/x-www-form-urlencoded";
  headers["User-Agent"] = "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi";

  ECHO_LOG("VipClaim", std::string(">>> upgrade_vip_reward userid=") + userId);

  const auto result = httpPost_(
      url,
      /*body=*/"",
      std::move(headers));

  if (!result.error.empty()) return MakeError(result.error, result.statusCode);

  try {
    auto json = nlohmann::json::parse(result.body);
    if (json.value("status", 0) != 1) {
      return NormalizeClaimFailure(json, "kugou_vip_upgrade_failed",
                                   "upgrade_vip_reward", profile);
    }
    LogClaimOutcome("upgrade_vip_reward", json, profile);
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
    const DeviceInfo& device, const std::string& userId, const std::string& token,
    KuGouEdition edition) const {
  if (userId.empty() || userId == "0" || token.empty()) {
    return MakeError("not logged in");
  }

  const std::string body = R"({"mixsongid":666075191})";

  // This endpoint overrides clientver to 10566 for its report protocol and
  // nothing else. server/module/youth_listen_song.js does exactly this: it
  // takes the lite/concept appid and salt and swaps only clientver, so the
  // appid and the salt keep following the session family. See ADR-0005.
  auto profile = GetKuGouProfile(edition);
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
    const bool rejected = json.value("status", 0) != 1;
    const std::string localFallback =
        rejected && upstreamError.empty() ? kUnspecifiedUpstreamRejection : "";
    // Same single outcome line as the other claim routes, on success too. The
    // bespoke diagnostic here used to be the only record this route ever made,
    // and only for failures.
    LogClaimOutcome("report/listen_song", json, profile);
    nlohmann::json out = {
        {"status", json.value("status", 0)},
        {"error_code", json.contains("error_code") ? json["error_code"] : nlohmann::json(0)},
        {"error_msg", upstreamError},
        {"error", upstreamError.empty() ? localFallback : upstreamError},
        {"upstream_message", upstreamError},
        {"local_fallback_message", localFallback},
        {"data", json.contains("data") && json["data"].is_object() ? json["data"] : nlohmann::json::object()},
    };
    // reason belongs to the failure contract only; a successful report has
    // nothing to classify and the UI must not have to ignore a field that is
    // always "unknown" on its happy path.
    if (rejected) {
      const auto classified = ClassifyClaimCode(json);
      out["reason"] = classified.reason;
      out["reason_confidence"] = classified.confidence;
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
    const DeviceInfo& device, const std::string& userId, const std::string& token,
    KuGouEdition edition) const {
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

  // 签名族跟随铸造会话的那一族，见 ADR-0005 追记。此处曾有一句注释记录
  // "2026-09-03 配对实测：概念族被拒后统一切标准族"，该结论已被 09-29 的
  // 同会话 A/B 推翻（标准族 20017 业务拒绝，概念族成功），且当时那次实验
  // 混入了设备注册顺序与 product_type 两个无关变量，不足以支撑这个决定。
  const auto profile = GetKuGouProfile(edition);
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
    const auto upstreamError = UpstreamErrorMessage(json);
    const bool rejected = json.value("status", 0) != 1;
    const std::string localFallback =
        rejected && upstreamError.empty() ? kUnspecifiedUpstreamRejection : "";
    LogClaimOutcome("ad/play_report", json, profile);
    nlohmann::json out = {
        {"status", json.value("status", 0)},
        {"error_code", json.contains("error_code") ? json["error_code"] : nlohmann::json(0)},
        {"error_msg", upstreamError},
        {"error", upstreamError.empty() ? localFallback : upstreamError},
        {"upstream_message", upstreamError},
        {"local_fallback_message", localFallback},
        {"data", json.contains("data") && json["data"].is_object() ? json["data"] : nlohmann::json::object()},
    };
    if (rejected) {
      const auto classified = ClassifyClaimCode(json);
      out["reason"] = classified.reason;
      out["reason_confidence"] = classified.confidence;
    }
    return out;
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error: ") + e.what());
  }
}

}  // namespace echo::core
