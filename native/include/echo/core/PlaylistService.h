#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"
#include "echo/core/Dto.h"

namespace echo::core {

using PlaylistHttpGet = std::function<HttpResult(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers)>;

using PlaylistHttpPost = std::function<HttpResult(
    const std::string& url,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& headers)>;

class PlaylistService {
 public:
   PlaylistService();
   explicit PlaylistService(PlaylistHttpGet httpGet);
   PlaylistService(PlaylistHttpGet httpGet, PlaylistHttpPost httpPost);

   nlohmann::json GetTracks(std::string id, int page, int pageSize) const;
   nlohmann::json GetTracks(
       const DeviceInfo& device,
       std::string id,
       int page,
       int pageSize) const;

   nlohmann::json GetTags() const;
   nlohmann::json GetTopPlaylists(int categoryId, int page, int pageSize, int sort) const;

   nlohmann::json GetPlaylistDetail(
       const std::string& id,
       const std::string& userId,
       const std::string& token) const;
   nlohmann::json GetPlaylistDetail(
       const DeviceInfo& device,
       const std::string& id,
       const std::string& userId,
       const std::string& token) const;

   nlohmann::json GetUserPlaylists(
       const std::string& userId,
       const std::string& token,
       int page,
       int pageSize) const;
   nlohmann::json GetUserPlaylists(
       const DeviceInfo& device,
       const std::string& userId,
       const std::string& token,
       int page,
       int pageSize) const;

   nlohmann::json AddPlaylist(
       const std::string& userId,
       const std::string& token,
       const std::string& name,
       int type = 0,
       int source = 1,
       const std::string& createUserId = "",
       const std::string& createListId = "",
       const std::string& createGid = "") const;
   nlohmann::json AddPlaylist(
       const DeviceInfo& device,
       const std::string& userId,
       const std::string& token,
       const std::string& name,
       int type = 0,
       int source = 1,
       const std::string& createUserId = "",
       const std::string& createListId = "",
       const std::string& createGid = "") const;

   nlohmann::json DeletePlaylist(
       const std::string& userId,
       const std::string& token,
       long long listId) const;
   nlohmann::json DeletePlaylist(
       const DeviceInfo& device,
       const std::string& userId,
       const std::string& token,
       long long listId) const;

   nlohmann::json AddPlaylistTracks(
       const std::string& userId,
       const std::string& token,
       const std::string& listId,
       const std::string& commaSeparatedTracks) const;
   nlohmann::json AddPlaylistTracks(
       const DeviceInfo& device,
       const std::string& userId,
       const std::string& token,
       const std::string& listId,
       const std::string& commaSeparatedTracks) const;

   nlohmann::json DeletePlaylistTracks(
       const std::string& userId,
       const std::string& token,
       const std::string& listId,
       const std::string& commaSeparatedFileIds) const;
   nlohmann::json DeletePlaylistTracks(
       const DeviceInfo& device,
       const std::string& userId,
       const std::string& token,
       const std::string& listId,
       const std::string& commaSeparatedFileIds) const;

 private:
  PlaylistHttpGet httpGet_;
  PlaylistHttpPost httpPost_;
};

}  // namespace echo::core

