#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

#include "echo/core/Authorization.h"
#include "echo/storage/Database.h"

#include "echo/core/Dto.h"

namespace echo::core {

struct CompatResponse {
  int httpStatus = 200;
  std::string contentType = "application/json; charset=utf-8";
  nlohmann::json body;
};

struct CompatApiHandlers {
  std::function<nlohmann::json(std::string keywords, std::string type, int page, int pageSize)> search;
  std::function<nlohmann::json(std::string hash, std::string quality, std::string ppageId)> songUrl;
  std::function<nlohmann::json(std::string hash)> lyricSearch;
  std::function<nlohmann::json(std::string id, std::string accessKey)> lyricDetail;
  std::function<nlohmann::json(std::string id, int page, int pageSize)> playlistTracks;
  std::function<nlohmann::json(const DeviceInfo& device)> loginQrKey;
  std::function<nlohmann::json(const DeviceInfo& device, std::string key)> loginQrCheck;
  std::function<nlohmann::json(std::string id, std::string userId, std::string token)> playlistDetail;
  std::function<nlohmann::json(std::string userId, std::string token, int page, int pageSize)> userPlaylist;
  // New: user profile, VIP, and home discovery handlers.
  std::function<nlohmann::json(std::string userId, std::string token)> userDetail;
  std::function<nlohmann::json(std::string userId, std::string token)> userVip;
  std::function<nlohmann::json(std::string userId, std::string token)> everydayRecommend;
};

class CompatApi {
 public:
  explicit CompatApi(storage::Database& database);
  CompatApi(storage::Database& database, CompatApiHandlers handlers);

  CompatResponse Handle(
      const std::string& method,
      const std::string& path,
      const QueryMap& query,
      const HeaderMap& headers);

 private:
  storage::Database& database_;
  CompatApiHandlers handlers_;

  CompatResponse HandleKnownRoute(
      const std::string& method,
      const std::string& path,
      const QueryMap& query,
      const HeaderMap& headers);
};

bool IsKnownCompatRoute(const std::string& path);
nlohmann::json NativeNotImplementedPayload(const std::string& path);

}  // namespace echo::core
