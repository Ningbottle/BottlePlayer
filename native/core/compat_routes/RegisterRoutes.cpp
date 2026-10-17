#include "echo/core/CompatApiUtils.h"
#include "echo/core/CompatRequestContext.h"
#include "echo/core/DeviceRegisterService.h"
#include "echo/core/JsonHelpers.h"

namespace echo::core {

CompatResponse HandleRegisterDev(storage::Database& database, const QueryMap& query,
    const execution::OperationContext& operation,
    const std::function<std::string(const DeviceInfo&, std::string, std::string, std::string*)>& registerHandler) {
  execution::OperationScope operationScope(operation);
  CompatRequestContext ctx(database, operation);
  auto device = ctx.Device();

  const bool force = QueryValue(query, "force") == "1";

  if (force || !device.registered) {
    if (ctx.HasLogin()) {
      ECHO_LOG("DeviceRegister", std::string("registration_attempt=") +
                                     (force ? "force " : "initial ") +
                                     DescribeDeviceIdentity(device));
      const auto& session = ctx.Session();
      DeviceRegisterService registerSvc;
      std::string regError;
      const auto committed = ctx.RegisterDevice([&] {
        if (registerHandler) return registerHandler(device, session->userId, session->token, &regError);
        return registerSvc.Register(device, session->userId, session->token, &regError);
      });
      if (committed == model::CommitResult::Applied) {
        const auto oldDfid = device.dfid;
        device = ctx.Device();
        const bool dfidChanged = oldDfid != device.dfid;
        ECHO_LOG("DeviceRegister", std::string("registration_result=success dfid_changed=") +
                                       (dfidChanged ? "Y " : "N ") +
                                       DescribeDeviceIdentity(device));
      } else {
        if (committed != model::CommitResult::UpstreamFailure)
          return JsonResponse({{"status", 0}, {"error", "stale_identity"}}, 409);
        ECHO_LOG("CompatApi", std::string("/register/dev upgrade failed: ") + regError);
        const auto message = regError.empty() ? "device registration failed" : regError;
        return JsonResponse({
            {"status", 0},
            {"data", ToJson(device)},
            {"error", message},
            {"error_msg", message},
            {"error_code", "device_registration_failed"},
        });
      }
    }
  }
  return JsonResponse({
      {"status", 1},
      {"data", ToJson(device)},
  });
}

}  // namespace echo::core
