#include "echo/core/HomeService.h"
#include "echo/core/Crypto.h"
#include "echo/core/DeviceService.h"
#include "echo/core/KuGouAndroidRequest.h"
#include "echo/core/KuGouProfile.h"
#include "echo/core/SafeStoll.h"
#include "echo/core/StringUtils.h"

#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono>

namespace echo::core {
namespace {

nlohmann::json MakeError(const std::string& message, long statusCode = 0) {
  return {
      {"status", 0},
      {"error", message},
      {"status_code", statusCode},
      {"data", nlohmann::json::array()},
  };
}

}  // namespace

HomeService::HomeService()
    : HomeService(
          [](const std::string& url,
             const std::string& body,
             const std::unordered_map<std::string, std::string>& headers) {
            HttpClient client;
            return client.Post(url, body, headers);
          },
          [](const std::string& url,
             const std::unordered_map<std::string, std::string>& headers) {
            HttpClient client;
            return client.Get(url, headers);
          }) {}

HomeService::HomeService(HomeHttpPost httpPost)
    : httpPost_(std::move(httpPost)),
      httpGet_([](const std::string& url,
                  const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        return client.Get(url, headers);
      }) {}

HomeService::HomeService(HomeHttpPost httpPost, HomeHttpGet httpGet)
    : httpPost_(std::move(httpPost)), httpGet_(std::move(httpGet)) {}

nlohmann::json HomeService::GetBanners(
    const std::string& userId,
    const std::string& token) const {
  nlohmann::json dataPayload = {
      {"plat", 0},
      {"channel", 201},
      {"operator", 7},
      {"networktype", 2},
      {"userid", SafeStoll(userId)},
      {"vip_type", 0},
      {"m_type", 0},
      {"tags", nlohmann::json::array()},
      {"apiver", 5},
      {"ability", 2},
      {"mode", "normal"},
  };
  const std::string body = dataPayload.dump();

  // Home ads/recommend endpoints use a special "lite" platform identity:
  // appid=1014, clientver=20000, saltKind=Lite (default).
  // This does NOT correspond to KuGouEdition::Standard (1005/20489).
  KuGouAndroidRequest req;
  req.endpoint = "https://gateway.kugou.com/ads.gateway/v3/listen_banner";
  req.profile = GetKuGouProfile(KuGouEdition::Concept);  // Use Concept for saltKind=Lite
  req.profile.appid = "1014";  // Override: lite platform appid
  req.profile.clientver = "20000";  // Override: lite platform clientver
  req.body = body;
  req.params["plat"] = "1";
  if (!userId.empty() && userId != "0") req.params["userid"] = userId;
  if (!token.empty()) req.params["token"] = token;

  const std::string url = BuildSignedUrl(req);

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

nlohmann::json HomeService::GetEverydayRecommend(
    const std::string& userId,
    const std::string& token) const {
  // Empty body — the endpoint uses query params only.
  const std::string body;

  // Same lite platform identity as GetBanners.
  KuGouAndroidRequest req;
  req.endpoint = "https://gateway.kugou.com/everyday_song_recommend";
  req.profile = GetKuGouProfile(KuGouEdition::Concept);  // Use Concept for saltKind=Lite
  req.profile.appid = "1014";  // Override: lite platform appid
  req.profile.clientver = "20000";  // Override: lite platform clientver
  req.params["plat"] = "1";
  req.params["platform"] = "ios";
  if (!userId.empty() && userId != "0") req.params["userid"] = userId;
  if (!token.empty()) req.params["token"] = token;

  const std::string url = BuildSignedUrl(req);

  const auto result = httpPost_(
      url,
      body,
      {
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
          {"x-router", "everydayrec.service.kugou.com"},
      });

  if (!result.error.empty()) return MakeError(result.error, result.statusCode);

  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error: ") + e.what());
  }
}

nlohmann::json HomeService::GetPersonalFm(
    const std::string& userId,
    const std::string& token,
    const std::string& hash,
    const std::string& songId,
    int playtime,
    int remainSongCount,
    bool isOverplay,
    const DeviceInfo& device,
    const std::string& action,
    int songPoolId) const {
  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  const std::string clientTime = std::to_string(nowMs);
  const std::string mid = ResolveAndroidMid(device);

  nlohmann::json dataPayload = {
      {"appid", profile.appid},
      {"clienttime", nowMs},
      {"mid", mid},
      {"action", action.empty() ? "play" : action},
      {"recommend_source_locked", 0},
      {"song_pool_id", songPoolId},
      {"callerid", 0},
      {"m_type", 1},
      {"platform", "ios"},
      {"area_code", 1},
      {"remain_songcnt", std::max(0, remainSongCount)},
      {"clientver", profile.clientver},
      {"is_overplay", isOverplay ? 1 : 0},
      {"mode", "normal"},
      {"fakem", "ca981cfc583a4c37f28d2d49000013c16a0a"},
      {"key", SignParamsKey(clientTime, profile.appid, profile.clientver, profile.saltKind)},
  };

  if (!userId.empty() && userId != "0") {
    dataPayload["userid"] = userId;
    dataPayload["kguid"] = userId;
  }
  if (!token.empty()) dataPayload["token"] = token;
  if (!hash.empty()) dataPayload["hash"] = hash;
  if (!songId.empty()) dataPayload["songid"] = songId;
  if (playtime > 0) dataPayload["playtime"] = playtime;

  const std::string body = dataPayload.dump();

  KuGouAndroidRequest req;
  req.endpoint = "https://gateway.kugou.com/v2/personal_recommend";
  req.profile = profile;
  req.device = device;
  req.body = body;
  req.params["clienttime"] = clientTime;

  const std::string url = BuildSignedUrl(req);
  auto headers = BuildAndroidHeaders(req);
  headers["Content-Type"] = "application/json";
  headers["User-Agent"] = "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi";
  headers["x-router"] = "persnfm.service.kugou.com";

  const auto result = httpPost_(url, body, headers);

  if (!result.error.empty()) return MakeError(result.error, result.statusCode);

  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error: ") + e.what());
  }
}

