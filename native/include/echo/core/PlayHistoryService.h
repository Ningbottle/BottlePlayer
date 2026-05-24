#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"

namespace echo::core {

using PlayHistoryHttpPost = std::function<HttpResult(
    const std::string& url,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& headers)>;

class PlayHistoryService {
 public:
  PlayHistoryService();
  explicit PlayHistoryService(PlayHistoryHttpPost httpPost);

  // Calls POST https://gateway.kugou.com/playhistory/v1/upload_songs
  nlohmann::json UploadSong(
      const std::string& userId,
      const std::string& token,
      long long mxid,
      long long time = 0,
      int pc = 1) const;

  // Calls POST https://gateway.kugou.com/playhistory/v1/get_songs
  nlohmann::json GetUserHistory(
      const std::string& userId,
      const std::string& token,
      const std::string& bp = "") const;

 private:
  PlayHistoryHttpPost httpPost_;
};

}  // namespace echo::core
