#include "echo/core/SongService.h"
#include "echo/core/KuGouAndroidRequest.h"
#include "echo/core/KuGouProfile.h"

#include <sstream>

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

SongService::SongService()
    : SongService([](const std::string& url,
                     const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        return client.Get(url, headers);
      }) {}

SongService::SongService(SongHttpGet httpGet) : httpGet_(std::move(httpGet)) {}

nlohmann::json SongService::GetClimax(const std::string& hash) const {
  nlohmann::json data = nlohmann::json::array();
  std::stringstream ss(hash);
  std::string s;
  while (std::getline(ss, s, ',')) {
    if (!s.empty()) {
      data.push_back({{"hash", s}});
    }
  }

  KuGouAndroidRequest req;
  req.endpoint = "https://expendablekmrcdn.kugou.com/v1/audio_climax/audio";
  req.profile = GetKuGouProfile(KuGouEdition::Concept);
  req.params["data"] = data.dump();

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

nlohmann::json SongService::GetRanking(const std::string& albumAudioId) const {
  KuGouAndroidRequest req;
  req.endpoint = "https://gateway.kugou.com/grow/v1/song_ranking/play_page/ranking_info";
  req.profile = GetKuGouProfile(KuGouEdition::Concept);
  req.params["album_audio_id"] = albumAudioId;

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

nlohmann::json SongService::GetRankingFilter(const std::string& albumAudioId, int page, int pageSize) const {
  KuGouAndroidRequest req;
  req.endpoint = "https://gateway.kugou.com/grow/v1/song_ranking/unlock/v2/ranking_filter";
  req.profile = GetKuGouProfile(KuGouEdition::Concept);
  req.params["album_audio_id"] = albumAudioId;
  req.params["page"] = std::to_string(page);
  req.params["pagesize"] = std::to_string(pageSize);

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
