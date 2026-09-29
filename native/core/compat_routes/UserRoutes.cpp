#include "echo/core/CompatApiUtils.h"
#include "echo/core/CompatRequestContext.h"
#include "echo/core/LoginService.h"
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

namespace {

class LazyVipRefreshFinishGuard {
 public:
  LazyVipRefreshFinishGuard(
      storage::SessionRepository& repository,
      const storage::LazyVipRefreshTicket& ticket) noexcept
      : repository_(repository), ticket_(ticket) {}

  ~LazyVipRefreshFinishGuard() noexcept {
    (void)repository_.FinishLazyVipRefresh(
        ticket_, storage::SessionRepository::CurrentUnixTimeMs());
  }

  LazyVipRefreshFinishGuard(const LazyVipRefreshFinishGuard&) = delete;
  LazyVipRefreshFinishGuard& operator=(const LazyVipRefreshFinishGuard&) = delete;

 private:
  storage::SessionRepository& repository_;
  const storage::LazyVipRefreshTicket& ticket_;
};

}  // namespace

CompatResponse HandleUserDetail(
    storage::Database& database,
    const std::function<nlohmann::json(std::string, std::string)>& handler,
    const LoginHttpGet& sessionHttpGet,
    const LoginHttpPost& sessionHttpPost) {
  if (handler) {
    auto detail = handler("", "");
    return JsonResponse(std::move(detail));
  }
  CompatRequestContext ctx(database);
  const auto& session = ctx.Session();
  std::string userId = ctx.UserIdOr("");
  std::string token = ctx.TokenOrEmpty();
  if (session && !userId.empty()) {
    const auto& device = ctx.Device();
    // Either transport seam being injected means a host/test owns the network:
    // a verb it did not supply must fail explicitly, never fall back to the
    // default real transport.
    const bool sessionInjected =
        static_cast<bool>(sessionHttpGet) || static_cast<bool>(sessionHttpPost);
    // 会话恢复（非扫码）路径：vip_token 不在库里时按 login_token.js 懒刷新，
    // 让 v6/priv_url 拿得到会员音质。
    if (session->vipToken.empty()) {
      storage::SessionRepository sessionRepository(database);
      const auto ticket = sessionRepository.TryClaimLazyVipRefresh(
          ctx.SessionGeneration(),
          storage::SessionRepository::CurrentUnixTimeMs(),
          storage::SessionRepository::CurrentProcessEpoch());
      if (ticket) {
        // The claim is based on the current encrypted row, not this context's
        // potentially older same-generation token snapshot. Use those current
        // credentials both for RefreshSession and this request's detail call.
        LazyVipRefreshFinishGuard finish(sessionRepository, *ticket);
        userId = ticket->session.userId;
        token = ticket->session.token;

        LoginService loginSvc = sessionInjected
            ? LoginService(sessionHttpGet, sessionHttpPost)
            : LoginService();
        const auto refreshed = loginSvc.RefreshSession(
            device, ticket->session.userId, ticket->session.token,
            ticket->session.t1);
        if (refreshed) {
          // B02: credential refresh is a late write after a network wait. It
          // must commit only while the account generation this request loaded
          // is still current — otherwise a logout / login B / re-login during
          // the network wait would be overwritten with this request's stale
          // credentials. This request may still use its own refreshed result.
          const std::string refreshedToken = refreshed->token;
          const std::string refreshedVipToken = refreshed->vipToken;
          const int refreshedVipType = refreshed->vipType;
          const std::string refreshedT1 = refreshed->t1;
          ctx.SaveSessionPatchIfCurrent([&](SessionInfo& s) {
            s.token = refreshedToken;
            s.vipToken = refreshedVipToken;
            s.vipType = refreshedVipType;
            if (!refreshedT1.empty()) s.t1 = refreshedT1;
          });
          token = refreshedToken;
          if (!refreshedVipToken.empty()) {
            ECHO_LOG("VipToken", "lazy-refreshed vip_token on /user/detail (len=" +
                std::to_string(refreshedVipToken.size()) + ")");
          }
        }
      }
    }
    // The lazy refresh above only runs with a POST verb (LoginService guards
    // its own empty callback). /user/detail itself is POST-only as well, so
    // when a seam is injected without a POST this route refuses explicitly
    // instead of constructing the default real-network UserService(). The
    // error_code is a marker only this branch can emit, so a test can prove no
    // transport was built, let alone called. Production injects neither
    // callback, so this is unreachable there and the default transport stands.
    if (sessionInjected && !sessionHttpPost) {
      return JsonResponse({{"status", 0},
                           {"error", "No HTTP POST handler available"},
                           {"error_code", "native_detail_no_post_transport"},
                           {"data", nullptr}});
    }
    UserService userSvc = sessionInjected
        ? UserService(sessionHttpGet, sessionHttpPost)
        : UserService();
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
        // B02: profile patch — only the profile fields, and only while the
        // account generation is unchanged. A late response must never
        // restore a snapshot that could resurrect old credentials or a
        // different account.
        ctx.SaveSessionPatchIfCurrent([&](SessionInfo& s) {
          if (!nickname.empty()) s.nickname = nickname;
          if (!pic.empty()) s.pic = pic;
        });
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
    const std::function<nlohmann::json(std::string, std::string)>& handler,
    const LoginHttpGet& sessionHttpGet,
    const LoginHttpPost& sessionHttpPost) {
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
    // Either transport seam being injected means a host/test owns the network:
    // a verb it did not supply must fail explicitly, never fall back to the
    // default real transport. GetUserVip is GET-only, so a GET-less injection
    // is a configuration error and must not reach the real network.
    const bool sessionInjected =
        static_cast<bool>(sessionHttpGet) || static_cast<bool>(sessionHttpPost);
    if (sessionInjected && !sessionHttpGet) {
      return JsonResponse({{"status", 0},
                           {"error", "No HTTP GET handler available"},
                           {"error_code", "native_vip_no_get_transport"},
                           {"data", nullptr}});
    }
    UserService userSvc = sessionInjected ? UserService(sessionHttpGet, sessionHttpPost)
                                          : UserService();
    // 2026-09-15 签名族对照实验证实：当前会话下 VIP 查询应使用 Concept 配置
    // （appid=3116 / clientver=11440 / Lite 盐）。Standard 配置导致可重复的 20017。
    // 详见 docs/signature-family-experiment-2026-09-15.md
    vip = userSvc.GetUserVip(ctx.Device(), userId, token, KuGouEdition::Concept);
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
      // B02: profile patch, conditional on the captured account generation.
      ctx.SaveSessionPatchIfCurrent([&](SessionInfo& s) {
        if (!nickname.empty()) s.nickname = nickname;
        if (!pic.empty()) s.pic = pic;
      });
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
    const std::function<std::string(const DeviceInfo&, std::string, std::string, std::string*)>& registerHandler,
    const LoginHttpGet& sessionHttpGet,
    const LoginHttpPost& sessionHttpPost) {
  CompatRequestContext ctx(database);
  const auto& session = ctx.Session();
  const std::string userId = ctx.UserIdOr("");
  const std::string token = ctx.TokenOrEmpty();
  const int page = QueryInt(query, "page", 1);
  const int pageSize = QueryInt(query, "pagesize", 30);
  auto device = ctx.Device();

  const auto fetchPlaylists = [&](const DeviceInfo& currentDevice) {
    if (handler) return handler(currentDevice, userId, token, page, pageSize);
    // Either transport seam being injected means a host/test owns the network:
    // a verb it did not supply must fail explicitly, never fall back to the
    // default real transport. GetUserPlaylists is POST-only, so a POST-less
    // injection is a configuration error and must not reach the real network.
    const bool sessionInjected =
        static_cast<bool>(sessionHttpGet) || static_cast<bool>(sessionHttpPost);
    if (sessionInjected && !sessionHttpPost) {
      return nlohmann::json{{"status", 0},
                            {"error", "No HTTP POST handler available"},
                            {"error_code", "native_playlist_no_post_transport"},
                            {"data", nullptr}};
    }
    PlaylistService playlist = sessionInjected ? PlaylistService(sessionHttpGet, sessionHttpPost)
                                               : PlaylistService();
    // 2026-09-15 签名族对照实验证实：当前会话下用户歌单查询应使用 Concept 配置
    // （appid=3116 / clientver=11440 / Lite 盐）。Standard 配置导致可重复的 20017。
    // 详见 docs/signature-family-experiment-2026-09-15.md
    return playlist.GetUserPlaylists(currentDevice, userId, token, page, pageSize, KuGouEdition::Concept);
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
  if (IsKuGouErrorCode(result, 20017)) {
    // Incident 2026-09-02: this refresh-retry used to re-register and persist
    // a synthetic dfid, destroying the user's trusted (web-captured) device
    // identity within seconds of the first upstream rejection. A registered
    // device is trusted: never auto-rotate it. Surface the upstream error;
    // the user refreshes dfid/mid/uuid from Settings when the fingerprint
    // expires upstream.
    ECHO_LOG("UserPlaylist", std::string("upstream 20017 with registered device; no auto-rotation ") +
                                 DescribeDeviceIdentity(device));
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
      // B02: profile patch from the playlist payload, conditional on the
      // captured account generation (same late-response race).
      ctx.SaveSessionPatchIfCurrent([&](SessionInfo& s) {
        if (!nick.empty()) s.nickname = nick;
        if (!pic.empty()) s.pic = pic;
      });
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
  // B07: the frontend's pagesize is now actually forwarded upstream (the
  // route used to read only bp, so the upstream chose its own page size and
  // pagesize=100 was silently ignored). Bound it defensively: 1..100.
  int pagesize = QueryInt(query, "pagesize", 100);
  if (pagesize < 1) pagesize = 100;
  if (pagesize > 100) pagesize = 100;
  PlayHistoryService playSvc;
  return JsonResponse(playSvc.GetUserHistory(userId, token, bp, pagesize));
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
  // B10: business-range validation at the boundary — the upstream must not
  // receive corrupt timestamps or play counts from any caller.
  if (timeVal < 0) {
    return JsonResponse({{"status", 0}, {"error", "invalid time"}}, 400);
  }
  if (pc < 1 || pc > 1000) {
    return JsonResponse({{"status", 0}, {"error", "invalid pc"}}, 400);
  }
  return JsonResponse(playSvc.UploadSong(userId, token, mxidVal, timeVal, pc));
}

}  // namespace echo::core
