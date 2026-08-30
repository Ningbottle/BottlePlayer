#include "echo/core/CompatApiUtils.h"
#include "echo/core/CompatRequestContext.h"
#include "echo/core/PlayHistoryService.h"
#include "echo/core/SafeStoll.h"
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
    auto detail = handler("", "");
    return JsonResponse(std::move(detail));
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
      return JsonResponse(std::move(detail));
    }

    // Fallback
    return JsonResponse({
        {"status", 1},
        {"data",
         {
             {"userid", userId},
             {"nickname", session->nickname.empty() ? "听歌用户" : session->nickname},
             {"pic", session->pic},
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
  if (!session || userId.empty()) {
    return JsonResponse({
        {"status", 0},
        {"error_code", "native_vip_no_session"},
        {"error", "not logged in"},
        {"authoritative", false},
        {"data", nullptr},
    });
  }

  nlohmann::json vip;
  if (handler) {
    vip = handler(userId, token);
  } else {
    UserService userSvc;
    vip = userSvc.GetUserVip(ctx.Device(), userId, token);
  }

  auto normalized = NormalizeUserVipDetailResponse(std::move(vip));
  if (normalized.value("authoritative", false) && normalized.contains("data") &&
      normalized["data"].is_object()) {
    const auto& data = normalized["data"];
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
  }
  return JsonResponse(std::move(normalized));
}

namespace {

nlohmann::json EmptyUserPlaylistData() {
  return {
      {"list", nlohmann::json::array()},
      {"lists", nlohmann::json::array()},
      {"info", nlohmann::json::array()},
      {"total", 0},
  };
}

}  // namespace

CompatResponse HandleUserPlaylist(
    storage::Database& database,
    const QueryMap& query,
    const std::function<nlohmann::json(
        const DeviceInfo&, std::string, std::string, int, int)>& handler,
    const std::function<std::string(const DeviceInfo&, std::string, std::string, std::string*)>& registerHandler) {
  CompatRequestContext ctx(database);
  const auto& session = ctx.Session();
  const std::string userId = ctx.UserIdOr("");
  const std::string token = ctx.TokenOrEmpty();
  const int page = QueryInt(query, "page", 1);
  const int pageSize = QueryInt(query, "pagesize", 30);
  auto device = ctx.Device();

  const auto fetchPlaylists = [&](const DeviceInfo& currentDevice) {
    if (handler) return handler(currentDevice, userId, token, page, pageSize);
    PlaylistService playlist;
    return playlist.GetUserPlaylists(currentDevice, userId, token, page, pageSize);
  };
  const auto registerDevice = [&](const DeviceInfo& currentDevice, std::string* error) {
    if (registerHandler) return registerHandler(currentDevice, userId, token, error);
    DeviceRegisterService registerSvc;
    return registerSvc.Register(currentDevice, userId, token, error);
  };
  const auto persistDevice = [&](DeviceInfo& currentDevice, const std::string& newDfid) {
    if (newDfid.empty()) return false;
    currentDevice.dfid = newDfid;
    currentDevice.registered = true;
    ctx.SaveDevice(currentDevice);
    return true;
  };

  if (!device.registered && session && !userId.empty() && !token.empty()) {
    ECHO_LOG("UserPlaylist", std::string("registration_attempt=initial playlist_attempt=0 ") +
                                 DescribeDeviceIdentity(device));
    std::string regError;
    const auto newDfid = registerDevice(device, &regError);
    const bool dfidChanged = !newDfid.empty() && newDfid != device.dfid;
    if (!persistDevice(device, newDfid)) {
      ECHO_LOG("UserPlaylist", std::string("initial registration failed: ") + regError);
    } else {
      ECHO_LOG("UserPlaylist", std::string("registration_result=success dfid_changed=") +
                                   (dfidChanged ? "Y " : "N ") + DescribeDeviceIdentity(device));
    }
  }

  ECHO_LOG("UserPlaylist", std::string("playlist_attempt=1 ") + DescribeDeviceIdentity(device));
  auto result = fetchPlaylists(device);
  if (IsKuGouErrorCode(result, 20017) && session && !userId.empty() && !token.empty()) {
    ECHO_LOG("UserPlaylist", std::string("registration_attempt=refresh playlist_attempt=1 ") +
                                 DescribeDeviceIdentity(device));
    std::string regError;
    DeviceInfo retryDevice = device;
    retryDevice.registered = false;
    const auto newDfid = registerDevice(retryDevice, &regError);
    const bool dfidChanged = !newDfid.empty() && newDfid != retryDevice.dfid;
    if (!persistDevice(retryDevice, newDfid)) {
      ECHO_LOG("UserPlaylist", std::string("refresh registration failed: ") + regError);
      return JsonResponse({
          {"status", 0},
          {"error_code", "device_registration_failed"},
          {"upstream_error_code", 20017},
          {"error", regError.empty() ? "device registration failed" : regError},
          {"error_msg", regError.empty() ? "device registration failed" : regError},
          {"data", EmptyUserPlaylistData()},
      });
    }
    device = retryDevice;
    ECHO_LOG("UserPlaylist", std::string("registration_result=success dfid_changed=") +
                                 (dfidChanged ? "Y " : "N ") + DescribeDeviceIdentity(device));
    ECHO_LOG("UserPlaylist", std::string("playlist_attempt=2 ") + DescribeDeviceIdentity(device));
    result = fetchPlaylists(device);
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
  long long mxidVal = SafeStollStrict(mxidStr);
  if (mxidVal < 0 && !mxidStr.empty()) {
    return JsonResponse({{"status", 0}, {"error", "invalid mxid"}}, 400);
  }
  long long timeVal = SafeStoll(timeStr);  // 0 = use current time (safe default)
  return JsonResponse(playSvc.UploadSong(userId, token, mxidVal, timeVal, pc));
}

}  // namespace echo::core
