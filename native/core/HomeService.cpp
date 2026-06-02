#include "echo/core/HomeService.h"
#include "echo/core/Crypto.h"
#include "echo/core/KuGouAndroidRequest.h"
#include "echo/core/KuGouProfile.h"
#include "echo/core/StringUtils.h"

#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>

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
      {"userid", userId.empty() ? 0 : std::stoll(userId)},
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
        data[index]["audio_id"] = s.empty() ? 0 : std::stoll(s);
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
        data[index]["album_audio_id"] = s.empty() ? 0 : std::stoll(s);
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
