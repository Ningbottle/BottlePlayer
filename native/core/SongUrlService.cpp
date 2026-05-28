#include "echo/core/SongUrlService.h"
#include "echo/core/Crypto.h"
#include "echo/core/KuGouProfile.h"

#include <chrono>
#include <ctime>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
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

std::string ResolveAndroidMid(const DeviceInfo& device) {
  const bool storedMidLooksAndroid =
      device.mid.size() >= 38 &&
      device.mid.size() <= 39 &&
      std::all_of(device.mid.begin(), device.mid.end(),
                  [](unsigned char c) { return std::isdigit(c); });
  if (storedMidLooksAndroid) return device.mid;
  if (!device.guid.empty()) return CalculateAndroidMid(device.guid);
  if (!device.mid.empty()) return CalculateAndroidMid(device.mid);
  return "0";
}

std::string ReadStringOrFirstArrayElement(const nlohmann::json& value, std::string_view key) {
  if (!value.contains(key)) return "";
  const auto& item = value.at(key);
  if (item.is_string()) return item.get<std::string>();
  if (item.is_array() && !item.empty() && item[0].is_string()) return item[0].get<std::string>();
  return "";
}

std::string BuildV5Url(
    const std::string& baseUrl,
    std::unordered_map<std::string, std::string> params) {
  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  if (params.find("appid") == params.end()) params["appid"] = profile.appid;
  if (params.find("clientver") == params.end()) {
    params["clientver"] = profile.clientver;
  }
  if (params.find("clienttime") == params.end()) {
    params["clienttime"] = std::to_string(std::time(nullptr));
  }
  if (params.find("mid") == params.end()) params["mid"] = "0";
  if (params.find("uuid") == params.end()) params["uuid"] = "0";
  if (params.find("dfid") == params.end()) params["dfid"] = "-";

  std::string hash = params.count("hash") ? params["hash"] : "";
  std::string appid = params["appid"];
  std::string mid = params["mid"];
  std::string userid = params.count("userid") ? params["userid"] : "0";

  // Concept (3116) and lite (1014) both use the lite key salt. Empirically the
  // user's concept-edition mid only validates with this salt; switching 3116 to
  // standard salt produces errcode 31833 "illegal key".
  params["key"] = SignKey(hash, mid, userid, appid, profile.saltKind);
  
  params["signature"] = SignatureAndroidParams(params, "", profile.saltKind);

  std::ostringstream urlStream;
  urlStream << baseUrl << "?";
  bool first = true;
  for (const auto& [key, value] : params) {
    if (!first) urlStream << "&";
    urlStream << key << "=" << UrlEncode(value);
    first = false;
  }
  return urlStream.str();
}

