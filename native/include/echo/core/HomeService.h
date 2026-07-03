#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/Dto.h"
#include "echo/core/HttpClient.h"

namespace echo::core {

using HomeHttpPost = std::function<HttpResult(
    const std::string& url,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& headers)>;

using HomeHttpGet = std::function<HttpResult(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers)>;

// Provides home-page discovery data: banners, everyday recommendations and audio images.
class HomeService {
 public:
  // Default: uses real HTTP client.
  HomeService();
  // Injectable constructor for testing.
  explicit HomeService(HomeHttpPost httpPost);
  HomeService(HomeHttpPost httpPost, HomeHttpGet httpGet);

  // Calls POST https://gateway.kugou.com/ads.gateway/v3/listen_banner.
  // Returns normalised banner list for the home screen.
  nlohmann::json GetBanners(const std::string& userId = "",
                            const std::string& token = "") const;

  // Calls POST https://gateway.kugou.com/everyday_song_recommend
  // (x-router: everydayrec.service.kugou.com).
  // Returns a list of daily recommended songs.
  nlohmann::json GetEverydayRecommend(const std::string& userId = "",
                                      const std::string& token = "") const;

  // Calls POST https://gateway.kugou.com/v2/personal_recommend
  // (x-router: persnfm.service.kugou.com).
  // Returns the continuous private-FM / "guess you like" feed.
  nlohmann::json GetPersonalFm(const std::string& userId = "",
                               const std::string& token = "",
                               const std::string& hash = "",
                               const std::string& songId = "",
                               int playtime = 0,
                               int remainSongCount = 0,
                               bool isOverplay = false,
                               const DeviceInfo& device = {},
                               const std::string& action = "play",
                               int songPoolId = 0) const;

  // Calls GET https://expendablekmr.kugou.com/v2/author_image/audio
  nlohmann::json GetImagesAudio(const std::string& hash,
                                const std::string& audioId = "",
                                const std::string& albumAudioId = "",
                                const std::string& filename = "",
                                int count = 5) const;

 private:
  HomeHttpPost httpPost_;
  HomeHttpGet httpGet_;
};

}  // namespace echo::core
