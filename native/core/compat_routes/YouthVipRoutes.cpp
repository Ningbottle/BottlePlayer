#include "echo/core/CompatApiUtils.h"
#include "echo/core/CompatRequestContext.h"
#include "echo/core/KuGouProfile.h"
#include "echo/core/UserService.h"

namespace echo::core {

CompatResponse HandleYouthDayVip(
    storage::Database& database,
    const QueryMap& query,
    const LoginHttpGet& sessionHttpGet,
    const LoginHttpPost& sessionHttpPost) {
  CompatRequestContext ctx(database);
  if (!ctx.HasLogin()) {
    return JsonResponse({{"status", 0}, {"error", "not logged in"}, {"data", nullptr}});
  }

  // The day claim always signs with the family that minted the session. There
  // used to be a compile-time gate here that kept the production path on
  // Standard and only allowed Concept in Debug builds — that gate is what
  // made every release claim return 51002. See ADR-0005 追记.
#ifdef _DEBUG
  // Diagnostics only: a Debug build can still be asked to sign with the
  // standard family so the two families can be A/B'd against one another on
  // the same session. It is labelled in the response so no reader can mistake
  // a diagnostic rejection for the production outcome. A Release build does
  // not read the parameter at all, so a shipped binary cannot be steered onto
  // the family that returns 51002.
  const bool wantsStandardDiagnostic = QueryValue(query, "profile") == "standard";
  const KuGouEdition edition =
      wantsStandardDiagnostic ? KuGouEdition::Standard : kProjectEdition;
#else
  const KuGouEdition edition = kProjectEdition;
#endif

  const bool sessionInjected =
      static_cast<bool>(sessionHttpGet) || static_cast<bool>(sessionHttpPost);
  if (sessionInjected && !sessionHttpPost) {
    return JsonResponse({
        {"status", 0},
        {"error", "day_claim_transport_incomplete"},
        {"error_msg", "day claim route requires POST transport injection"},
        {"upstream_called", false},
        {"data", nullptr},
    });
  }

  UserService userSvc = sessionInjected
      ? UserService(sessionHttpGet, sessionHttpPost)
      : UserService();
  const auto& session = ctx.Session();
  auto result = userSvc.ClaimVip(ctx.Device(), session->userId, session->token, edition);
#ifdef _DEBUG
  if (wantsStandardDiagnostic && result.is_object()) {
    result["diagnostic_profile"] = "standard";
  }
#endif
  return JsonResponse(result);
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