nlohmann::json EmptySongUrl(std::string hash, std::string quality, std::string error) {
  return {
      {"status", 0},
      {"error_code", error.empty() ? "native_song_url_empty" : "native_song_url_failed"},
      {"error", std::move(error)},
      {"url", ""},
      {"play_url", ""},
      {"playUrl", ""},
      {"data",
       {
           {"hash", std::move(hash)},
           {"quality", std::move(quality)},
           {"url", ""},
           {"play_url", ""},
           {"playUrl", ""},
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

void ClearAuth(std::unordered_map<std::string, std::string>& params) {
  params.erase("userid");
  params.erase("token");
  params["dfid"] = "-";
  params["mid"] = "0";
  params["uuid"] = "-";
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

  hash = Trim(std::move(hash));
  std::transform(hash.begin(), hash.end(), hash.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (hash.empty()) {
    return EmptySongUrl(hash, "", "Missing song hash");
  }

  // ── 1. Signed query params ───────────────────────────────────────────────
  const std::string clienttime = std::to_string(std::time(nullptr));
  const std::string mid = ResolveAndroidMid(device);
  const std::string dfid = device.dfid.empty() ? "-" : device.dfid;

  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  std::unordered_map<std::string, std::string> queryParams;
  queryParams["appid"] = profile.appid;
  queryParams["clientver"] = profile.clientver;
  queryParams["clienttime"] = clienttime;
  queryParams["dfid"] = dfid;
  queryParams["mid"] = mid;
  queryParams["uuid"] = "-";
  if (!userId.empty()) queryParams["userid"] = userId;
  if (!token.empty()) queryParams["token"] = token;

  // ── 2. Build JSON body ───────────────────────────────────────────────────
  const auto collectTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  const std::string albumAudioIdStr = albumAudioId.empty() ? "0" : albumAudioId;
  const std::string key = SignKey(hash, mid, userId, profile.appid, profile.saltKind);
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

  // ── 3. Compute signature over query params + body ────────────────────────
  queryParams["signature"] = SignatureAndroidParams(queryParams, bodyStr, profile.saltKind);

  // ── 4. Build full URL ────────────────────────────────────────────────────
  std::ostringstream urlStream;
  urlStream << "http://tracker.kugou.com/v6/priv_url?";
  bool first = true;
  for (const auto& [k, v] : queryParams) {
    if (!first) urlStream << "&";
    urlStream << k << "=" << UrlEncode(v);
    first = false;
  }
  const std::string url = urlStream.str();

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
      });

  // Diagnostic log
  {
    std::string bodyPreview = bodyStr.size() > 600 ? bodyStr.substr(0, 600) + "..." : bodyStr;
    std::string respPreview = result.body.size() > 800 ? result.body.substr(0, 800) + "..." : result.body;
    std::cerr << "[SongUrl/V6PRIV] http=" << result.statusCode
              << " err=" << result.error
              << " body=" << bodyPreview
              << " resp=" << respPreview << std::endl;
  }

  if (!result.error.empty()) {
    return EmptySongUrl(hash, "", "v6 HTTP error: " + result.error);
  }
  if (result.statusCode < 200 || result.statusCode >= 300) {
    return EmptySongUrl(hash, "", "v6 upstream returned HTTP " + std::to_string(result.statusCode));
  }

  nlohmann::json upstream;
  try {
    upstream = nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception&) {
    return EmptySongUrl(hash, "", "v6 invalid JSON response");
  }

  if (upstream.is_null() || upstream.empty()) {
    return EmptySongUrl(hash, "", "v6 empty response");
  }

  // ── 6. Parse response ────────────────────────────────────────────────────
  // v6 returns: { status, data: { url: [{ quality, url, ... }, ...] }, ... }
  const int status = ReadInt(upstream, "status", 0);
  const int errcode = ReadInt(upstream, "errcode", ReadInt(upstream, "error_code", 0));
  if (status == 0 && errcode != 0) {
    return EmptySongUrl(hash, "", "v6 errcode " + std::to_string(errcode));
  }

  std::string playUrl;
  std::string bestQuality;
  nlohmann::json backupUrls = nlohmann::json::array();

  if (upstream.contains("data") && upstream["data"].is_object()) {
    const auto& data = upstream["data"];
    if (data.contains("url") && data["url"].is_array()) {
      for (const auto& item : data["url"]) {
        if (!item.is_object()) continue;
        const auto itemUrl = ReadString(item, "url");
        if (itemUrl.empty()) continue;
        const auto itemQuality = ReadString(item, "quality");
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

  const bool ok = !playUrl.empty();
  const bool hasFullSegment = playUrl.find("/yp/full/") != std::string::npos
                           || playUrl.find("/full/") != std::string::npos;
  const bool isPreview = ok && !hasFullSegment;

  // Pass through other useful fields from v6 response
  std::string fileName;
  std::string songName;
  std::string singerName;
  int timeLength = 0;
  int bitRate = 0;
  std::string extName;
  if (upstream.contains("data") && upstream["data"].is_object()) {
    const auto& data = upstream["data"];
    fileName = ReadString(data, "fileName");
    songName = ReadString(data, "songName");
    singerName = ReadString(data, "singerName");
    timeLength = ReadInt(data, "timeLength", 0);
    bitRate = ReadInt(data, "bitRate", 0);
    extName = ReadString(data, "extName");
  }

  return {
      {"status", ok ? 1 : 0},
      {"error", ""},
      {"error_code", ""},
      {"url", playUrl},
      {"play_url", playUrl},
      {"playUrl", playUrl},
      {"is_preview", isPreview},
      {"vip_required", false},
      {"data",
       {
           {"hash", hash},
           {"req_hash", hash},
           {"quality", bestQuality.empty() ? "" : bestQuality},
           {"url", playUrl},
           {"play_url", playUrl},
           {"playUrl", playUrl},
           {"is_preview", isPreview},
           {"vip_required", false},
           {"backup_url", backupUrls},
           {"fileName", fileName},
           {"songName", songName},
           {"singerName", singerName},
           {"timeLength", timeLength},
           {"bitRate", bitRate},
           {"extName", extName},
           {"raw", upstream},
       }},
  };
}

nlohmann::json SongUrlService::Resolve(
    std::string hash,
    std::string albumId,
    std::string albumAudioId,
    std::string quality,
    std::string ppageId,
    std::string userId,
    std::string token,
    const DeviceInfo& device) const {
  hash = Trim(std::move(hash));
  quality = Trim(std::move(quality));
  ppageId = Trim(std::move(ppageId));

  // KuGou v5/url requires lowercase hash; search results return uppercase (e.g. "ABC123").
  std::transform(hash.begin(), hash.end(), hash.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  if (hash.empty()) {
    return EmptySongUrl(hash, quality, "Missing song hash");
  }

  // ── Try v6/priv_url first (VIP-aware endpoint) ──────────────────────────
  if (httpPost_) {
    auto v6 = ResolveV6PrivUrl(hash, albumAudioId, userId, token,
                                /*vipToken=*/"", /*vipType=*/0, device);
    if (v6.value("status", 0) == 1) {
      std::cerr << "[SongUrl/V6PRIV] SUCCESS — using v6 result" << std::endl;
      return v6;
    }
    std::cerr << "[SongUrl/V6PRIV] FAILED — falling back to v5" << std::endl;
  }

  // ── v5/url fallback ─────────────────────────────────────────────────────
  // Once the device is registered with KuGou (DeviceRegisterService), its
  // dfid/mid/uuid become "trusted" and /v5/url returns full VIP URLs. Without
  // registration we previously zeroed these out as a workaround, but KuGou
  // then treated us as anonymous and only served 60s previews. The DeviceInfo
  // here carries the *registered* fingerprint when device.registered is true.
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
  // Mirror MakcRe/KuGouMusicApi module/song_url.js exactly. The /v5/url
  // endpoint is concept-edition (lite): appid=3116, clientver=11430 (the
  // module's dataMap explicitly overrides the lite default 11440).
  //
  // CRITICAL: KuGou's Android-family clients send `mid` as a 38-39 digit
  // DECIMAL string (calculateMid in MakcRe util/util.js: hex md5 → base16
  // BigInt → base10). The raw 32-char hex mid that m.kugou.com web sets in
  // cookies is NOT accepted by /v5/url with appid=3116 — KuGou silently
  // returns priv_status:0 + auth_through:[] (no VIP applied) when the mid
  // format doesn't match the appid family.
  params["appid"] = profile.appid;
  params["clientver"] = V5UrlClientver;
  params["mid"] = ResolveAndroidMid(device);
  params["dfid"] = device.dfid.empty() ? "-" : device.dfid;
  params["uuid"] = "-";  // MakcRe request.js:36 default

  if (!userId.empty() && !token.empty()) {
    params["userid"] = userId;
    params["token"] = token;
  }

  auto callUpstream = [&](std::unordered_map<std::string, std::string> p)
      -> std::pair<HttpResult, nlohmann::json> {
    if (p.find("clienttime") == p.end()) {
      p["clienttime"] = std::to_string(std::time(nullptr));
    }
    const std::string requestDfid = p.find("dfid") != p.end() ? p["dfid"] : "-";
    const std::string requestMid = p.find("mid") != p.end() ? p["mid"] : "0";
    const std::string requestClientTime =
        p.find("clienttime") != p.end() ? p["clienttime"] : std::to_string(std::time(nullptr));
    const std::string url = BuildV5Url("https://gateway.kugou.com/v5/url", std::move(p));
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
      std::string urlPreview = url.size() > 400 ? url.substr(0, 400) + "..." : url;
      std::string bodyPreview = result.body.size() > 800 ? result.body.substr(0, 800) + "..." : result.body;
      std::cerr << "[SongUrl/" << pathKind << "] http=" << result.statusCode
                << " hasUserId=" << (hasUserId ? "Y" : "N")
                << " hasToken=" << (hasToken ? "Y" : "N")
                << " url=" << urlPreview
                << " body=" << bodyPreview << std::endl;
    }
    nlohmann::json parsed;
    if (result.error.empty() && result.statusCode >= 200 && result.statusCode < 300) {
      try {
        parsed = nlohmann::json::parse(result.body);
      } catch (const nlohmann::json::exception&) {
        parsed = nlohmann::json::object();
      }
    }
    return {std::move(result), std::move(parsed)};
  };

  auto [result, upstream] = callUpstream(params);

  if (!result.error.empty()) {
    return EmptySongUrl(hash, quality, result.error);
  }

  if (result.statusCode < 200 || result.statusCode >= 300) {
    return EmptySongUrl(hash, quality, "Kugou song URL upstream returned an error");
  }

  if (upstream.is_null() || upstream.empty()) {
    return EmptySongUrl(hash, quality, "Invalid Kugou song URL JSON");
  }

  std::string playUrl = ReadStringOrFirstArrayElement(upstream, "url");
  std::string upstreamHash = ReadString(upstream, "hash");
  nlohmann::json backupUrl = NormalizeBackupUrl(upstream.value("backup_url", nlohmann::json::array()));
  bool ok = !playUrl.empty();
  bool isPreview = false;
  // KuGou's main-path "VIP-locked" signal: fail_process containing "pkg"/"buy"
  // means the account has no entitlement and KuGou will only serve a 60s clip
  // (hash_offset.end_ms == 60000). Detect this BEFORE the tryPreview fallback
  // so we can mark isPreview correctly even when the preview URL fetch later
  // succeeds.
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
      auto [previewResult, previewUpstream] = callUpstream(previewParams);
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
    auto [anonymousResult, anonymousUpstream] = callUpstream(anonymousParams);
    if (anonymousResult.error.empty() && anonymousResult.statusCode >= 200 &&
        anonymousResult.statusCode < 300 && !anonymousUpstream.is_null()) {

      const auto anonymousPlayUrl = ReadStringOrFirstArrayElement(anonymousUpstream, "url");
      if (!anonymousPlayUrl.empty()) {
        playUrl = anonymousPlayUrl;
        upstreamHash = ReadString(anonymousUpstream, "hash");
        backupUrl = NormalizeBackupUrl(anonymousUpstream.value("backup_url", nlohmann::json::array()));
        upstream = std::move(anonymousUpstream);
        ok = true;
        // CRITICAL: KuGou's anonymous endpoint can still return /full/ URLs for
        // songs that don't actually require VIP. Only mark isPreview=true when
        // the URL path actually shows preview semantics (no "/full/" segment).
        // Real example: errcode 20028 (device-risk-control) on main path forces
        // anonymous fallback, but the song is free → /full/ comes back; user
        // saw "试听" banner incorrectly. URL pattern:
        //   /v3/<hash>/yp/full/...   (complete track)
        //   /v3/<hash>/yp/p_0_<n>/...(preview byte range, ~60s)
        const bool hasFullSegment = playUrl.find("/yp/full/") != std::string::npos
                                 || playUrl.find("/full/") != std::string::npos;
        isPreview = !hasFullSegment;
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
  if (!ok && error.empty()) {
    const bool needsVip = HasFailProcess(upstream, {"pkg", "buy", "vip"});
    const int upstreamStatus = ReadInt(upstream, "status", 0);
    const int errcode = ReadInt(upstream, "errcode", ReadInt(upstream, "error_code", 0));
    if (needsVip) {
      error = userId.empty() ? "此歌曲需要登录 VIP 账号才能播放" : "此歌曲需要 VIP 会员，请先领取或开通 VIP";
      errorCode = "native_song_vip_required";
    } else if (errcode == 20018) {
      error = "此歌曲需要 VIP 音乐包，当前账号无法获取完整音源";
      errorCode = "native_song_vip_required";
    } else if (upstreamStatus == 2) {
      error = "酷狗未返回播放地址（可能受版权或地区限制）";
      errorCode = "native_song_url_blocked";
    } else if (userId.empty()) {
      error = "未登录，无法获取播放地址";
      errorCode = "native_song_url_no_session";
    }
  }

  // Final preview flag: any of the explicit VIP-locked main-path signals or
  // the fallback paths that previously set isPreview.
  isPreview = isPreview || vipLocked;

  return {
      {"status", ok ? 1 : 0},
      {"error", error},
      {"error_code", errorCode},
      {"url", playUrl},
      {"play_url", playUrl},
      {"playUrl", playUrl},
      {"is_preview", isPreview},
      {"vip_required", vipLocked},
      {"data",
       {
           {"hash", upstreamHash.empty() ? hash : upstreamHash},
           {"req_hash", upstream.value("req_hash", hash)},
           {"quality", quality},
           {"url", playUrl},
           {"play_url", playUrl},
           {"playUrl", playUrl},
           {"is_preview", isPreview},
           {"vip_required", vipLocked},
           {"backup_url", backupUrl},
           {"fileName", upstream.value("fileName", "")},
           {"songName", upstream.value("songName", "")},
           {"singerName", upstream.value("singerName", "")},
           {"albumid", upstream.value("albumid", 0)},
           {"album_audio_id", upstream.value("album_audio_id", 0)},
           {"audio_id", upstream.value("audio_id", 0)},
           {"timeLength", upstream.value("timeLength", 0)},
           {"bitRate", upstream.value("bitRate", 0)},
           {"extName", upstream.value("extName", "")},
           {"privilege", upstream.value("privilege", 0)},
           {"pay_type", upstream.value("pay_type", 0)},
           {"raw", upstream},
       }},
  };
}

nlohmann::json SongUrlService::Resolve(
    std::string hash, std::string albumId, std::string albumAudioId) const {
  return Resolve(
      std::move(hash), std::move(albumId), std::move(albumAudioId),
      /*quality=*/"", /*ppageId=*/"", /*userId=*/"", /*token=*/"", DeviceInfo{});
}

}  // namespace echo::core
