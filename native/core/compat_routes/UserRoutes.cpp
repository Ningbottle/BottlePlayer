#include "echo/core/CompatApiUtils.h"
#include "echo/core/CompatRequestContext.h"
#include "echo/core/PlayHistoryService.h"
#include "echo/storage/SessionRepository.h"
#include "echo/core/PlaylistService.h"
#include "echo/core/UserCloudService.h"
#include "echo/core/UserService.h"
#include "echo/core/DeviceRegisterService.h"
#include "echo/diagnostics/Redaction.h"

#include <sstream>

namespace echo::core {

CompatResponse HandleUserDetail(
    storage::Database& database,
    const std::function<nlohmann::json(std::string, std::string)>& handler) {
  if (handler) {
    return JsonResponse(handler("", ""));
  }
  CompatRequestContext ctx(database);
  const auto& session = ctx.Session();
  const std::string userId = ctx.UserIdOr("");
  const std::string token = ctx.TokenOrEmpty();
  if (session && !userId.empty()) {
    const auto& device = ctx.Device();
    UserService userSvc;
    nlohmann::json detail = userSvc.GetUserDetail(device, userId, token);
    if (detail.value("status", 0) == 1 && detail.contains("data") && detail["data"].is_object()) {
      auto data = detail["data"];
      std::string nickname = data.value("nickname", "");
      std::string pic = data.value("pic", "");
      if (pic.empty()) {
        pic = data.value("avatar", "");
      }
      if ((!nickname.empty() && nickname != session->nickname) ||
          (!pic.empty() && pic != session->pic)) {
        SessionInfo updatedSession = *session;
        if (!nickname.empty()) updatedSession.nickname = nickname;
        if (!pic.empty()) updatedSession.pic = pic;
        ctx.SaveSession(updatedSession);
      }
      if (detail["data"].value("pic", "").empty() && !pic.empty()) {
        detail["data"]["pic"] = pic;
      }
      return JsonResponse(detail);
    }

    // Fallback
    return JsonResponse({
        {"status", 1},
        {"data",
         {
             {"userid", userId},
             {"nickname", session->nickname.empty() ? "听歌用户" : session->nickname},
             {"pic", session->pic},
             {"token", token},
         }},
    });
  }
  return JsonResponse({{"status", 0}, {"error", "not logged in"}, {"data", nullptr}});
}

CompatResponse HandleUserVipDetail(
    storage::Database& database,
    const std::function<nlohmann::json(std::string, std::string)>& handler) {
  CompatRequestContext ctx(database);
  const auto& session = ctx.Session();
  const std::string userId = ctx.UserIdOr("");
  const std::string token = ctx.TokenOrEmpty();
  if (handler) {
    return JsonResponse(handler(userId, token));
  }

  if (session && !userId.empty()) {
    const auto& device = ctx.Device();
    UserService userSvc;
    nlohmann::json vip = userSvc.GetUserVip(device, userId, token);

    if (vip.value("status", 0) == 1 && vip.contains("data") && vip["data"].is_object()) {
      const auto& data = vip["data"];
      const auto extractStr = [&](std::initializer_list<const char*> keys) {
        for (const char* k : keys) {
          if (data.contains(k) && data[k].is_string() && !data[k].get<std::string>().empty()) {
            return data[k].get<std::string>();
          }
        }
        return std::string{};
      };
      const auto nickname = extractStr({"nickname", "username", "name"});
      const auto pic = extractStr({"pic", "headphoto", "avatar", "headerurl", "userpic"});
      if ((!nickname.empty() && nickname != session->nickname) ||
          (!pic.empty() && pic != session->pic)) {
        SessionInfo updated = *session;
        if (!nickname.empty()) updated.nickname = nickname;
        if (!pic.empty()) updated.pic = pic;
        ctx.SaveSession(updated);
      }
      return JsonResponse(vip);
    }

    // Fallback
    return JsonResponse({
        {"status", 1},
        {"data",
         {
             {"vip_level", 0},
             {"vip_type", 0},
             {"is_vip", 0},
             {"end_time", ""},
             {"nickname", session->nickname},
             {"pic", session->pic},
         }},
    });
  }
  return JsonResponse({{"status", 0}, {"error", "not logged in"}, {"data", nullptr}});
}

CompatResponse HandleUserPlaylist(
    storage::Database& database,
    const QueryMap& query,
    const std::function<nlohmann::json(std::string, std::string, int, int)>& handler) {
  CompatRequestContext ctx(database);
  const auto& session = ctx.Session();
  const std::string userId = ctx.UserIdOr("");
  const std::string token = ctx.TokenOrEmpty();
  const int page = QueryInt(query, "page", 1);
  const int pageSize = QueryInt(query, "pagesize", 30);
  if (handler) {
    return JsonResponse(handler(userId, token, page, pageSize));
  }
  auto device = ctx.Device();

  PlaylistService playlist;
  if (!device.registered && session && !userId.empty() && !token.empty()) {
    DeviceRegisterService registerSvc;
    std::string regError;
    const auto newDfid = registerSvc.Register(device, userId, token, &regError);
    if (!newDfid.empty()) {
      device.dfid = newDfid;
      device.registered = true;
      ctx.SaveDevice(device);
    } else {
      ECHO_LOG("CompatApi", std::string("/user/playlist device registration failed: ") + regError);
    }
  }
  auto result = playlist.GetUserPlaylists(device, userId, token, page, pageSize);
  if (IsKuGouErrorCode(result, 20017) && session && !userId.empty() && !token.empty()) {
    DeviceRegisterService registerSvc;
    std::string regError;
    DeviceInfo retryDevice = device;
    retryDevice.registered = false;
    const auto newDfid = registerSvc.Register(retryDevice, userId, token, &regError);
    if (!newDfid.empty()) {
      retryDevice.dfid = newDfid;
      retryDevice.registered = true;
      ctx.SaveDevice(retryDevice);
      result = playlist.GetUserPlaylists(retryDevice, userId, token, page, pageSize);
    } else {
      ECHO_LOG("CompatApi", std::string("/user/playlist device refresh failed: ") + regError);
    }
  }

  if (session && result.value("status", 0) == 1 && result.contains("data") &&
      result["data"].is_object() && result["data"].contains("info") &&
      result["data"]["info"].is_array() && !result["data"]["info"].empty()) {
    const auto& first = result["data"]["info"][0];
    std::string nick;
    std::string pic;
    if (first.is_object()) {
      if (first.contains("list_create_username") && first["list_create_username"].is_string())
        nick = first["list_create_username"].get<std::string>();
      if (first.contains("create_user_pic") && first["create_user_pic"].is_string())
        pic = first["create_user_pic"].get<std::string>();
    }
    if ((!nick.empty() && nick != session->nickname) ||
        (!pic.empty() && pic != session->pic)) {
      SessionInfo updated = *session;
      if (!nick.empty()) updated.nickname = nick;
      if (!pic.empty()) updated.pic = pic;
      ctx.SaveSession(updated);
    }
  }
  return JsonResponse(result);
}

CompatResponse HandleUserHistory(
    storage::Database& database,
    const QueryMap& query) {
  storage::SessionRepository sessionRepo(database);
  const auto session = sessionRepo.Load();
  const std::string userId = session ? session->userId : "";
  const std::string token = session ? session->token : "";
  const std::string bp = QueryValue(query, "bp");
  PlayHistoryService playSvc;
  return JsonResponse(playSvc.GetUserHistory(userId, token, bp));
}

CompatResponse HandleUserCloud(
    storage::Database& database,
    const QueryMap& query) {
  storage::SessionRepository sessionRepo(database);
  const auto session = sessionRepo.Load();
  const std::string userId = session ? session->userId : "";
  const std::string token = session ? session->token : "";
  const int page = QueryInt(query, "page", 1);
  const int pageSize = QueryInt(query, "pagesize", 30);
  UserCloudService cloudSvc;
  return JsonResponse(cloudSvc.GetList(userId, token, page, pageSize));
}

CompatResponse HandlePlayHistoryUpload(
    storage::Database& database,
    const QueryMap& query) {
  storage::SessionRepository sessionRepo(database);
  const auto session = sessionRepo.Load();
  const std::string userId = session ? session->userId : "";
  const std::string token = session ? session->token : "";

  std::string mxidStr = QueryValue(query, "mxid");
  std::string timeStr = QueryValue(query, "time");
  int pc = QueryInt(query, "pc", 1);

  PlayHistoryService playSvc;
  long long mxidVal = mxidStr.empty() ? 0 : std::stoll(mxidStr);
  long long timeVal = timeStr.empty() ? 0 : std::stoll(timeStr);
  return JsonResponse(playSvc.UploadSong(userId, token, mxidVal, timeVal, pc));
}

}  // namespace echo::core