nlohmann::json HomeService::GetImagesAudio(const std::string& hash,
                                           const std::string& audioId,
                                           const std::string& albumAudioId,
                                           const std::string& filename,
                                           int count) const {
  std::vector<std::string> hashes;
  {
    std::stringstream ss(hash);
    std::string s;
    while (std::getline(ss, s, ',')) {
      hashes.push_back(s);
    }
  }

  nlohmann::json data = nlohmann::json::array();
  for (const auto& h : hashes) {
    if (!h.empty()) {
      data.push_back({
          {"audio_id", 0},
          {"hash", h},
          {"album_audio_id", 0},
          {"filename", ""}
      });
    }
  }

  if (!audioId.empty()) {
    std::stringstream ss(audioId);
    std::string s;
    size_t index = 0;
    while (std::getline(ss, s, ',') && index < data.size()) {
      try {
        data[index]["audio_id"] = SafeStoll(s);
      } catch (...) {
        data[index]["audio_id"] = 0;
      }
      index++;
    }
  }

  if (!albumAudioId.empty()) {
    std::stringstream ss(albumAudioId);
    std::string s;
    size_t index = 0;
    while (std::getline(ss, s, ',') && index < data.size()) {
      try {
        data[index]["album_audio_id"] = SafeStoll(s);
      } catch (...) {
        data[index]["album_audio_id"] = 0;
      }
      index++;
    }
  }

  if (!filename.empty()) {
    std::stringstream ss(filename);
    std::string s;
    size_t index = 0;
    while (std::getline(ss, s, ',') && index < data.size()) {
      data[index]["filename"] = s;
      index++;
    }
  }

  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  KuGouAndroidRequest req;
  req.endpoint = "https://expendablekmr.kugou.com/v2/author_image/audio";
  req.profile = profile;
  req.skipDeviceDefaults = true;  // This endpoint doesn't use device fingerprint params
  req.params["count"] = std::to_string(count);
  req.params["data"] = data.dump();
  req.params["isCdn"] = "1";
  req.params["publish_time"] = "1";
  req.params["show_authors"] = "1";

  const std::string url = BuildSignedUrl(req);

  const auto result = httpGet_(url, {
    {"Accept", "application/json"},
    {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"}
  });

  if (!result.error.empty()) return MakeError(result.error, result.statusCode);

  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error: ") + e.what());
  }
}

}  // namespace echo::core
