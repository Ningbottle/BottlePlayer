#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"

namespace echo::core {

using SongHttpGet = std::function<HttpResult(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers)>;

class SongService {
 public:
  SongService();
  explicit SongService(SongHttpGet httpGet);

  // Calls GET https://expendablekmrcdn.kugou.com/v1/audio_climax/audio
  // params: data = [{"hash": ...}]
  nlohmann::json GetClimax(const std::string& hash) const;

  // Calls GET https://gateway.kugou.com/grow/v1/song_ranking/play_page/ranking_info
  // params: album_audio_id
  nlohmann::json GetRanking(const std::string& albumAudioId) const;

  // Calls GET https://gateway.kugou.com/grow/v1/song_ranking/unlock/v2/ranking_filter
  // params: album_audio_id, page, pagesize
  nlohmann::json GetRankingFilter(const std::string& albumAudioId, int page = 1, int pageSize = 30) const;

 private:
  SongHttpGet httpGet_;
};

}  // namespace echo::core
