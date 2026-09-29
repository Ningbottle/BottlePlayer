#include "echo/core/CompatApiUtils.h"
#include "echo/core/CompatRequestContext.h"
#include "echo/core/KuGouProfile.h"
#include "echo/core/UserService.h"

namespace echo::core {

CompatResponse HandleYouthDayVip(
    storage::Database& database,
    const QueryMap& query,
    const LoginHttpGet& sessionHttpGet,
    const LoginHttpPost& sessionHttpPost,
    bool conceptCandidateEnabled) {
  CompatRequestContext ctx(database);
  if (!ctx.HasLogin()) {
    return JsonResponse({{"status", 0}, {"error", "not logged in"}, {"data", nullptr}});
  }

  const auto profileParam = QueryValue(query, "profile");
  const bool wantsConceptCandidate = profileParam == "concept";

  // Release (or test-forced release decision): explicit candidate is not
  // available. Reject BEFORE any UserService/upstream construction so a
  // Release binary can never silently fall through to a Standard claim and
  // poison a controlled experiment.
  if (wantsConceptCandidate && !conceptCandidateEnabled) {
    return JsonResponse({
        {"status", 0},
        {"error", "candidate_profile_unavailable"},
        {"error_msg", "day Concept 候选仅 Debug 构建可用；未发起上游领取"},
        {"upstream_called", false},
        {"profile_requested", "concept"},
        {"data", nullptr},
    });
  }

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
  // Production default Standard. Concept only when explicitly requested AND
  // the build allows the candidate. Never auto-selected.
  const KuGouEdition edition = wantsConceptCandidate
      ? KuGouEdition::Concept
      : KuGouEdition::Standard;
  auto result = userSvc.ClaimVip(ctx.Device(), session->userId, session->token, edition);
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
