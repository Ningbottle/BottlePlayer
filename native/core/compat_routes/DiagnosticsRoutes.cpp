#include "echo/core/CompatApiUtils.h"
#include "echo/core/CompatRequestContext.h"
#include "echo/core/DeviceService.h"
#include "echo/core/PlaylistService.h"
#include "echo/core/RequestDeadlines.h"
#include "echo/core/UserService.h"
#include "echo/diagnostics/MemorySnapshot.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <sstream>

#ifndef NDEBUG
#include "echo/storage/DeviceRepository.h"
#include "echo/storage/SessionRepository.h"
#endif

namespace echo::core {

CompatResponse HandleHealth() {
  return JsonResponse({
      {"status", 1},
      {"data",
       {
           {"service", "EchoCompatServer"},
           {"state", "ok"},
           {"compat_port", 6609},
           {"native", true},
       }},
  });
}

CompatResponse HandleServerNow() {
  return JsonResponse({
      {"status", 1},
      {"data",
       {
           {"now", UnixSeconds()},
           {"time", UnixSeconds()},
           {"timestamp", UnixMilliseconds()},
           {"server_time", UnixSeconds()},
           {"serverTime", UnixSeconds()},
       }},
  });
}

CompatResponse HandleDiagnosticsMemory() {
  echo::diagnostics::MemorySnapshotProvider provider;
  // workingSet / private 由 PSAPI 真实读取；image cache / pending task / playback
  // 不在 FFI 请求路径内（播放在 WebView），如实报 0 / "webview"。
  const auto snapshot = provider.Capture(0, 0, "webview");
  return JsonResponse({
      {"status", 1},
      {"data",
       {
           {"working_set_bytes", snapshot.workingSetBytes},
           {"private_bytes", snapshot.privateBytes},
           {"image_cache_bytes", snapshot.imageCacheBytes},
           {"pending_task_count", snapshot.pendingTaskCount},
           {"playback_state", snapshot.playbackState},
           {"text", echo::diagnostics::FormatMemorySnapshot(snapshot)},
       }},
  });
}

#ifndef NDEBUG

namespace {

// 指纹只用于「前后是否变化」检测；绝不输出 dfid/guid/token 本体。
std::string ProbeShortFp(const std::string& material) {
  return material.empty() ? std::string("-") : CalculateMd5(material).substr(0, 8);
}

std::string ProbeSessionFp(const SessionInfo& session) {
  return ProbeShortFp(session.userId + "|" + session.token + "|" + session.t1 + "|" +
                      (session.vipToken.empty() ? "novip" : "vip") +
                      "|" + std::to_string(session.vipType));
}

std::string ProbeDeviceFp(const DeviceInfo& device) {
  return ProbeShortFp(device.dfid + "|" + device.mid + "|" + device.guid + "|" +
                      device.uuid + "|" + device.appid + "|" + device.clientver + "|" +
                      (device.registered ? "registered" : "unregistered"));
}

nlohmann::json ProbeProfileFields(const KuGouProfileParams& profile) {
  return {
      {"profile", KuGouProfileName(profile)},
      {"appid", profile.appid},
      {"clientver", profile.clientver},
      {"salt_kind", KuGouSaltKindName(profile.saltKind)},
  };
}

nlohmann::json ProbeNotRunRecord(const char* family, KuGouEdition edition, const char* reason) {
  auto record = ProbeProfileFields(GetKuGouProfile(edition));
  record["family"] = family;
  // NOT_RUN 必须显式标记：不得伪装成业务失败，也不得伪装成成功。
  record["result"] = "NOT_RUN";
  record["not_run_reason"] = reason;
  record["executed"] = false;
  record["elapsed_ms"] = 0;
  record["http_status"] = nullptr;
  record["upstream_status"] = nullptr;
  record["upstream_error_code"] = nullptr;
  record["normalized_status"] = nullptr;
  record["transport_ok"] = false;
  record["json_parseable"] = false;
  record["transport_error"] = "";
  // 计划 1c：三类响应判别字段在 NOT_RUN 上一律为 false / not_run。
  record["success_payload_valid"] = false;
  record["response_shape_valid"] = false;
  record["business_rejection_valid"] = false;
  record["response_contract_valid"] = false;
  record["result_class"] = "not_run";
  return record;
}

// ── 到期时间证据（2026-09-15 审阅发现 3）────────────────────────────────
// account_has_rights 需要「未过期」证据。与前端 vipResolver.parseVipEndTime
// 对齐：解析 "YYYY-MM-DD HH:MM:SS"（日期必需，时刻缺省 00:00:00，按 UTC
// 折算仅用于相对比较）；空串 → missing，非空但解析失败 → invalid（不得当
// “永久”证据，Stage 5a）。
long long ProbeDaysFromCivil(long long y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return era * 146097 + static_cast<long long>(doe) - 719468;
}

long long ProbeParseEndTimeMs(const std::string& text) {
  if (text.empty()) return 0;
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
  if (std::sscanf(text.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 3) {
    return 0;
  }
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 59) {
    return 0;
  }
  const long long seconds =
      ProbeDaysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400LL +
      h * 3600LL + mi * 60LL + s;
  return seconds * 1000LL;
}

long long ProbeNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

struct ProbeEndDateState {
  enum Kind { kMissing, kInvalid, kValid } kind = kMissing;
  long long ms = 0;
};

ProbeEndDateState ProbeEndDateOf(const nlohmann::json& node,
                                 std::initializer_list<const char*> keys) {
  ProbeEndDateState state;
  for (const auto key : keys) {
    if (!node.contains(key)) continue;
    const auto& field = node.at(key);
    if (!field.is_string()) continue;
    const auto raw = field.get<std::string>();
    if (raw.empty()) continue;
    state.ms = ProbeParseEndTimeMs(raw);
    state.kind = state.ms > 0 ? ProbeEndDateState::kValid : ProbeEndDateState::kInvalid;
    return state;
  }
  return state;
}

// 计划 1d：请求实际使用的 mid 口径 = EnsureDeviceReady 的内存补全 +
// ResolveAndroidMid。直接对持久化行调 ResolveAndroidMid 会得到 guid 派生值
// （补全激活后请求路径根本不会用它），造成每次轮次的假错配——因此两侧都
// 先走同一补全再解析。
std::string ProbeRequestMid(const DeviceInfo& device) {
  return ResolveAndroidMid(BackfillInMemoryIdentity(device));
}

// VIP：GetUserVip 返回的就是上游 JSON（可能被本地按 busi_vip 修正过顶层
// is_vip）。计划 1a/1c：
//   * transport_ok = HTTP 层成功（解析出响应体或拿到 2xx）；
//   * json_parseable = 响应体是已解析 JSON（服务层在 Debug 构建对解析结果
//     附加 _debug_http_status；MakeError 结果没有该标记）；
//   * success_payload_valid = status=1 且权益结构合法：busi_vip 按实际契约
//     是数组（合法空数组=明确无权益，也算权威结构），元素必须是对象；
//     is_vip/vip_type 存在时必须是数字；不要求可选字段同时存在；
//   * business_rejection_valid = status=0 且带 error_code（如 20017），
//     属于可解释的业务拒绝——允许参与选族对照；
//   * response_contract_valid = 上述二者之一；超时/解析失败/未知结构不能参与。
// account_has_rights 区分「结构有效」与「账号当前有权益」。2026-09-15 审阅
// 发现 3：busi_vip 非空 ≠ 当前有权益 —— 按条目 is_vip + 产品类型 + 到期
// 逐条判断（对齐 vipResolver），历史到期不得标 true，信息不足记 unknown。
nlohmann::json ProbeVipRecord(KuGouEdition edition,
                              const nlohmann::json& result,
                              long long elapsedMs,
                              const std::string& sessionFp,
                              const std::string& deviceFp) {
  auto record = ProbeProfileFields(GetKuGouProfile(edition));
  record["family"] = "vip";
  record["result"] = "executed";
  record["executed"] = true;
  record["elapsed_ms"] = elapsedMs;
  record["session_fp"] = sessionFp;
  record["device_fp"] = deviceFp;

  const bool isObject = result.is_object();
  const bool hasDebugStatus = isObject && result.contains("_debug_http_status") &&
                              result["_debug_http_status"].is_number();
  const auto httpStatus = hasDebugStatus ? result["_debug_http_status"].get<int>() : 0;
  record["http_status"] = httpStatus > 0 ? nlohmann::json(httpStatus) : nlohmann::json(nullptr);

  const std::string error = isObject && result.contains("error") && result["error"].is_string()
                                ? result["error"].get<std::string>() : "";
  const bool parseFailure = !error.empty() && error.rfind("JSON parse error", 0) == 0;
  record["transport_error"] = (hasDebugStatus || parseFailure) ? "" : error;

  // transport_ok：HTTP 请求成功。已解析响应看 HTTP 状态码；解析失败说明
  // HTTP 层完成（拿到了响应体）但内容不是 JSON。
  const bool transportOk = hasDebugStatus ? (httpStatus >= 200 && httpStatus < 300)
                                          : parseFailure;
  const bool jsonParseable = hasDebugStatus;
  record["transport_ok"] = transportOk;
  record["json_parseable"] = jsonParseable;

  record["upstream_status"] =
      isObject && result.contains("status") ? result["status"] : nlohmann::json(nullptr);
  const auto errorCode = isObject ? ReadKuGouErrorCode(result) : std::nullopt;
  record["upstream_error_code"] =
      errorCode ? nlohmann::json(*errorCode) : nlohmann::json(nullptr);

  bool successPayloadValid = false;
  bool businessRejectionValid = false;
  std::string resultClass;
  if (!transportOk || !jsonParseable) {
    resultClass = parseFailure ? "parse_error" : "transport_error";
  } else {
    const bool statusPresent = result.contains("status") && result["status"].is_number_integer();
    const int upstreamStatus = statusPresent ? result["status"].get<int>() : -1;
    if (upstreamStatus == 1 && result.contains("data") && result["data"].is_object()) {
      const auto& data = result["data"];
      bool anyAuthoritative = false;
      bool contractViolated = false;
      if (data.contains("is_vip")) {
        if (data["is_vip"].is_number()) anyAuthoritative = true;
        else contractViolated = true;
      }
      if (data.contains("vip_type")) {
        if (data["vip_type"].is_number()) anyAuthoritative = true;
        else contractViolated = true;
      }
      // busi_vip 实际契约是数组（UserService 按数组消费）：合法空数组
      // （无权益）也是有效查询结果；元素必须为对象；非数组即结构异常。
      if (data.contains("busi_vip")) {
        const auto& busi = data["busi_vip"];
        if (busi.is_array()) {
          anyAuthoritative = true;
          for (const auto& item : busi) {
            if (!item.is_object()) { contractViolated = true; break; }
          }
          record["busi_vip_kind"] = "array";
          record["busi_vip_count"] = busi.size();
        } else {
          contractViolated = true;
          record["busi_vip_kind"] = std::string(busi.type_name());
          record["busi_vip_count"] = nullptr;
        }
      } else {
        record["busi_vip_kind"] = "missing";
        record["busi_vip_count"] = nullptr;
      }
      // 区分「响应结构有效」与「账号当前有权益」：权益到期 = 结构有效 +
      // 无权益；20017 参数拒绝是另一种失败，不能被「无权益」解释。
      // 审阅发现 3（2026-09-15）：逐条判断，busi_vip 非空不构成权益证据：
      //   active  → 音乐类（svip/music/musicpack）条目 is_vip=1 且（缺省
      //             到期=上游“无限期” 或 有效未过期），或顶层 is_vip=1/
      //             vip_type>0 且顶层到期有效未过期；
      //   expired → 有过权益证据（音乐类 is_vip=1 或顶层付费标志）但日期
      //             均已过期；
      //   none    → 明确无权益（busi_vip 空数组、或条目 is_vip=0）且无
      //             顶层付费标志；
      //   unknown → 信息不足（条目缺 is_vip/产品类型、日期非法、顶层标志
      //             无到期时间）—— 不得标 true。
      const long long nowMs = ProbeNowMs();
      bool activeEvidence = false;
      bool expiredEvidence = false;
      bool noneEvidence = false;
      if (data.contains("busi_vip") && data["busi_vip"].is_array()) {
        if (data["busi_vip"].empty()) {
          noneEvidence = true;  // 合法空数组 = 明确无权益
        }
        for (const auto& item : data["busi_vip"]) {
          if (!item.is_object()) continue;  // 结构违例已计入 contractViolated
          const auto product = item.contains("product_type") && item["product_type"].is_string()
                                   ? item["product_type"].get<std::string>() : std::string();
          const bool musicClass =
              product == "svip" || product == "music" || product == "musicpack";
          const bool hasEntryVip = item.contains("is_vip") && item["is_vip"].is_number();
          const bool entryVip = hasEntryVip && item["is_vip"] == 1;
          if (!musicClass) continue;  // 非音乐白名单（如 tvip）：用途未确认，不构成音乐权益证据
          if (entryVip) {
            const auto endState = ProbeEndDateOf(item, {"vip_end_time", "end_time"});
            if (endState.kind == ProbeEndDateState::kMissing) {
              activeEvidence = true;  // 缺省到期 = 上游“无限期”语义（广告临时 SVIP）
            } else if (endState.kind == ProbeEndDateState::kValid) {
              if (endState.ms > nowMs) activeEvidence = true;
              else expiredEvidence = true;
            }
            // kInvalid：非空但解析失败 → 无可信证据，不判真（Stage 5a）。
          } else if (hasEntryVip) {
            noneEvidence = true;  // is_vip=0：该产品当前无权益（含历史到期）
          }
          // 缺 is_vip 字段：信息不足，不构成任何结论。
        }
      }
      const bool topFlag = (data.contains("is_vip") && data["is_vip"].is_number() &&
                            data["is_vip"] == 1) ||
                           (data.contains("vip_type") && data["vip_type"].is_number() &&
                            data["vip_type"] > 0);
      if (topFlag) {
        // 顶层付费标志：判真必须有「有效且未过期」的到期时间（Stage 5a）。
        const auto topEnd = ProbeEndDateOf(data, {"vip_end_time", "end_time"});
        if (topEnd.kind == ProbeEndDateState::kValid) {
          if (topEnd.ms > nowMs) activeEvidence = true;
          else expiredEvidence = true;
        }
        // missing/invalid：无可信证据 → 不判真，也不计入过期证据。
      }
      std::string rightsState;
      if (activeEvidence) {
        rightsState = "active";
      } else if (expiredEvidence) {
        rightsState = "expired";
      } else if (noneEvidence && !topFlag) {
        rightsState = "none";
      } else {
        rightsState = "unknown";
      }
      record["account_has_rights"] = rightsState == "active";
      record["account_has_rights_state"] = rightsState;
      successPayloadValid = anyAuthoritative && !contractViolated;
      resultClass = successPayloadValid ? "success" : "unknown_structure";
    } else if (upstreamStatus == 0 && errorCode.has_value()) {
      // 可解释的业务拒绝（status=0 + error_code，例如 20017）。
      record["account_has_rights"] = false;
      record["account_has_rights_state"] = "unknown";  // 拒绝不含权益信息
      businessRejectionValid = true;
      resultClass = "business_rejection";
    } else {
      resultClass = "unknown_structure";
    }
  }
  record["success_payload_valid"] = successPayloadValid;
  record["response_shape_valid"] = successPayloadValid;  // 兼容既有字段口径
  record["business_rejection_valid"] = businessRejectionValid;
  record["response_contract_valid"] = successPayloadValid || businessRejectionValid;
  record["result_class"] = resultClass;

  const auto normalized = NormalizeUserVipDetailResponse(result);
  record["normalized_status"] = normalized.value("status", 0);
  record["normalized_authoritative"] = normalized.value("authoritative", false);
  return record;
}

// 歌单：GetUserPlaylists 返回归一化结果；上游字段经 _debug_*（仅 Debug
// 构建）保留，满足「上游 JSON 与归一化结果分开」的记录要求。
// 计划 1b/1c：
//   * 归一化前验证：上游 data.info/list/lists 必须真实存在且为数组
//     （_debug_raw_lists 由 PlaylistService 在归一化前捕获）。归一化补出的
//     空数组不能当作上游成功；info:null 不再被「字段存在」放行；
//   * 区分：非空歌单 / 合法空歌单 / 缺少列表字段 / 列表字段类型错误；
//   * business_rejection_valid = 上游 status=0 且带 error_code，可参与对照。
// 2026-09-15 审阅发现 2：形状通过 ≠ 可用。success_payload_valid 还要求
// 归一化 status=1（生产层把「全部缺 global_collection_id/numeric listid」
// 的 status 打成 0），且非空原始列表必须真的产出条目（非对象元素等会被
// 静默丢弃，不得伪装成合法空歌单）。合法空列表（raw_count=0）仍通过。
// 记录 raw_count / normalized_count / skipped_count 便于解释。
nlohmann::json ProbePlaylistRecord(KuGouEdition edition,
                                   const nlohmann::json& result,
                                   long long elapsedMs,
                                   const std::string& sessionFp,
                                   const std::string& deviceFp) {
  auto record = ProbeProfileFields(GetKuGouProfile(edition));
  record["family"] = "playlist";
  record["result"] = "executed";
  record["executed"] = true;
  record["elapsed_ms"] = elapsedMs;
  record["session_fp"] = sessionFp;
  record["device_fp"] = deviceFp;

  const bool isObject = result.is_object();
  const bool hasDebugStatus = isObject && result.contains("_debug_http_status") &&
                              result["_debug_http_status"].is_number();
  const auto httpStatus = hasDebugStatus ? result["_debug_http_status"].get<int>() : 0;
  record["http_status"] = httpStatus > 0 ? nlohmann::json(httpStatus) : nlohmann::json(nullptr);

  const std::string error = isObject && result.contains("error") && result["error"].is_string()
                                ? result["error"].get<std::string>() : "";
  const bool parseFailure = !error.empty() && error.rfind("JSON parse error", 0) == 0;
  record["transport_error"] = (hasDebugStatus || parseFailure) ? "" : error;

  const bool transportOk = hasDebugStatus ? (httpStatus >= 200 && httpStatus < 300)
                                          : parseFailure;
  const bool jsonParseable = hasDebugStatus;
  record["transport_ok"] = transportOk;
  record["json_parseable"] = jsonParseable;

  record["upstream_status"] =
      isObject && result.contains("_debug_upstream_status") ? result["_debug_upstream_status"]
                                                            : nlohmann::json(nullptr);
  record["upstream_error_code"] =
      isObject && result.contains("_debug_upstream_error_code") ? result["_debug_upstream_error_code"]
                                                                : nlohmann::json(nullptr);
  record["normalized_status"] =
      isObject && result.contains("status") ? result["status"] : nlohmann::json(nullptr);

  // 原始列表字段（归一化前捕获）：字段名、类型、数组长度。无内容，可留档。
  nlohmann::json rawLists = nlohmann::json::array();
  if (isObject && result.contains("_debug_raw_lists") && result["_debug_raw_lists"].is_array()) {
    rawLists = result["_debug_raw_lists"];
  }
  record["raw_list_fields"] = rawLists;
  const bool hasOriginalInfo = isObject && result.contains("_debug_has_original_info") &&
                               result["_debug_has_original_info"].get<bool>();
  record["has_original_info"] = hasOriginalInfo;

  bool successPayloadValid = false;
  bool businessRejectionValid = false;
  std::string resultClass;
  nlohmann::json shapeClass = nullptr;
  if (!transportOk || !jsonParseable) {
    resultClass = parseFailure ? "parse_error" : "transport_error";
  } else {
    const bool upstreamStatusPresent = isObject && result.contains("_debug_upstream_status") &&
                                       result["_debug_upstream_status"].is_number_integer();
    const int upstreamStatus =
        upstreamStatusPresent ? result["_debug_upstream_status"].get<int>() : -1;
    const bool hasUpstreamErrorCode =
        isObject && result.contains("_debug_upstream_error_code") &&
        !result["_debug_upstream_error_code"].is_null();
    if (upstreamStatus == 1) {
      // 归一化前口径：契约列表字段必须存在且至少一个为数组。
      bool anyArray = false;
      std::size_t arrayLength = 0;
      for (const auto& entry : rawLists) {
        if (entry.is_object() && entry.value("type", "") == "array") {
          anyArray = true;
          arrayLength = entry.value("length", static_cast<std::size_t>(0));
          break;
        }
      }
      if (!hasOriginalInfo) {
        shapeClass = "missing_list_field";
      } else if (!anyArray) {
        shapeClass = "list_field_type_error";
      } else {
        record["original_list_count"] = arrayLength;
        shapeClass = arrayLength > 0 ? "non_empty_list" : "empty_list";
      }
      const bool shapeOk = hasOriginalInfo && anyArray;
      // 审阅发现 2（2026-09-15）：形状之后继续验证归一化结果 ——
      //   * normalized status 必须 =1（生产层在全部条目缺标识时打成 0）；
      //   * 非空原始列表必须产出条目（normalized_count>0），否则是「不可
      //     用的非空列表」伪装成合法空歌单；
      //   * 合法空列表（raw_count=0）仍然通过。
      long long normalizedCount = -1;
      long long skippedCount = 0;
      if (shapeOk && isObject && result.contains("data") && result["data"].is_object()) {
        const auto& data = result["data"];
        if (data.contains("list") && data["list"].is_array()) {
          normalizedCount = static_cast<long long>(data["list"].size());
        }
        skippedCount = data.value("skipped_invalid_id_count", 0);
      }
      const long long normalizedStatusValue =
          shapeOk && isObject && result.contains("status") &&
                  result["status"].is_number_integer()
              ? result["status"].get<long long>() : -1;
      if (shapeOk) {
        record["raw_count"] = arrayLength;
        record["normalized_count"] = normalizedCount >= 0
                                         ? nlohmann::json(normalizedCount)
                                         : nlohmann::json(nullptr);
        record["skipped_count"] = skippedCount;
      }
      const bool entriesUsable = arrayLength == 0 || normalizedCount > 0;
      successPayloadValid = shapeOk && normalizedStatusValue == 1 && entriesUsable;
      if (!shapeOk) {
        resultClass = "unknown_structure";
      } else if (normalizedStatusValue != 1) {
        // 上游 status=1 但归一化不可用（缺标识被生产层降级为 0）。
        resultClass = "normalization_failed";
      } else if (!entriesUsable) {
        // 非对象元素等被静默丢弃：归一化「成功」但产出为空。
        resultClass = "all_entries_unusable";
      } else {
        resultClass = "success";
      }
    } else if (upstreamStatus == 0 && hasUpstreamErrorCode) {
      businessRejectionValid = true;
      resultClass = "business_rejection";
    } else {
      resultClass = "unknown_structure";
    }
  }
  record["playlist_shape_class"] = shapeClass;
  record["success_payload_valid"] = successPayloadValid;
  record["response_shape_valid"] = successPayloadValid;  // 兼容既有字段口径
  record["business_rejection_valid"] = businessRejectionValid;
  record["response_contract_valid"] = successPayloadValid || businessRejectionValid;
  record["result_class"] = resultClass;
  return record;
}

// 判定表（计划 1c）——只是提示，不自动改任何生产配置。
// 参与选族的门槛（每组）：executed && transport_ok && json_parseable &&
// response_contract_valid；整轮还必须无干扰、无快照错配。
// 业务拒绝（status=0+error_code）可以参与对照；超时、解析失败、未知结构不能。
std::string ProbeVerdict(const nlohmann::json& standardRecord,
                         const nlohmann::json& conceptRecord,
                         bool interfered,
                         bool snapshotMismatch) {
  if (interfered) return "insufficient_interfered";
  if (snapshotMismatch) return "insufficient_snapshot_mismatch";
  if (!standardRecord.value("executed", false) || !conceptRecord.value("executed", false)) {
    return "insufficient_not_run";
  }
  if (!standardRecord.value("transport_ok", false) || !conceptRecord.value("transport_ok", false)) {
    return "insufficient_transport_failed";
  }
  if (!standardRecord.value("json_parseable", false) ||
      !conceptRecord.value("json_parseable", false)) {
    return "insufficient_parse_failed";
  }
  if (!standardRecord.value("response_contract_valid", false) ||
      !conceptRecord.value("response_contract_valid", false)) {
    return "insufficient_unknown_structure";
  }
  const bool standardSuccess = standardRecord.value("response_shape_valid", false);
  const bool conceptSuccess = conceptRecord.value("response_shape_valid", false);
  if (standardSuccess && conceptSuccess) return "both_succeed_no_config_change";
  if (standardSuccess) return "keep_standard_concept_rejected";
  if (conceptSuccess) return "concept_candidate_needs_reverse_order_retest";
  return "both_business_rejected_no_family_choice";
}

}  // namespace

CompatResponse HandleSignatureFamilyProbe(
    storage::Database& database,
    const LoginHttpGet& sessionHttpGet,
    const LoginHttpPost& sessionHttpPost,
    long long budgetMs,
    bool reverseOrder) {
  static std::atomic<int> attemptCounter{0};

  if (budgetMs <= 0) budgetMs = kSignatureFamilyProbeBudgetMs;

  const bool injected = static_cast<bool>(sessionHttpGet) || static_cast<bool>(sessionHttpPost);
  if (injected && (!sessionHttpGet || !sessionHttpPost)) {
    // 测试注入只给一个动词时拒绝执行：绝不能一半用假传输、一半偷偷建真网络。
    return JsonResponse({{"status", 0},
                         {"error_code", "native_probe_partial_transport"},
                         {"error", "probe requires both GET and POST transports when any seam is injected"},
                         {"data", nullptr}});
  }

  // ── 准备阶段：EnsureDeviceReady / 会话加载（含迁移）只允许发生在这里。
  CompatRequestContext ctx(database);
  const auto& session = ctx.Session();
  const std::string userId = ctx.UserIdOr("");
  const std::string token = ctx.TokenOrEmpty();
  if (!session || userId.empty() || token.empty()) {
    return JsonResponse({{"status", 0},
                         {"error_code", "native_probe_no_session"},
                         {"error", "not logged in"},
                         {"data", nullptr}});
  }

  // ── 不可变快照：四组探测只使用这份拷贝，不再经 Context 重读，
  // 也不写回正式会话。之后任何后台刷新只会体现为「干扰」。
  const DeviceInfo snapDevice = ctx.Device();
  const SessionInfo snapSession = *session;
  // 计划 1d：记录快照捕获时刻（准备完成后）与持久化基线读取时刻。
  const long long snapshotCapturedAt = UnixMilliseconds();
  // 报告指纹取「快照」而非持久化基线：探测实际使用的身份就是快照。
  const std::string sessionFpSnapshot = ProbeSessionFp(snapSession);
  const std::string deviceFpSnapshot = ProbeDeviceFp(snapDevice);
  const std::string requestMid = ProbeRequestMid(snapDevice);
  // 干扰基线取「持久化记录」而非快照：准备阶段允许 EnsureDeviceReady /
  // Load 做规范化与迁移写入（mid/uuid 内存回填等不落盘），这些不属于干扰；
  // 基线读取之后任何持久化变化才判定为干扰。
  const auto deviceBaseline = storage::DeviceRepository(database).Load();
  const auto sessionBaseline = storage::SessionRepository(database).Load();
  const long long baselineReadAt = UnixMilliseconds();
  const std::string sessionFpBefore =
      sessionBaseline ? ProbeSessionFp(*sessionBaseline) : std::string("-");
  const std::string deviceFpBefore =
      deviceBaseline ? ProbeDeviceFp(*deviceBaseline) : std::string("-");

  // 计划 1d：快照一致性——比较「探测实际使用的身份」与「持久化基线」的
  // 请求身份字段（token/userid/dfid/mid）。mid 两侧都经同一补全 + 解析口径
  // （ProbeRequestMid），准备阶段的内存补全不会造成假错配；准备完成后
  // 持久化身份发生变化（如基线读取前的会话刷新）才判 snapshot_mismatch，
  // 该轮不可用于选族——不能只靠时间戳判断。
  const bool sessionIdentityStable =
      sessionBaseline.has_value() && sessionBaseline->userId == snapSession.userId &&
      sessionBaseline->token == snapSession.token;
  const bool deviceIdentityStable =
      deviceBaseline.has_value() && deviceBaseline->dfid == snapDevice.dfid &&
      ProbeRequestMid(*deviceBaseline) == requestMid;
  const bool snapshotMismatch = !sessionIdentityStable || !deviceIdentityStable;

  UserService userSvc = injected ? UserService(sessionHttpGet, sessionHttpPost) : UserService();
  PlaylistService playlistSvc =
      injected ? PlaylistService(sessionHttpGet, sessionHttpPost) : PlaylistService();

  const int attemptNo = attemptCounter.fetch_add(1) + 1;
  const std::time_t now = std::time(nullptr);
  std::tm tmUtc{};
#if defined(_WIN32)
  gmtime_s(&tmUtc, &now);
#else
  gmtime_r(&now, &tmUtc);
#endif
  std::ostringstream idStream;
  idStream << "sigfam-" << std::put_time(&tmUtc, "%Y%m%dT%H%M%SZ");
  const std::string experimentId = idStream.str();
  const std::string attemptId = experimentId + "-a" + std::to_string(attemptNo);

  const auto started = steady_clock::now();
  const auto elapsedMs = [&started] {
    return duration_cast<milliseconds>(steady_clock::now() - started).count();
  };
  const auto budgetLeft = [&budgetMs, &elapsedMs] { return budgetMs - elapsedMs(); };

  auto runVip = [&](KuGouEdition edition) {
    const auto t0 = steady_clock::now();
    auto result = userSvc.GetUserVip(snapDevice, userId, token, edition);
    return ProbeVipRecord(edition, result,
                          duration_cast<milliseconds>(steady_clock::now() - t0).count(),
                          sessionFpSnapshot, deviceFpSnapshot);
  };
  auto runPlaylist = [&](KuGouEdition edition) {
    const auto t0 = steady_clock::now();
    auto result = playlistSvc.GetUserPlaylists(snapDevice, userId, token, 1, 30, edition);
    return ProbePlaylistRecord(edition, result,
                               duration_cast<milliseconds>(steady_clock::now() - t0).count(),
                               sessionFpSnapshot, deviceFpSnapshot);
  };

  // ── 成对探测，共享外层预算；预算耗尽后的探针记 NOT_RUN（不执行、
  // 不记业务失败）。正向顺序：VIP Standard → Concept → 歌单 Standard →
  // Concept；反向顺序（reverseOrder，判定表要求的概念族复测）Concept 在前。
  auto vipStandard = ProbeNotRunRecord("vip", KuGouEdition::Standard, "budget_exhausted");
  auto vipConcept = ProbeNotRunRecord("vip", KuGouEdition::Concept, "budget_exhausted");
  auto playlistStandard =
      ProbeNotRunRecord("playlist", KuGouEdition::Standard, "budget_exhausted");
  auto playlistConcept =
      ProbeNotRunRecord("playlist", KuGouEdition::Concept, "budget_exhausted");

  const auto runVipPair = [&]() {
    if (reverseOrder) {
      if (budgetLeft() > 0) vipConcept = runVip(KuGouEdition::Concept);
      if (budgetLeft() > 0) vipStandard = runVip(KuGouEdition::Standard);
    } else {
      if (budgetLeft() > 0) vipStandard = runVip(KuGouEdition::Standard);
      if (budgetLeft() > 0) vipConcept = runVip(KuGouEdition::Concept);
    }
  };
  const auto runPlaylistPair = [&]() {
    if (reverseOrder) {
      if (budgetLeft() > 0) playlistConcept = runPlaylist(KuGouEdition::Concept);
      if (budgetLeft() > 0) playlistStandard = runPlaylist(KuGouEdition::Standard);
    } else {
      if (budgetLeft() > 0) playlistStandard = runPlaylist(KuGouEdition::Standard);
      if (budgetLeft() > 0) playlistConcept = runPlaylist(KuGouEdition::Concept);
    }
  };
  runVipPair();
  runPlaylistPair();

  const nlohmann::json probes = reverseOrder
      ? nlohmann::json{vipConcept, vipStandard, playlistConcept, playlistStandard}
      : nlohmann::json{vipStandard, vipConcept, playlistStandard, playlistConcept};

  // ── 干扰检测：探测完成后直接重读存储（不再触发任何写路径）。
  // 指纹变化 = 期间发生刷新/退出/注册 → 本轮不用于选族。
  std::string sessionFpAfter = sessionFpBefore;
  std::string deviceFpAfter = deviceFpBefore;
  if (const auto sessionAfter = storage::SessionRepository(database).Load()) {
    sessionFpAfter = ProbeSessionFp(*sessionAfter);
  } else {
    sessionFpAfter = "-";
  }
  if (const auto deviceAfter = storage::DeviceRepository(database).Load()) {
    deviceFpAfter = ProbeDeviceFp(*deviceAfter);
  } else {
    deviceFpAfter = "-";
  }
  bool interfered = false;
  nlohmann::json interference = nlohmann::json::array();
  if (sessionFpAfter != sessionFpBefore) {
    interfered = true;
    interference.push_back("session_fp_changed");
  }
  if (deviceFpAfter != deviceFpBefore) {
    interfered = true;
    interference.push_back("device_fp_changed");
  }

  const bool allExecuted = vipStandard.value("executed", false) &&
                           vipConcept.value("executed", false) &&
                           playlistStandard.value("executed", false) &&
                           playlistConcept.value("executed", false);
  // 计划 1c 选族条件：每组均满足 executed && transport_ok && json_parseable
  // && response_contract_valid；整轮无干扰、无快照错配。
  // 「Standard 超时 + Concept 成功」「结构异常 + 成功」都不是选族依据。
  const auto groupUsable = [](const nlohmann::json& r) {
    return r.value("executed", false) && r.value("transport_ok", false) &&
           r.value("json_parseable", false) && r.value("response_contract_valid", false);
  };
  const bool allGroupsUsable = groupUsable(vipStandard) && groupUsable(vipConcept) &&
                               groupUsable(playlistStandard) && groupUsable(playlistConcept);
  const bool usableForSelection =
      !interfered && !snapshotMismatch && allExecuted && allGroupsUsable;

  // 计划 1e：快照摘要（实际请求身份的长度与指纹，不含本体）。
  const auto shortFp = [](const std::string& material) {
    return material.empty() ? std::string("-") : CalculateMd5(material).substr(0, 8);
  };
  const std::string dfidIdentity =
      snapDevice.dfid.empty() || snapDevice.dfid == "-" ? std::string() : snapDevice.dfid;

  nlohmann::json data = {
      {"experiment_id", experimentId},
      {"attempt_id", attemptId},
      {"order", reverseOrder ? "reverse" : "forward"},
      {"budget_ms", budgetMs},
      {"elapsed_total_ms", elapsedMs()},
      {"snapshot_captured_at", snapshotCapturedAt},
      {"baseline_read_at", baselineReadAt},
      {"session_fp", sessionFpSnapshot},
      {"device_fp", deviceFpSnapshot},
      {"session_fp_baseline", sessionFpBefore},
      {"device_fp_baseline", deviceFpBefore},
      {"session_fp_after", sessionFpAfter},
      {"device_fp_after", deviceFpAfter},
      {"snapshot_mismatch", snapshotMismatch},
      {"request_identity",
       {
           {"token_len", snapSession.token.size()},
           {"userid_len", snapSession.userId.size()},
           {"dfid_len", dfidIdentity.size()},
           {"dfid_fp", shortFp(dfidIdentity)},
           {"mid_len", requestMid.size()},
           {"mid_fp", shortFp(requestMid)},
       }},
      {"interfered", interfered},
      {"interference", interference},
      {"usable_for_selection", usableForSelection},
      {"probes", probes},
      {"vip_verdict", ProbeVerdict(vipStandard, vipConcept, interfered, snapshotMismatch)},
      {"playlist_verdict",
       ProbeVerdict(playlistStandard, playlistConcept, interfered, snapshotMismatch)},
      {"note",
       "Business rejections (status=0 + error_code) participate in the comparison; "
       "timeouts, unparseable bodies and unknown structures never do, and neither do "
       "interfered or snapshot-mismatched rounds. Concept repeat success only forms an "
       "endpoint-candidate fix and must reproduce in the reverse-order round; cold-start "
       "recovery and logout-relogin must pass before any production change."},
  };

  std::ostringstream summary;
  summary << "attempt=" << attemptId
          << " usable=" << (usableForSelection ? "Y" : "N")
          << " interfered=" << (interfered ? "Y" : "N")
          << " snap_mismatch=" << (snapshotMismatch ? "Y" : "N")
          << " vip_std=" << vipStandard.value("result_class", "?")
          << " vip_con=" << vipConcept.value("result_class", "?")
          << " pl_std=" << playlistStandard.value("result_class", "?")
          << " pl_con=" << playlistConcept.value("result_class", "?")
          << " vip_verdict=" << data["vip_verdict"].get<std::string>()
          << " pl_verdict=" << data["playlist_verdict"].get<std::string>();
  ECHO_LOG("SignatureFamily", summary.str());

  return JsonResponse({{"status", 1}, {"data", std::move(data)}});
}

#endif  // !NDEBUG

}  // namespace echo::core
