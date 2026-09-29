#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"
#include "echo/core/Dto.h"
#include "echo/core/KuGouProfile.h"

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

   // `edition` swaps the WHOLE signature profile (appid+clientver+salt move
   // together). Default is Standard (1005/20489/standard salt), the current
   // production contract; Concept is for the signature-family A/B probe only.
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
       int pageSize,
       KuGouEdition edition = KuGouEdition::Standard) const;

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
   // B08 structured track entries: a JSON array of objects
   // [{name, hash, album_id, mixsongid}, ...]. Names may contain ANY
   // characters (commas, pipes, percent signs, CJK) — no escaping protocol.
   // Records with an empty hash are rejected (song identity); a missing
   // name degrades to "". Returns {status:0, error} on malformed input.
   nlohmann::json AddPlaylistTracksStructured(
       const DeviceInfo& device,
       const std::string& userId,
       const std::string& token,
       const std::string& listId,
       const nlohmann::json& tracks) const;

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
  // Shared tail of both add-tracks paths: listId normalization, payload
  // assembly, and the upstream POST.
  nlohmann::json SendAddTracksPayload(
      const DeviceInfo& device,
      const std::string& userId,
      const std::string& token,
      const std::string& listId,
      const nlohmann::json& resource) const;
};

}  // namespace echo::core

