#include "echo/core/SongUrlService.h"
#include "echo/core/Crypto.h"
#include "echo/core/DeviceService.h"
#include "echo/core/KuGouAndroidRequest.h"
#include "echo/core/KuGouProfile.h"
#include "echo/diagnostics/EchoDiagnostics.h"
#include "echo/diagnostics/Redaction.h"

#include <chrono>
#include <ctime>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

namespace echo::core {
namespace {

std::string Trim(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  std::size_t first = 0;
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
    ++first;
  }
  if (first > 0) value.erase(0, first);
  return value;
}

std::string ReadString(const nlohmann::json& value, std::string_view key) {
  if (!value.contains(key)) return "";
  const auto& item = value.at(key);
  if (item.is_string()) return item.get<std::string>();
  if (item.is_number_integer()) return std::to_string(item.get<std::int64_t>());
  if (item.is_number_unsigned()) return std::to_string(item.get<std::uint64_t>());
  return "";
}

int ReadInt(const nlohmann::json& value, std::string_view key, int fallback = 0) {
  if (!value.contains(key)) return fallback;
  const auto& item = value.at(key);
  if (item.is_number_integer()) return item.get<int>();
  if (item.is_number_unsigned()) return static_cast<int>(item.get<unsigned int>());
  if (item.is_string()) {
    try {
      return std::stoi(item.get<std::string>());
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

std::string ReadStringOrFirstArrayElement(const nlohmann::json& value, std::string_view key) {
  if (!value.contains(key)) return "";
  const auto& item = value.at(key);
  if (item.is_string()) return item.get<std::string>();
  if (item.is_array() && !item.empty() && item[0].is_string()) return item[0].get<std::string>();
  return "";
}

// BuildV5Url removed: /v5/url signing now handled by KAR BuildSignedUrl
// with includeSongUrlKey=true, profile.clientver=V5UrlClientver.

nlohmann::json EmptySongUrl(std::string hash, std::string quality, std::string error) {
  return {
      {"status", 0},
      {"error_code", error.empty() ? "native_song_url_empty" : "native_song_url_failed"},
      {"error", std::move(error)},
      {"url", ""},
      {"play_url", ""},
      {"playUrl", ""},
      {"delivery", "unknown"},
      {"is_preview", false},
      {"data",
       {
           {"hash", std::move(hash)},
           {"quality", std::move(quality)},
           {"url", ""},
           {"play_url", ""},
           {"playUrl", ""},
           {"delivery", "unknown"},
           {"is_preview", false},
           {"backup_url", nlohmann::json::array()},
       }},
  };
}

nlohmann::json NormalizeBackupUrl(const nlohmann::json& value) {
  if (value.is_array()) return value;
  if (value.is_string() && !value.get<std::string>().empty()) {
    return nlohmann::json::array({value.get<std::string>()});
  }
  if (value.is_object()) {
    nlohmann::json urls = nlohmann::json::array();
    for (const auto& item : value.items()) {
      if (item.value().is_string() && !item.value().get<std::string>().empty()) {
        urls.push_back(item.value().get<std::string>());
      } else if (item.value().is_array()) {
        for (const auto& nested : item.value()) {
          if (nested.is_string() && !nested.get<std::string>().empty()) {
            urls.push_back(nested.get<std::string>());
          }
        }
      }
    }
    return urls;
  }
  return nlohmann::json::array();
}

bool HasFailProcess(const nlohmann::json& upstream, std::initializer_list<std::string_view> expected) {
  if (!upstream.contains("fail_process") || !upstream["fail_process"].is_array()) {
    return false;
  }
  for (const auto& item : upstream["fail_process"]) {
    if (!item.is_string()) continue;
    const auto reason = item.get<std::string>();
    for (const auto wanted : expected) {
      if (reason == wanted) return true;
    }
  }
  return false;
}

// ── Duration-unit provenance ────────────────────────────────────────────
//
// The unit of a duration field is read from the FIELD NAME; it is never
// inferred from magnitude. Crucially, a field yields a unit ONLY when the
// exact (endpoint, response-level, field) combination it was read from is
// listed in the evidence table below. A spelling observed at one endpoint or
// level is NOT generalised into a contract for every endpoint or every nested
// object — the previous flat-list model did exactly that and is the defect
// this type closes.
enum class SongUrlEndpoint { kV6PrivUrl, kV5Url };
enum class ResponseLevel { kEnvelope, kData, kItem, kInfo };

// A JSON object together with the surface it was read from. Every object that
// takes part in a duration decision carries its source, so the unit lookup can
// key on (endpoint, level, field) instead of the field name alone.
struct SourcedJson {
  nlohmann::json object;
  SongUrlEndpoint endpoint;
  ResponseLevel level;
};

SourcedJson V6Source(ResponseLevel level, const nlohmann::json& object) {
  return SourcedJson{object, SongUrlEndpoint::kV6PrivUrl, level};
}

SourcedJson V5Source(ResponseLevel level, const nlohmann::json& object) {
  return SourcedJson{object, SongUrlEndpoint::kV5Url, level};
}

// The explicit evidence table. A row may only exist here WITH a provenance
// citation — there must be no row without evidence. Only rows in this table
// establish a track length, and today there is exactly one.
struct UnitEvidence {
  SongUrlEndpoint endpoint;
  ResponseLevel level;
  const char* field;
  long long millisPerTick;
  const char* provenance;
};

const UnitEvidence kUnitEvidence[] = {
    {SongUrlEndpoint::kV5Url, ResponseLevel::kEnvelope, "timeLength", 1000LL,
     "outputs/vip-stability-audit-2026-09-05/live-session-probe-results.json: "
     "song.data.raw.timeLength = 294 beside song.data.raw.hash_offset.end_ms = "
     "60000 on a real ~5 min track. `raw` is the v5 upstream envelope (Resolve "
     "assigns raw = upstream for v5), so the sample is (v5/url, Envelope). "
     "Reading 294 as milliseconds would place the 60000 ms window far past the "
     "whole track (not self-consistent) -> seconds (x1000)."},
    // No further rows. v6/priv_url at Envelope/Data/Item/Info, and v5/url at
    // Data, are UNEVIDENCED. Adding a row requires a cited capture for that
    // exact (endpoint, level, field) — never a magnitude guess, never a copy
    // of another row's unit.
};

// Duration spellings that are NEVER a unit basis on /song/url. They are read
// only to detect a self-contradictory payload once a basis exists, and are
// converted by their DOCUMENTED meaning before comparison:
//   duration     seconds  seconds on Catalog/Playlist/Rank; never captured on
//                         /song/url.
//   time_length  seconds  PROJECT ALIAS emitted by this service from
//                         timeLength (BuildSongUrlOutput / quality switch).
//   timelen      millis   PROJECT ALIAS built by Catalog/Playlist/Rank as
//                         duration * 1000.
//   timelength   seconds  unverified spelling; never captured anywhere.
struct DurationAlias {
  const char* field;
  long long millisPerTick;
};

const DurationAlias kDurationAliases[] = {
    {"duration", 1000LL},
    {"time_length", 1000LL},
    {"timelen", 1LL},
    {"timelength", 1000LL},
};

// Establishes the track length in milliseconds, or nullopt when no sourced
// field does. Pass 1 consults ONLY the evidence table; pass 2 only checks the
// remaining spellings for agreement with an already-established basis.
std::optional<long long> ResolveTrackMillis(
    std::initializer_list<SourcedJson> sources) {
  // Pass 1: only a (endpoint, level, field) row present in kUnitEvidence may
  // establish the unit. Same-endpoint, different-level objects do not share a
  // row, so a v5 envelope sample cannot establish a v6 or nested-field unit.
  std::optional<long long> track;
  for (const auto& source : sources) {
    if (!source.object.is_object()) continue;
    for (const auto& row : kUnitEvidence) {
      if (row.endpoint != source.endpoint || row.level != source.level) continue;
      if (!source.object.contains(row.field)) continue;
      const int ticks = ReadInt(source.object, row.field, 0);
      if (ticks <= 0) continue;
      const long long millis = static_cast<long long>(ticks) * row.millisPerTick;
      if (track && *track != millis) return std::nullopt;
      track = millis;
    }
  }
  if (!track) {
    // No evidence-backed source at all (unevidenced endpoint/level, alias-only,
    // or absent): the length is unknowable, so the caller must not decide.
    return std::nullopt;
  }

  // Pass 2: every alias present in the response must agree with the basis. The
  // aliases never supply the unit themselves.
  for (const auto& source : sources) {
    if (!source.object.is_object()) continue;
    for (const auto& alias : kDurationAliases) {
      if (!source.object.contains(alias.field)) continue;
      const int ticks = ReadInt(source.object, alias.field, 0);
      if (ticks <= 0) continue;
      if (static_cast<long long>(ticks) * alias.millisPerTick != *track) {
        return std::nullopt;  // a disagreement leaves the unit unresolved
      }
    }
  }
  return track;
}

// Delivery describes this URL, never an earlier failed attempt. Explicit clip
// evidence (a /yp/p_ path, a restrictive fail_process, a window opening past
// zero) wins over a /full/ path. A missing marker is not proof of a preview:
// when the duration unit has no evidence-backed source (for the endpoint and
// level each object was read from) and there is no independent clip evidence
// either, the delivery stays "unknown".
//
// Each source carries the (endpoint, level) it came from, so the same
// envelope window on the v5 side (evidence-backed unit) and the v6 side
// (unevidenced) can legitimately classify differently.
std::string ClassifyDelivery(const std::string& url,
                            std::initializer_list<SourcedJson> sources,
                            bool requestedPreview = false) {
  if (url.empty()) return "unknown";
  const auto pathStart = url.find('/', url.find("://") == std::string::npos ? 0 : url.find("://") + 3);
  const auto pathEnd = url.find_first_of("?#");
  const auto path = pathStart == std::string::npos || pathStart >= pathEnd
      ? std::string{} : url.substr(pathStart, pathEnd - pathStart);
  bool preview = requestedPreview || path.find("/yp/p_") != std::string::npos;
  bool uncertainOffset = false;
  const auto trackMillis = ResolveTrackMillis(sources);
  for (const auto& sourced : sources) {
    const auto& source = sourced.object;
    if (!source.is_object()) continue;
    if (source.contains("fail_process")) {
      const auto& failure = source["fail_process"];
      auto restricted = [](const nlohmann::json& v) {
        return v == 12 || v == "12" || v == "pkg" || v == "buy" || v == "vip";
      };
      if (failure.is_array()) {
        for (const auto& reason : failure) preview = preview || restricted(reason);
      } else {
        preview = preview || restricted(failure);
      }
    }
    if (!source.contains("hash_offset") || source["hash_offset"].is_null()) continue;
    const auto& offset = source["hash_offset"];
    if (!offset.is_object() || offset.empty()) continue;
    const int start = ReadInt(offset, "start_ms", 0);
    const int end = ReadInt(offset, "end_ms", 0);
    if (start < 0 || end <= start) {
      // Not a valid window; nothing may be concluded from it.
      uncertainOffset = true;
    } else if (start > 0) {
      // Provable without knowing the track length: the *_ms field names are
      // explicit, and a window opening past zero is necessarily an excerpt.
      preview = true;
    } else if (trackMillis) {
      if (end > *trackMillis) {
        // The window outruns the track it describes: the payload contradicts
        // itself, so no delivery may be claimed from it.
        uncertainOffset = true;
      } else if (end < *trackMillis) {
        preview = true;  // the window stops short of the track end
      }
      // end == track: the window covers the whole track; no preview signal.
    } else {
      // The window opens at zero and no field established the track length
      // (absent, unrecognised, or two fields disagreeing). The unit is not
      // resolved, so the delivery stays unclaimed instead of being guessed.
      uncertainOffset = true;
    }
  }
  if (preview) return "preview";
  if (uncertainOffset) return "unknown";
  return path.find("/full/") != std::string::npos ? "full" : "unknown";
}

nlohmann::json MakeAttempt(const char* endpoint, const HttpResult& result,
                           const nlohmann::json& response, bool anonymous,
                           const std::string& quality, const std::string& delivery) {
  auto failure = response.is_object()
      ? response.value("fail_process", nlohmann::json{}) : nlohmann::json{};
  // v6 also nests rejection codes in data[].info. Preserve those when the
  // envelope has no code; a usable URL must not erase the nested rejection.
  if (failure.is_null() && response.contains("data")) {
    nlohmann::json reasons = nlohmann::json::array();
    auto collect = [&](const nlohmann::json& source) {
      if (!source.is_object() || !source.contains("fail_process")) return;
      const auto& value = source["fail_process"];
      if (value.is_null()) return;
      if (value.is_array()) {
        for (const auto& reason : value) reasons.push_back(reason);
      } else reasons.push_back(value);
    };
    const auto& data = response["data"];
    if (data.is_array()) {
      for (const auto& item : data) {
        collect(item);
        if (item.is_object()) collect(item.value("info", nlohmann::json{}));
      }
    } else collect(data);
    if (reasons.size() == 1) failure = reasons[0];
    else if (!reasons.empty()) failure = std::move(reasons);
  }
  const int errcode = ReadInt(response, "errcode", ReadInt(response, "error_code", 0));
  // Sanitized upstream message for diagnostics only (no URLs / tokens).
  std::string message;
  if (response.is_object()) {
    for (const char* key : {"message", "error_msg", "errmsg"}) {
      if (!response.contains(key) || !response[key].is_string()) continue;
      message = response[key].get<std::string>();
      break;
    }
    if (message.size() > 120) message.resize(120);
  }
  const bool hasEntitlementSignal = [&]() {
    if (failure.is_null() || failure.empty()) return false;
    auto match = [](const nlohmann::json& v) {
      const auto s = v.is_string() ? v.get<std::string>() : v.dump();
      return s.find("pkg") != std::string::npos
          || s.find("buy") != std::string::npos
          || s.find("vip") != std::string::npos;
    };
    if (failure.is_array()) {
      for (const auto& item : failure) if (match(item)) return true;
      return false;
    }
    return match(failure);
  }();
  // Bare 20018 is auth rejection evidence (owner logs: "token api error"),
  // not proof of missing VIP package. Entitlement only when fail_process says so.
  std::string rejectClass;
  if (hasEntitlementSignal) {
    rejectClass = "entitlement_required";
  } else if (errcode == 20018 || message.find("token api error") != std::string::npos) {
    rejectClass = "auth_rejected";
  } else if (!result.error.empty()) {
    rejectClass = "transport";
  } else if (result.statusCode < 200 || result.statusCode >= 300) {
    rejectClass = "http";
  } else if (!response.is_object() || response.empty()) {
    rejectClass = "invalid_json";
  } else if (errcode != 0) {
    rejectClass = "upstream_error";
  }
  // Only diagnostic fields: never copy signed URLs, request params, or bodies.
  return {{"endpoint", endpoint}, {"http_status", result.statusCode},
          {"errcode", errcode},
          {"fail_process", failure}, {"anonymous", anonymous},
          {"quality", quality}, {"delivery", delivery}, {"is_preview", delivery == "preview"},
          {"message", message},
          {"reject_class", rejectClass},
          {"error", !result.error.empty() ? "transport"
              : result.statusCode < 200 || result.statusCode >= 300 ? "http"
              : !response.is_object() || response.empty() ? "invalid_json" : ""}};
}

nlohmann::json WithAttempts(nlohmann::json output, const nlohmann::json& attempts) {
  output["attempts"] = attempts;
  output["data"]["attempts"] = attempts;
  return output;
}

void ClearAuth(std::unordered_map<std::string, std::string>& params) {
  params.erase("userid");
  params.erase("token");
  params["dfid"] = "-";
  params["mid"] = "0";
  params["uuid"] = "-";
}

// ── Shared normalization helpers ───────────────────────────────────────

std::string NormalizeHash(std::string hash) {
  hash = Trim(std::move(hash));
  std::transform(hash.begin(), hash.end(), hash.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return hash;
}

struct SongUrlOutput {
  std::string hash;
  std::string reqHash;
  std::string quality;
  std::string playUrl;
  std::string delivery = "unknown";
  bool vipRequired = false;
  nlohmann::json backupUrls = nlohmann::json::array();
  nlohmann::json availableQualities = nlohmann::json::array();
  nlohmann::json raw;
  // Error fields
  std::string error;
  std::string errorCode;
  // Metadata
  std::string fileName;
  std::string songName;
  std::string singerName;
  int timeLength = 0;
  int bitRate = 0;
  std::string extName;
  // Optional (V5 only)
  int albumid = 0;
  int albumAudioId = 0;
  int audioId = 0;
  int privilege = 0;
  int payType = 0;
};

nlohmann::json BuildSongUrlOutput(SongUrlOutput out) {
  const bool ok = !out.playUrl.empty();
  return {
      {"status", ok ? 1 : 0},
      {"error", out.error},
      {"error_code", out.errorCode},
      {"url", out.playUrl},
      {"play_url", out.playUrl},
      {"playUrl", out.playUrl},
      {"quality", out.quality}, // Stage 6a: 顶层与 data.quality 同源（切换后由 Resolve 同步）
      {"delivery", out.delivery},
      {"is_preview", out.delivery == "preview"},
      {"vip_required", out.vipRequired},
      {"data",
       {
           {"hash", out.hash},
           {"req_hash", out.reqHash.empty() ? out.hash : out.reqHash},
           {"quality", out.quality},
           {"url", out.playUrl},
           {"play_url", out.playUrl},
           {"playUrl", out.playUrl},
           {"delivery", out.delivery},
           {"is_preview", out.delivery == "preview"},
           {"vip_required", out.vipRequired},
           {"backup_url", out.backupUrls},
           {"file_name", out.fileName},
           {"fileName", out.fileName},
           {"song_name", out.songName},
           {"songName", out.songName},
           {"singer_name", out.singerName},
           {"singerName", out.singerName},
           {"album_id", out.albumid},
           {"albumid", out.albumid},
           {"album_audio_id", out.albumAudioId},
           {"audio_id", out.audioId},
           {"time_length", out.timeLength},
           {"timeLength", out.timeLength},
           {"bit_rate", out.bitRate},
           {"bitRate", out.bitRate},
           {"ext_name", out.extName},
           {"extName", out.extName},
           {"privilege", out.privilege},
           {"pay_type", out.payType},
           {"available_qualities", out.availableQualities},
           {"raw", out.raw},
       }},
  };
}

}  // namespace

SongUrlService::SongUrlService()
    : SongUrlService(
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

SongUrlService::SongUrlService(SongUrlHttpGet httpGet)
    : httpGet_(std::move(httpGet)),
      httpPost_([](const std::string& url,
                    const std::string& body,
                    const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        return client.Post(url, body, headers);
      }) {}

SongUrlService::SongUrlService(SongUrlHttpGet httpGet, SongUrlHttpPost httpPost)
    : httpGet_(std::move(httpGet)), httpPost_(std::move(httpPost)) {}

nlohmann::json SongUrlService::ResolveV6PrivUrl(
    std::string hash,
    std::string albumAudioId,
    std::string userId,
    std::string token,
    std::string vipToken,
    int vipType,
    const DeviceInfo& device) const {
  if (!httpPost_) {
    return EmptySongUrl(hash, "", "No HTTP POST handler available");
  }

  hash = NormalizeHash(std::move(hash));
  if (hash.empty()) {
    return EmptySongUrl(hash, "", "Missing song hash");
  }

  // ── 1. Build signed URL via KAR ──────────────────────────────────────────
  // v6/priv_url 与参考实现(song_url_new.js)同约：Standard(appid=1005) + Lite key salt
  // (参考硬编码 185672dd… 即使 appid=1005) + 会话 Cookie。
  const auto profile = GetKuGouProfile(KuGouEdition::Standard);
  KuGouAndroidRequest req;
  req.endpoint = "http://tracker.kugou.com/v6/priv_url";
  req.profile = profile;
  req.device = device;
  req.body = ""; // set after body construction
  req.includeSongUrlKey = true;
  if (!userId.empty()) req.params["userid"] = userId;
  if (!token.empty()) req.params["token"] = token;

  // Pre-set clienttime so URL and HTTP headers use the same value
  const std::string clienttime = std::to_string(std::time(nullptr));
  req.params["clienttime"] = clienttime;

  // mid still needed explicitly for SignKey in body's tracker_param (line below)
  const std::string mid = ResolveAndroidMid(device);
  const std::string dfid = device.dfid.empty() ? "-" : device.dfid;

  // ── 2. Build JSON body ───────────────────────────────────────────────────
  const auto collectTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  const std::string albumAudioIdStr = albumAudioId.empty() ? "0" : albumAudioId;
  // tracker_param.key 参照 MakcRe song_url_new.js：盐硬编码为 Lite(185672dd…)，
  // 即使 appid=1005(Standard) 也不用 Standard 盐——否则 tracker 报 20010 key invalid。
  const std::string key = SignKey(hash, mid, userId, profile.appid, KuGouSaltKind::Lite);
  // MakcRe sends userid as a number (Number(userid)), defaulting to 0
  const int useridNum = userId.empty() ? 0 : [userId] {
    try { return std::stoi(userId); } catch (...) { return 0; }
  }();

  nlohmann::json body = {
      {"area_code", "1"},
      {"behavior", "play"},
      {"qualities", {"128", "320", "flac", "high", "multitrack",
                     "viper_atmos", "viper_tape", "viper_clear", "super"}},
      {"resource", {
          {"album_audio_id", albumAudioIdStr},
          {"collect_list_id", "3"},
          {"collect_time", collectTimeMs},
          {"hash", hash},
          {"id", 0},
          {"page_id", 1},
          {"type", "audio"},
      }},
      {"token", token},
      {"tracker_param", {
          {"all_m", 1},
          {"auth", ""},
          {"is_free_part", 0},
          {"key", key},
          {"module_id", 0},
          {"need_climax", 1},
          {"need_xcdn", 1},
          {"open_time", ""},
          {"pid", GetConceptUrlParams().pid},
          {"pidversion", "3001"},
          {"priv_vip_type", "6"},
          {"viptoken", vipToken},
      }},
      {"userid", std::to_string(useridNum)},
      {"vip", vipType},
  };

  const std::string bodyStr = body.dump();
  req.body = bodyStr;
  const std::string url = BuildSignedUrl(req);

  // ── 5. HTTP POST ─────────────────────────────────────────────────────────
  auto result = httpPost_(
      url,
      bodyStr,
      {
          {"Content-Type", "application/json"},
          {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
          {"dfid", dfid},
          {"mid", mid},
          {"clienttime", clienttime},
          {"kg-rc", "1"},
          {"kg-thash", "5d816a0"},
          {"kg-rec", "1"},
          {"kg-rf", "B9EDA08A64250DEFFBCADDEE00F8F25F"},
          // 参照 MakcRe song_url_new.js：v6/priv_url 携带会话 cookie（dfid+token+userid），
          // 与 VIP 领取修复一致；缺失时 tracker 返回 token api error(20018)。
          {"Cookie", "token=" + token + ";userid=" + userId + ";KugooID=" + userId},
      });

  // Diagnostic log — 脱敏：body/resp 可能携带 token 与签名 play_url，
  // 经 FFI 日志回调写盘时会泄漏，统一走 RedactSensitive + TruncateForLog。
  {
    std::string bodyPreview = diagnostics::TruncateForLog(diagnostics::RedactSensitive(bodyStr));
    std::string respPreview = diagnostics::TruncateForLog(diagnostics::RedactSensitive(result.body));
    std::ostringstream log;
    log << "[SongUrl/V6PRIV] http=" << result.statusCode
        << " err=" << result.error
        << " body=" << bodyPreview
        << " resp=" << respPreview;
    ECHO_LOG("SongUrlV6", log.str());
  }

  auto upstream = nlohmann::json::parse(result.body, nullptr, false);
  if (upstream.is_discarded()) upstream = nlohmann::json{};
  auto finish = [&](nlohmann::json output) {
    return WithAttempts(output, nlohmann::json::array({MakeAttempt(
        "v6", result, upstream, userId.empty() || token.empty(),
        output.value("quality", ""), output.value("delivery", "unknown"))}));
  };
  if (!result.error.empty()) {
    return finish(EmptySongUrl(hash, "", "v6 HTTP error: " + result.error));
  }
  if (result.statusCode < 200 || result.statusCode >= 300) {
    return finish(EmptySongUrl(hash, "", "v6 upstream returned HTTP " + std::to_string(result.statusCode)));
  }
  if (!upstream.is_object() || upstream.empty()) {
    return finish(EmptySongUrl(hash, "", "v6 invalid JSON response"));
  }

  // ── 6. Parse response ────────────────────────────────────────────────────
  // v6 returns: { status, data: { url: [{ quality, url, ... }, ...] }, ... }
  const int status = ReadInt(upstream, "status", 0);
  const int errcode = ReadInt(upstream, "errcode", ReadInt(upstream, "error_code", 0));
  if (status == 0 && errcode != 0) {
    return finish(EmptySongUrl(hash, "", "v6 errcode " + std::to_string(errcode)));
  }

  std::string playUrl;
  std::string bestQuality;
  int bestBitRate = -1;  // 跟踪最高 bitrate，用于选择最佳音质
  nlohmann::json backupUrls = nlohmann::json::array();
  nlohmann::json availableQualities = nlohmann::json::array();
  // 元数据字段（v6 数组路径和 v5 路径都需要）
  std::string fileName;
  std::string songName;
  std::string singerName;
  int timeLength = 0;
  int bitRate = 0;
  std::string extName;

  // v6 API 返回格式: data 是数组 [{info: {filesize, extname, bitrate, ...}, ...}]
  // 需要从 data 数组中提取音质信息
  if (upstream.contains("data") && upstream["data"].is_array()) {
    for (const auto& item : upstream["data"]) {
      if (!item.is_object()) continue;
      
      // 提取 URL：v6 真实字段 tracker_url / tracker_backup_url（数组）位于 info 对象内；
      // 兼容旧字段 url / backupUrl（可能在 item 顶层）。
      auto readFirstUrl = [](const nlohmann::json& obj, const char* key) -> std::string {
        if (!obj.contains(key)) return {};
        const auto& v = obj[key];
        if (v.is_array() && !v.empty() && v[0].is_string()) return v[0].get<std::string>();
        if (v.is_string()) return v.get<std::string>();
        return {};
      };
      std::string itemUrl = readFirstUrl(item, "url");
      std::string itemBackup = readFirstUrl(item, "backupUrl");
      if (item.contains("info") && item["info"].is_object()) {
        if (itemUrl.empty()) itemUrl = readFirstUrl(item["info"], "tracker_url");
        if (itemBackup.empty()) itemBackup = readFirstUrl(item["info"], "tracker_backup_url");
        // en_tracker_url 是加密流（"en"=encrypted，实测内容不可 demux）——
        // 客户端无法解密，不能当作可播放 URL，刻意忽略让 v6 判失败后回落 v5。
      }
      
      // 提取音质信息从 info 对象
      if (item.contains("info") && item["info"].is_object()) {
        const auto& info = item["info"];
        const auto itemBitRate = ReadInt(info, "bitrate", 0);
        // 将 bitrate 转换为语义化标签，与前端 qualityLabels 一致
        std::string itemQuality;
        if (itemBitRate >= 2000) itemQuality = "master";
        else if (itemBitRate >= 1400) itemQuality = "flac";
        else if (itemBitRate >= 320) itemQuality = "320";
        else itemQuality = "128";
        const auto itemSize = ReadInt(info, "filesize", 0);
        const auto itemExt = ReadString(info, "extname");
        
        // 仅在有 URL 时才加入可用音质列表
        if (!itemUrl.empty() || !itemBackup.empty()) {
          const auto entryUrl = itemUrl.empty() ? itemBackup : itemUrl;
          const auto entryDelivery = ClassifyDelivery(
              entryUrl, {V6Source(ResponseLevel::kInfo, info),
                         V6Source(ResponseLevel::kItem, item),
                         V6Source(ResponseLevel::kEnvelope, upstream)});
          nlohmann::json qualityEntry = {
              {"quality", itemQuality},
              {"url", entryUrl},
              {"fileSize", itemSize},
              {"bitRate", itemBitRate},
              {"extName", itemExt},
              {"delivery", entryDelivery},
              {"is_preview", entryDelivery == "preview"},
              // 条目级元数据：音质切换时 data 层元数据随选中条目重算（F10）。
              {"fileName", ReadString(info, "fileName")},
              {"songName", ReadString(info, "songName")},
              {"singerName", ReadString(info, "singerName")},
              {"timeLength", ReadInt(info, "timeLength", 0)},
          };
          availableQualities.push_back(qualityEntry);
        }
        
        // 选择最高 bitrate 的音质作为默认播放 URL
        const auto candidateUrl = itemUrl.empty() ? itemBackup : itemUrl;
        if (!candidateUrl.empty() && itemBitRate > bestBitRate) {
          if (!playUrl.empty()) {
            backupUrls.push_back(playUrl);  // 旧的最佳降级为备份
          }
          playUrl = candidateUrl;
          bestBitRate = itemBitRate;
          bestQuality = itemQuality;
          fileName = ReadString(info, "fileName");
          songName = ReadString(info, "songName");
          singerName = ReadString(info, "singerName");
          timeLength = ReadInt(info, "timeLength", 0);
          bitRate = itemBitRate;
          extName = itemExt;
        } else if (!candidateUrl.empty()) {
          backupUrls.push_back(candidateUrl);
        }
      }
    }
  }
  // Fallback: v5 API 返回格式: data 是对象 {url: [...]}
  else if (upstream.contains("data") && upstream["data"].is_object()) {
    const auto& data = upstream["data"];
    
    if (data.contains("url") && data["url"].is_array()) {
      for (const auto& item : data["url"]) {
        if (!item.is_object()) continue;
        const auto itemUrl = ReadString(item, "url");
        if (itemUrl.empty()) continue;
        const auto itemQuality = ReadString(item, "quality");
        const auto itemSize = ReadInt(item, "fileSize", 0);
        const auto itemBitRate = ReadInt(item, "bitRate", 0);
        const auto itemExt = ReadString(item, "extName");
        availableQualities.push_back({
            {"quality", itemQuality},
            {"url", itemUrl},
            {"fileSize", itemSize},
            {"bitRate", itemBitRate},
            {"extName", itemExt},
            {"delivery", ClassifyDelivery(itemUrl, {V6Source(ResponseLevel::kItem, item), V6Source(ResponseLevel::kData, data), V6Source(ResponseLevel::kEnvelope, upstream)})},
            {"is_preview", ClassifyDelivery(itemUrl, {V6Source(ResponseLevel::kItem, item), V6Source(ResponseLevel::kData, data), V6Source(ResponseLevel::kEnvelope, upstream)}) == "preview"},
        });
        if (playUrl.empty()) {
          playUrl = itemUrl;
          bestQuality = itemQuality;
        } else {
          backupUrls.push_back(itemUrl);
        }
      }
    }
    // Fallback: some responses put url directly as string
    if (playUrl.empty()) {
      playUrl = ReadStringOrFirstArrayElement(data, "url");
    }
  }

  // Also check top-level url (some responses may flatten)
  if (playUrl.empty()) {
    playUrl = ReadStringOrFirstArrayElement(upstream, "url");
  }
  
  // v5 API 返回格式: 顶层 url 是字符串数组 ["http://...", ...]
  if (availableQualities.empty() && upstream.contains("url") && upstream["url"].is_array()) {
    const auto extNameV5 = ReadString(upstream, "extName");
    const auto bitRateV5 = ReadInt(upstream, "bitRate", 0);
    
    for (const auto& urlItem : upstream["url"]) {
      if (!urlItem.is_string()) continue;
      const auto urlStr = urlItem.get<std::string>();
      if (urlStr.empty()) continue;
      
      // 提取 quality 从 URL 参数 (qu128, qu320 等)
      std::string quality = "128";
      auto quPos = urlStr.find("_qu");
      if (quPos != std::string::npos && quPos + 3 < urlStr.size()) {
        auto endPos = urlStr.find_first_of("_.?&/", quPos + 3);
        if (endPos == std::string::npos) endPos = urlStr.size();
        quality = urlStr.substr(quPos + 3, endPos - quPos - 3);
      }
      
      availableQualities.push_back({
          {"quality", quality},
          {"url", urlStr},
          {"bitRate", bitRateV5},
          {"extName", extNameV5},
          {"delivery", ClassifyDelivery(urlStr, {V6Source(ResponseLevel::kEnvelope, upstream)})},
          {"is_preview", ClassifyDelivery(urlStr, {V6Source(ResponseLevel::kEnvelope, upstream)}) == "preview"},
      });
    }
  }

  auto delivery = ClassifyDelivery(playUrl, {V6Source(ResponseLevel::kData, upstream.value("data", nlohmann::json{})), V6Source(ResponseLevel::kEnvelope, upstream)});
  for (const auto& entry : availableQualities) {
    if (entry.value("url", "") == playUrl) {
      delivery = entry.value("delivery", "unknown");
      break;
    }
  }

  // Pass through other useful fields from v6 response
  // 元数据已在 v6 数组路径中提取；以下为 v5 对象路径的 fallback
  if (upstream.contains("data") && upstream["data"].is_object()) {
    const auto& data = upstream["data"];
    fileName = ReadString(data, "fileName");
    songName = ReadString(data, "songName");
    singerName = ReadString(data, "singerName");
    timeLength = ReadInt(data, "timeLength", 0);
    bitRate = ReadInt(data, "bitRate", 0);
    extName = ReadString(data, "extName");
  }

  return finish(BuildSongUrlOutput(SongUrlOutput{
      .hash = hash,
      .quality = bestQuality,
      .playUrl = playUrl,
      .delivery = delivery,
      .backupUrls = backupUrls,
      .availableQualities = availableQualities,
      .raw = upstream,
      .fileName = fileName,
      .songName = songName,
      .singerName = singerName,
      .timeLength = timeLength,
      .bitRate = bitRate,
      .extName = extName,
  }));
}

nlohmann::json SongUrlService::Resolve(
    std::string hash,
    std::string albumId,
    std::string albumAudioId,
    std::string quality,
    std::string ppageId,
    std::string userId,
    std::string token,
    const DeviceInfo& device,
    std::string vipToken) const {
  return Resolve(
      std::move(hash), std::move(albumId), std::move(albumAudioId),
      std::move(quality), std::move(ppageId), std::move(userId),
      std::move(token), device, std::move(vipToken), /*vipType=*/0);
}

nlohmann::json SongUrlService::Resolve(
    std::string hash,
    std::string albumId,
    std::string albumAudioId,
    std::string quality,
    std::string ppageId,
    std::string userId,
    std::string token,
    const DeviceInfo& device,
    std::string vipToken,
    int vipType) const {
  hash = NormalizeHash(std::move(hash));
  quality = Trim(std::move(quality));
  ppageId = Trim(std::move(ppageId));
  nlohmann::json attempts = nlohmann::json::array();
  auto finish = [&](nlohmann::json output) { return WithAttempts(std::move(output), attempts); };

  if (hash.empty()) {
    return finish(EmptySongUrl(hash, quality, "Missing song hash"));
  }

  // ── Try v6/priv_url first (VIP-aware endpoint) ──────────────────────────
  nlohmann::json v6PreviewFallback;
  if (httpPost_) {
    auto v6 = ResolveV6PrivUrl(hash, albumAudioId, userId, token,
                                std::move(vipToken), vipType, device);
    attempts = v6.value("attempts", nlohmann::json::array());
    if (v6.value("status", 0) == 1) {
      // 2026-09-03 实测：缺 viptoken 时 v6 会"成功"但只回试听包
      // （is_preview=true、URL 带 /yp/p_ 字节区间、fail_process=12），
      // 会把整条链路钉死在试听上。只有这种真试听包才降级去 v5；
      // 合成/完整地址（无 /yp/p_ 标记）照常采用。
      const std::string v6Url = v6.value("url", std::string{});
      const bool v6Degraded = v6.value("is_preview", false)
          && v6Url.find("/yp/p_") != std::string::npos;
      if (!v6Degraded) {
        if (!quality.empty() && v6.contains("data") && v6["data"].is_object()) {
          auto& data = v6["data"];
          if (data.contains("available_qualities") && data["available_qualities"].is_array()) {
            for (const auto& candidate : data["available_qualities"]) {
              if (!candidate.is_object()) continue;
              if (candidate.value("quality", "") != quality) continue;
              const auto preferredUrl = candidate.value("url", "");
              if (preferredUrl.empty()) continue;
              v6["url"] = preferredUrl;
              v6["play_url"] = preferredUrl;
              v6["playUrl"] = preferredUrl;
              data["url"] = preferredUrl;
              data["play_url"] = preferredUrl;
              data["playUrl"] = preferredUrl;
              data["quality"] = quality;
              v6["quality"] = quality; // 顶层 quality 由 BuildSongUrlOutput 提供（最高码率候选），切换后同步
              // Read the entry's classification, including its rights/offset
              // evidence, so the aggregate cannot diverge on quality changes.
              const bool switchedIsPreview = candidate.value("is_preview", false);
              v6["is_preview"] = switchedIsPreview;
              data["is_preview"] = switchedIsPreview;
              v6["delivery"] = candidate.value("delivery", "unknown");
              data["delivery"] = v6["delivery"];
              // F10 元数据随选中条目重算：码率/扩展名/时长/文件名等描述
              // 最终播放的条目，而不是最高码率候选。
              if (candidate.contains("bitRate")) {
                data["bit_rate"] = candidate["bitRate"];
                data["bitRate"] = candidate["bitRate"];
              }
              if (candidate.contains("extName")) {
                data["ext_name"] = candidate["extName"];
                data["extName"] = candidate["extName"];
              }
              if (candidate.contains("timeLength")) {
                data["time_length"] = candidate["timeLength"];
                data["timeLength"] = candidate["timeLength"];
              }
              if (candidate.contains("fileName")) {
                data["file_name"] = candidate["fileName"];
                data["fileName"] = candidate["fileName"];
              }
              if (candidate.contains("songName")) {
                data["song_name"] = candidate["songName"];
                data["songName"] = candidate["songName"];
              }
              if (candidate.contains("singerName")) {
                data["singer_name"] = candidate["singerName"];
                data["singerName"] = candidate["singerName"];
              }
              break;
            }
          }
        }
        ECHO_LOG("SongUrlV6", "SUCCESS — using v6 result");
        if (!attempts.empty()) {
          attempts.back()["quality"] = v6.value("quality", "");
          attempts.back()["delivery"] = v6.value("delivery", "unknown");
          attempts.back()["is_preview"] = v6.value("is_preview", false);
        }
        return finish(std::move(v6));
      }
      ECHO_LOG("SongUrlV6", "DEGRADED (preview-only) — falling back to v5");
      v6PreviewFallback = std::move(v6);
    } else {
      ECHO_LOG("SongUrlV6", "FAILED — falling back to v5");
    }
  }

  // A missing transport verb must fail loudly instead of invoking an empty
  // std::function (which throws std::bad_function_call and terminates the
  // process). Symmetric with the POST guard at the top of ResolveV6PrivUrl: a
  // host that injects only one verb must still receive a JSON answer.
  if (!httpGet_) {
    if (!v6PreviewFallback.empty()) return finish(std::move(v6PreviewFallback));
    return finish(EmptySongUrl(hash, quality, "No HTTP GET handler available"));
  }

  // ── v5/url fallback ─────────────────────────────────────────────────────
  // Once the device is registered with KuGou (DeviceRegisterService), its
  // dfid/mid/uuid become "trusted" and /v5/url returns full VIP URLs. Without
  // registration we previously zeroed these out as a workaround, but KuGou
  // then treated us as anonymous and only served 60s previews. The DeviceInfo
  // here carries the *registered* fingerprint when device.registered is true.
  // 2026-09-03 实测分流：业务接口（歌单/会员查询）认标准族，但歌链
  // tracker 端点认概念族——18:27 概念族 v5 给了全无损，02:13 标准族 v5 回
  // pkg/buy 只给 60 秒。故 v5 保持概念族（3116/411/967177915），
  // clientver 仍按 dataMap 覆盖 11430。
  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  std::unordered_map<std::string, std::string> params;
  params["album_id"] = albumId.empty() ? "0" : albumId;
  params["area_code"] = "1";
  params["hash"] = hash;
  params["ssa_flag"] = "is_fromtrack";
  params["version"] = V5UrlClientver;
  const auto conceptUrls = GetConceptUrlParams();
  params["page_id"] = conceptUrls.pageId;
  params["quality"] = quality.empty() ? "128" : quality;
  params["album_audio_id"] = albumAudioId.empty() ? "0" : albumAudioId;
  params["behavior"] = "play";
  params["pid"] = conceptUrls.pid;
  params["cmd"] = "26";
  params["pidversion"] = "3001";
  params["IsFreePart"] = "0";
  params["ppage_id"] = ppageId.empty() ? conceptUrls.ppageId : ppageId;
  params["cdnBackup"] = "1";
  params["module"] = "";
  // CRITICAL: mid 必须是 38-39 位十进制安卓 mid（ResolveAndroidMid 产出），
  // 与 appid 族匹配，否则上游静默不给会员音质。
  params["appid"] = profile.appid;
  params["clientver"] = V5UrlClientver;
  params["mid"] = ResolveAndroidMid(device);
  params["dfid"] = device.dfid.empty() ? "-" : device.dfid;
  params["uuid"] = "-";  // MakcRe request.js:36 default

  if (!userId.empty() && !token.empty()) {
    params["userid"] = userId;
    params["token"] = token;
  }

  auto callUpstream = [&](std::unordered_map<std::string, std::string> p, const char* endpoint)
      -> std::pair<HttpResult, nlohmann::json> {
    KuGouAndroidRequest v5req;
    v5req.endpoint = "https://gateway.kugou.com/v5/url";
    v5req.profile = GetKuGouProfile(KuGouEdition::Concept);
    v5req.profile.clientver = V5UrlClientver;
    v5req.device = device;
    v5req.includeSongUrlKey = true;
    v5req.params = std::map<std::string, std::string>(p.begin(), p.end());
    // Pre-set clienttime so URL and HTTP headers use the same value
    if (!v5req.params.count("clienttime")) {
      v5req.params["clienttime"] = std::to_string(std::time(nullptr));
    }
    const std::string url = BuildSignedUrl(v5req);
    // Extract request-level headers from params (consistent with URL values)
    const std::string requestDfid = v5req.params.count("dfid") ? v5req.params.at("dfid") : "-";
    const std::string requestMid = v5req.params.count("mid") ? v5req.params.at("mid") : "0";
    const std::string requestClientTime = v5req.params.at("clienttime");
    auto result = httpGet_(
        url,
        {
            {"Accept", "application/json"},
            {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
            {"x-router", "trackercdn.kugou.com"},
            {"dfid", requestDfid},
            {"clienttime", requestClientTime},
            {"mid", requestMid},
            {"kg-rc", "1"},
            {"kg-thash", "5d816a0"},
            {"kg-rec", "1"},
            // FULL 32-char value from MakcRe util/request.js:41. Truncating
            // to 28 chars (which the code had been doing) makes KuGou's risk
            // service treat the request as fingerprint-altered.
            {"kg-rf", "B9EDA08A64250DEFFBCADDEE00F8F25F"}
        });
    // Diagnostic: log auth presence + KuGou response. Tag each call with the
    // path kind so logs from main path / preview-retry / anonymous-fallback
    // can be told apart at a glance. The tag is inferred from the params:
    //   - userid/token absent + IsFreePart=1 → tryPreview (offset_hash retry)
    //   - userid/token absent + IsFreePart=0 → anonymous fallback
    //   - userid/token present → main path
    {
      const bool hasUserId = url.find("&userid=") != std::string::npos
                          || url.find("?userid=") != std::string::npos;
      const bool hasToken = url.find("&token=") != std::string::npos
                         || url.find("?token=") != std::string::npos;
      const bool isFreePart = url.find("IsFreePart=1") != std::string::npos;
      const char* pathKind = hasToken ? "MAIN" : (isFreePart ? "PREVIEW" : "ANON");
      // 脱敏：URL 带 token/userid，resp 带签名 play_url，写盘会泄漏。
      std::string urlPreview = diagnostics::TruncateForLog(diagnostics::RedactSensitive(url), 400);
      std::string bodyPreview = diagnostics::TruncateForLog(diagnostics::RedactSensitive(result.body));
      std::ostringstream log;
      log << "phase=" << pathKind
          << " http=" << result.statusCode
          << " hasUserId=" << (hasUserId ? "Y" : "N")
          << " hasToken=" << (hasToken ? "Y" : "N")
          << " url=" << urlPreview
          << " body=" << bodyPreview;
      ECHO_LOG("SongUrlV5", log.str());
    }
    auto parsed = nlohmann::json::parse(result.body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) parsed = nlohmann::json::object();
    const auto urlDelivery = ClassifyDelivery(ReadStringOrFirstArrayElement(parsed, "url"),
        {V5Source(ResponseLevel::kEnvelope, parsed)}, p["IsFreePart"] == "1");
    attempts.push_back(MakeAttempt(endpoint, result, parsed,
        !p.count("userid") || !p.count("token"), p["quality"], urlDelivery));
    return {std::move(result), std::move(parsed)};
  };

  auto [result, upstream] = callUpstream(params, "v5-main");

  if (!result.error.empty()) {
    return finish(EmptySongUrl(hash, quality, result.error));
  }

  if (result.statusCode < 200 || result.statusCode >= 300) {
    return finish(EmptySongUrl(hash, quality, "Kugou song URL upstream returned an error"));
  }

  if (upstream.is_null() || upstream.empty()) {
    return finish(EmptySongUrl(hash, quality, "Invalid Kugou song URL JSON"));
  }

  std::string playUrl = ReadStringOrFirstArrayElement(upstream, "url");
  std::string upstreamHash = ReadString(upstream, "hash");
  nlohmann::json backupUrl = NormalizeBackupUrl(upstream.value("backup_url", nlohmann::json::array()));
  bool ok = !playUrl.empty();
  bool isPreview = false;
  // Preserve the legacy vip_required field from the main response. Delivery
  // is classified separately on the final URL; this rejection may be followed
  // by an anonymous full stream and is retained in attempts for diagnosis.
  const bool mainPathVipBlocked = HasFailProcess(upstream, {"pkg", "buy"});
  const bool hasShortOffset = upstream.contains("hash_offset")
      && upstream["hash_offset"].is_object()
      && ReadInt(upstream["hash_offset"], "end_ms", 0) > 0
      && ReadInt(upstream["hash_offset"], "end_ms", 0) <= 65000;
  const bool vipLocked = mainPathVipBlocked || hasShortOffset;

  auto tryPreview = [&](const nlohmann::json& source, std::unordered_map<std::string, std::string> sourceParams) {
    if (!source.contains("hash_offset") || !source["hash_offset"].is_object()) {
      return false;
    }
    const auto offsetHash = ReadString(source["hash_offset"], "offset_hash");
    if (!offsetHash.empty()) {
      auto previewParams = std::move(sourceParams);
      ClearAuth(previewParams);
      std::string lowerOffset = offsetHash;
      std::transform(lowerOffset.begin(), lowerOffset.end(), lowerOffset.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      previewParams["hash"] = lowerOffset;
      previewParams["IsFreePart"] = "1";
      auto [previewResult, previewUpstream] = callUpstream(previewParams, "v5-preview");
      if (previewResult.error.empty() && previewResult.statusCode >= 200 &&
          previewResult.statusCode < 300 && !previewUpstream.is_null()) {
        const auto previewPlayUrl = ReadStringOrFirstArrayElement(previewUpstream, "url");
        if (!previewPlayUrl.empty()) {
          playUrl = previewPlayUrl;
          upstreamHash = ReadString(previewUpstream, "hash");
          if (upstreamHash.empty()) upstreamHash = offsetHash;
          backupUrl = NormalizeBackupUrl(previewUpstream.value("backup_url", nlohmann::json::array()));
          upstream = std::move(previewUpstream);
          ok = true;
          isPreview = true;
          return true;
        }
      }
    }
    return false;
  };

  // Fallback: if upstream rejected the full track but provided a free-preview
  // segment (hash_offset.offset_hash), retry without login as IsFreePart=1.
  // KuGou returns "no free part info" when the preview request carries a
  // non-VIP token, while anonymous preview works for the same offset hash.
  if (!ok) {
    tryPreview(upstream, params);
  }

  if (!ok) {
    auto anonymousParams = params;
    ClearAuth(anonymousParams);
    auto [anonymousResult, anonymousUpstream] = callUpstream(anonymousParams, "v5-anon");
    if (anonymousResult.error.empty() && anonymousResult.statusCode >= 200 &&
        anonymousResult.statusCode < 300 && !anonymousUpstream.is_null()) {

      const auto anonymousPlayUrl = ReadStringOrFirstArrayElement(anonymousUpstream, "url");
      if (!anonymousPlayUrl.empty()) {
        playUrl = anonymousPlayUrl;
        upstreamHash = ReadString(anonymousUpstream, "hash");
        backupUrl = NormalizeBackupUrl(anonymousUpstream.value("backup_url", nlohmann::json::array()));
        upstream = std::move(anonymousUpstream);
        ok = true;
        // An anonymous response can supply a full free track. Do not carry
        // the earlier main-path rejection into this URL's classification.
        isPreview = false; // Classification below uses this response's evidence.
      } else {
        tryPreview(anonymousUpstream, anonymousParams);
      }

      if (!ok && upstream.empty()) {
        upstream = std::move(anonymousUpstream);
      }
    }
  }

  std::string error = upstream.value("error", "");
  std::string errorCode = ok ? "" : "native_song_url_empty";
  bool vipRequiredOut = vipLocked;
  if (!ok && error.empty()) {
    const bool needsVip = HasFailProcess(upstream, {"pkg", "buy", "vip"});
    const int upstreamStatus = ReadInt(upstream, "status", 0);
    const int errcode = ReadInt(upstream, "errcode", ReadInt(upstream, "error_code", 0));
    // Prefer attempt ledger: anonymous pkg/buy must not erase earlier auth rejects,
    // and bare 20018 must not be promoted to "missing VIP package".
    bool attemptsAuthRejected = false;
    bool attemptsEntitlement = false;
    for (const auto& attempt : attempts) {
      if (!attempt.is_object()) continue;
      const auto cls = attempt.value("reject_class", std::string{});
      if (cls == "auth_rejected") attemptsAuthRejected = true;
      if (cls == "entitlement_required") attemptsEntitlement = true;
    }
    if (attemptsAuthRejected || (!needsVip && !attemptsEntitlement && errcode == 20018)) {
      // Auth rejection outranks a later anonymous pkg/buy when no URL was obtained.
      error = "账号鉴权被上游拒绝，无法获取完整音源（详见 attempts）";
      errorCode = "native_song_auth_rejected";
      vipRequiredOut = false;
    } else if (needsVip || attemptsEntitlement) {
      error = userId.empty() ? "此歌曲需要登录 VIP 账号才能播放" : "此歌曲需要 VIP 会员，请先领取或开通 VIP";
      errorCode = "native_song_vip_required";
      vipRequiredOut = true;
    } else if (upstreamStatus == 2) {
      error = "酷狗未返回播放地址（可能受版权或地区限制）";
      errorCode = "native_song_url_blocked";
    } else if (userId.empty()) {
      error = "未登录，无法获取播放地址";
      errorCode = "native_song_url_no_session";
    }
  }

  const auto delivery = ClassifyDelivery(playUrl, {V5Source(ResponseLevel::kEnvelope, upstream)}, isPreview);

  // 从 v5 upstream 提取当前音质信息（v5 只返回请求的那一个音质）
  nlohmann::json v5Qualities = nlohmann::json::array();
  if (!playUrl.empty()) {
    const auto extNameV5 = upstream.value("extName", "");
    const auto bitRateV5 = upstream.value("bitRate", 0);
    // 从请求参数或 URL 中推断音质标签
    std::string qLabel = quality.empty() ? "128" : quality;
    // 也从 URL 的 _qu 参数验证
    auto quPos = playUrl.find("_qu");
    if (quPos != std::string::npos && quPos + 3 < playUrl.size()) {
      auto endPos = playUrl.find_first_of("_.?&/", quPos + 3);
      if (endPos == std::string::npos) endPos = playUrl.size();
      qLabel = playUrl.substr(quPos + 3, endPos - quPos - 3);
    }
    v5Qualities.push_back({
        {"quality", qLabel},
        {"url", playUrl},
        {"bitRate", bitRateV5},
        {"extName", extNameV5},
        {"delivery", delivery},
        {"is_preview", delivery == "preview"},
    });
  }

  // v5 全空时回退到 v6 的试听包（有声音总比没有好）。
  if (playUrl.empty() && !v6PreviewFallback.empty()) {
    ECHO_LOG("SongUrlV6", "v5 empty — using v6 preview fallback");
    return finish(std::move(v6PreviewFallback));
  }

  return finish(BuildSongUrlOutput(SongUrlOutput{
      .hash = upstreamHash.empty() ? hash : upstreamHash,
      .reqHash = std::string(upstream.value("req_hash", hash)),
      .quality = quality,
      .playUrl = playUrl,
      .delivery = delivery,
      .vipRequired = ok ? vipLocked : vipRequiredOut,
      .backupUrls = backupUrl,
      .availableQualities = v5Qualities,
      .raw = upstream,
      .error = error,
      .errorCode = errorCode,
      .fileName = std::string(upstream.value("fileName", "")),
      .songName = std::string(upstream.value("songName", "")),
      .singerName = std::string(upstream.value("singerName", "")),
      .timeLength = upstream.value("timeLength", 0),
      .bitRate = upstream.value("bitRate", 0),
      .extName = std::string(upstream.value("extName", "")),
      .albumid = upstream.value("albumid", 0),
      .albumAudioId = upstream.value("album_audio_id", 0),
      .audioId = upstream.value("audio_id", 0),
      .privilege = upstream.value("privilege", 0),
      .payType = upstream.value("pay_type", 0),
  }));
}

nlohmann::json SongUrlService::Resolve(
    std::string hash, std::string albumId, std::string albumAudioId) const {
  return Resolve(
      std::move(hash), std::move(albumId), std::move(albumAudioId),
      /*quality=*/"", /*ppageId=*/"", /*userId=*/"", /*token=*/"", DeviceInfo{});
}

}  // namespace echo::core
