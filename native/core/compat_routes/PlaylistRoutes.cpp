#include "echo/core/CompatApiUtils.h"
#include "echo/core/CompatRequestContext.h"
#include "echo/core/DeviceService.h"
#include "echo/core/PlaylistService.h"
#include "echo/core/SafeStoll.h"
#include "echo/storage/DeviceRepository.h"
#include "echo/storage/SessionRepository.h"

namespace echo::core {

CompatResponse HandlePlaylistAdd(storage::Database& database, const QueryMap& query) {
  CompatRequestContext ctx(database);
  const std::string userId = ctx.UserIdOr("");
  const std::string token = ctx.TokenOrEmpty();
  const std::string name = QueryValue(query, "name");
  const int type = QueryInt(query, "type", 0);
  const int source = QueryInt(query, "source", 1);
  const std::string createUserId = QueryValue(query, "list_create_userid");
  const std::string createListId = QueryValue(query, "list_create_listid");
  const std::string createGid = QueryValue(query, "list_create_gid");

  const auto& device = ctx.Device();

  PlaylistService playlist;
  return JsonResponse(playlist.AddPlaylist(
      device, userId, token, name, type, source, createUserId, createListId, createGid));
}

CompatResponse HandlePlaylistDel(storage::Database& database, const QueryMap& query) {
  CompatRequestContext ctx(database);
  const std::string userId = ctx.UserIdOr("");
  const std::string token = ctx.TokenOrEmpty();
  const std::string listIdStr = QueryValue(query, "listid", QueryValue(query, "id"));
  const long long listId = SafeStollStrict(listIdStr);
  if (listId < 0 && !listIdStr.empty()) {
    return JsonResponse({{"status", 0}, {"error", "invalid listid"}}, 400);
  }

  const auto& device = ctx.Device();

  PlaylistService playlist;
  return JsonResponse(playlist.DeletePlaylist(device, userId, token, listId));
}

CompatResponse HandlePlaylistTracksAdd(storage::Database& database, const QueryMap& query, const std::string& body) {
  CompatRequestContext ctx(database);
  const std::string userId = ctx.UserIdOr("");
  const std::string token = ctx.TokenOrEmpty();
  const auto& device = ctx.Device();

  nlohmann::json jsonBody;
  try {
    if (!body.empty()) {
      jsonBody = nlohmann::json::parse(body);
    }
  } catch (...) {}

  auto ReadString = [](const nlohmann::json& j, const std::string& k, const std::string& def = "") -> std::string {
    if (j.contains(k)) {
      if (j[k].is_string()) return j[k].get<std::string>();
      if (j[k].is_number()) return std::to_string(j[k].get<long long>());
    }
    return def;
  };

  const std::string listIdFromQuery = QueryValue(query, "listid", QueryValue(query, "id"));
  const std::string listId = listIdFromQuery.empty()
      ? ReadString(jsonBody, "listId", ReadString(jsonBody, "id", ReadString(jsonBody, "listid")))
      : listIdFromQuery;

  const std::string dataFromQuery = QueryValue(query, "data");
  const std::string data = dataFromQuery.empty() ? ReadString(jsonBody, "data") : dataFromQuery;

  PlaylistService playlist;
  // B08: `data` may be a structured JSON array of track objects
  // [{name, hash, album_id, mixsongid}, ...] — names then carry commas,
  // pipes, and percent signs verbatim. Legacy callers send the
  // comma-separated `name|hash|album_id|mixsongid` string, still accepted
  // (with %7C restored inside names). The structured form is preferred:
  // the legacy string cannot express a name containing a comma.
  if (!jsonBody.is_null() && jsonBody.contains("data") && jsonBody["data"].is_array()) {
    return JsonResponse(
        playlist.AddPlaylistTracksStructured(device, userId, token, listId, jsonBody["data"]));
  }
  {
    nlohmann::json parsedData = nlohmann::json::parse(data, nullptr, false);
    if (!parsedData.is_discarded() && parsedData.is_array()) {
      return JsonResponse(
          playlist.AddPlaylistTracksStructured(device, userId, token, listId, parsedData));
    }
  }
  return JsonResponse(playlist.AddPlaylistTracks(device, userId, token, listId, data));
}

CompatResponse HandlePlaylistTracksDel(storage::Database& database, const QueryMap& query) {
  CompatRequestContext ctx(database);
  const std::string userId = ctx.UserIdOr("");
  const std::string token = ctx.TokenOrEmpty();
  const std::string listId = QueryValue(query, "listid", QueryValue(query, "id"));
  const std::string fileids = QueryValue(query, "fileids", QueryValue(query, "ids", QueryValue(query, "data")));

  const auto& device = ctx.Device();

  PlaylistService playlist;
  return JsonResponse(playlist.DeletePlaylistTracks(device, userId, token, listId, fileids));
}

CompatResponse HandlePlaylistDetail(
    storage::Database& database,
    const QueryMap& query,
    const std::function<nlohmann::json(std::string, std::string, std::string)>& handler) {
  const auto id = QueryValue(query, "id", QueryValue(query, "ids"));
  storage::SessionRepository sessionRepo(database);
  const auto session = sessionRepo.Load();
  const std::string userId = session ? session->userId : "0";
  const std::string token = session ? session->token : "";
  if (handler) {
    return JsonResponse(handler(id, userId, token));
  }
  storage::DeviceRepository deviceRepo(database);
  DeviceService devices(deviceRepo);
  const auto device = devices.EnsureDeviceReady();

  PlaylistService playlist;
  return JsonResponse(playlist.GetPlaylistDetail(device, id, userId, token));
}

CompatResponse HandlePlaylistTrackAll(
    storage::Database& database,
    const QueryMap& query,
    const std::function<nlohmann::json(std::string, int, int)>& handler) {
  const auto id = QueryValue(query, "id", QueryValue(query, "listid"));
  const auto page = QueryInt(query, "page", 1);
  const auto pageSize = QueryInt(query, "pagesize", QueryInt(query, "pageSize", 30));
  if (handler) {
    return JsonResponse(handler(id, page, pageSize));
  }
  storage::DeviceRepository deviceRepo(database);
  DeviceService devices(deviceRepo);
  const auto device = devices.EnsureDeviceReady();

  PlaylistService playlist;
  return JsonResponse(playlist.GetTracks(device, id, page, pageSize));
}

CompatResponse HandlePlaylistTrackAllNew(
    storage::Database& database,
    const QueryMap& query,
    const std::function<nlohmann::json(std::string, int, int)>& handler) {
  const auto id = QueryValue(query, "listid", QueryValue(query, "id"));
  const auto page = QueryInt(query, "page", 1);
  const auto pageSize = QueryInt(query, "pagesize", QueryInt(query, "pageSize", 30));
  if (handler) {
    return JsonResponse(handler(id, page, pageSize));
  }
  storage::DeviceRepository deviceRepo(database);
  DeviceService devices(deviceRepo);
  const auto device = devices.EnsureDeviceReady();

  PlaylistService playlist;
  return JsonResponse(playlist.GetTracks(device, id, page, pageSize));
}

CompatResponse HandlePlaylistTags(storage::Database& database) {
  PlaylistService playlist;
  return JsonResponse(playlist.GetTags());
}

CompatResponse HandleTopPlaylist(storage::Database& database, const QueryMap& query) {
  PlaylistService playlist;
  return JsonResponse(playlist.GetTopPlaylists(
      QueryInt(query, "category_id", QueryInt(query, "categoryid", 0)),
      QueryInt(query, "page", 1),
      QueryInt(query, "pagesize", 30),
      QueryInt(query, "sort", 2)));
}

}  // namespace echo::core
