#include "echo/core/CompatApiUtils.h"
#include "echo/core/CompatRequestContext.h"
#include "echo/core/UserService.h"

namespace echo::core {

CompatResponse HandleYouthDayVip(storage::Database& database) {
  CompatRequestContext ctx(database);
  if (!ctx.HasLogin()) {
    return JsonResponse({{"status", 0}, {"error", "not logged in"}, {"data", nullptr}});
  }
  const auto& session = ctx.Session();
  UserService userSvc;
  return JsonResponse(userSvc.ClaimVip(ctx.Device(), session->userId, session->token));
}

CompatResponse HandleYouthDayVipUpgrade(storage::Database& database) {
  CompatRequestContext ctx(database);
  if (!ctx.HasLogin()) {
    return JsonResponse({{"status", 0}, {"error", "not logged in"}, {"data", nullptr}});
  }
  const auto& session = ctx.Session();
  UserService userSvc;
  return JsonResponse(userSvc.UpgradeVipReward(ctx.Device(), session->userId, session->token));
}

CompatResponse HandleYouthListenSong(storage::Database& database) {
  CompatRequestContext ctx(database);
  if (!ctx.HasLogin()) {
    return JsonResponse({{"status", 0}, {"error", "not logged in"}, {"data", nullptr}});
  }
  const auto& session = ctx.Session();
  UserService userSvc;
  return JsonResponse(userSvc.ClaimYouthListenSong(ctx.Device(), session->userId, session->token));
}

CompatResponse HandleYouthVipAd(storage::Database& database) {
  CompatRequestContext ctx(database);
  if (!ctx.HasLogin()) {
    return JsonResponse({{"status", 0}, {"error", "not logged in"}, {"data", nullptr}});
  }
  const auto& session = ctx.Session();
  UserService userSvc;
  return JsonResponse(userSvc.ClaimYouthAdVip(ctx.Device(), session->userId, session->token));
}

}  // namespace echo::core
