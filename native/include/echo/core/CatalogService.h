#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"

namespace echo::core {

using CatalogHttpGet = std::function<HttpResult(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers)>;

class CatalogService {
 public:
  CatalogService();
  explicit CatalogService(CatalogHttpGet httpGet);

  nlohmann::json GetAlbumDetail(std::string id) const;
  nlohmann::json GetAlbumSongs(std::string id, int page, int pageSize) const;
  nlohmann::json GetArtistDetail(std::string id) const;
  nlohmann::json GetArtistSongs(std::string id, int page, int pageSize, std::string sort) const;
  nlohmann::json GetArtistAlbums(std::string id, int page, int pageSize, std::string sort) const;

 private:
  CatalogHttpGet httpGet_;
};

}  // namespace echo::core
