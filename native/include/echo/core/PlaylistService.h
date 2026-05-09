#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"

namespace echo::core {

using PlaylistHttpGet = std::function<HttpResult(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers)>;

class PlaylistService {
 public:
  PlaylistService();
  explicit PlaylistService(PlaylistHttpGet httpGet);

  nlohmann::json GetTracks(std::string id, int page, int pageSize) const;
  nlohmann::json GetTags() const;
  nlohmann::json GetTopPlaylists(int categoryId, int page, int pageSize, int sort) const;

 private:
  PlaylistHttpGet httpGet_;
};

}  // namespace echo::core
