#include "echo/core/CompatApiUtils.h"
#include "echo/core/CompatRequestContext.h"
#include "echo/core/UserService.h"

namespace echo::core {

CompatResponse HandleYouthDayVip() {
  return JsonResponse({
      {"status", 0},
      {"error_code", "kugou_vip_legacy_disabled"},
      {"error", "该端点需要广告 SDK 凭证，纯 HTTP 不可达；请使用 /youth/listen/song 或 /youth/vip/ad"},
      {"data", nullptr},
  });
}

CompatResponse HandleYouthListenSong(storage::Database& database) {
  CompatRequestContext ctx(database);
  if (!ctx.HasLogin()) {
    return JsonResponse({{"status", 0}, {"error", "not logged in"}, {"data", nullptr}});
  }
  const auto& session = ctx.Session();
  UserService userSvc;
  return JsonResponse(userSvc.ClaimYouthListenSong(session->userId, session->token));
}

CompatResponse HandleYouthVipAd(storage::Database& database) {
  CompatRequestContext ctx(database);
  if (!ctx.HasLogin()) {
    return JsonResponse({{"status", 0}, {"error", "not logged in"}, {"data", nullptr}});
  }
  const auto& session = ctx.Session();
  UserService userSvc;
  return JsonResponse(userSvc.ClaimYouthAdVip(session->userId, session->token));
}

}  // namespace echo::core
