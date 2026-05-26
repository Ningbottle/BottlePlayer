#include "echo/core/SongUrlService.h"
#include "echo/core/Crypto.h"

#include <ctime>

#include <algorithm>
#include <cctype>
#include <iomanip>
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
  if (params.find("appid") == params.end()) params["appid"] = "1005";
  if (params.find("clientver") == params.end()) {
    const std::string appid = params["appid"];
    const bool isLite = (appid == "1014" || appid == "3116");
    params["clientver"] = isLite ? (appid == "1014" ? "10000" : "11440") : "11430";
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

  const bool isLiteKey = (appid == "1014"); // 3116 uses standard key string
  const std::string str = isLiteKey ? "185672dd44712f60bb1736df5a377e82" : "57ae12eb6890223e355ccfcb74edf70d";
  params["key"] = CalculateMd5(hash + str + appid + mid + userid);
  
  params["signature"] = SignatureAndroidParams(params, "");

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
    : SongUrlService([](
          const std::string& url,
          const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        return client.Get(url, headers);
      }) {}

SongUrlService::SongUrlService(SongUrlHttpGet httpGet) : httpGet_(std::move(httpGet)) {}

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

  // Once the device is registered with KuGou (DeviceRegisterService), its
  // dfid/mid/uuid become "trusted" and /v5/url returns full VIP URLs. Without
  // registration we previously zeroed these out as a workaround, but KuGou
  // then treated us as anonymous and only served 60s previews. The DeviceInfo
  // here carries the *registered* fingerprint when device.registered is true.
  std::unordered_map<std::string, std::string> params;
  params["album_id"] = albumId.empty() ? "0" : albumId;
  params["area_code"] = "1";
  params["hash"] = hash;
  params["ssa_flag"] = "is_fromtrack";
  params["version"] = "11430";
  params["page_id"] = "151369488";
  params["quality"] = quality.empty() ? "128" : quality;
  params["album_audio_id"] = albumAudioId.empty() ? "0" : albumAudioId;
  params["behavior"] = "play";
  params["pid"] = "2";
  params["cmd"] = "26";
  params["pidversion"] = "3001";
  params["IsFreePart"] = "0";
  params["ppage_id"] = ppageId.empty() ? "463467626,350369493,788954147" : ppageId;
  params["cdnBackup"] = "1";
  params["module"] = "";
  params["appid"] = "1005";
  params["clientver"] = "12143";
  params["mid"] = device.mid.empty() ? "0" : device.mid;
  params["dfid"] = device.dfid.empty() ? "-" : device.dfid;
  params["uuid"] = device.uuid.empty() ? "-" : device.uuid;

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
            {"kg-rf", "B9EDA08A64250DEFFBCADDEE00F8"}
        });
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
        isPreview = true;
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

  return {
      {"status", ok ? 1 : 0},
      {"error", error},
      {"error_code", errorCode},
      {"url", playUrl},
      {"play_url", playUrl},
      {"playUrl", playUrl},
      {"is_preview", isPreview},
      {"data",
       {
           {"hash", upstreamHash.empty() ? hash : upstreamHash},
           {"req_hash", upstream.value("req_hash", hash)},
           {"quality", quality},
           {"url", playUrl},
           {"play_url", playUrl},
           {"playUrl", playUrl},
           {"is_preview", isPreview},
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
