#include "echo/core/CompatApiUtils.h"
#include "echo/core/CompatRequestContext.h"
#include "echo/core/DeviceRegisterService.h"
#include "echo/core/JsonHelpers.h"

namespace echo::core {

CompatResponse HandleRegisterDev(storage::Database& database, const QueryMap& query) {
  CompatRequestContext ctx(database);
  auto device = ctx.Device();

  const bool force = QueryValue(query, "force") == "1";

  if (force || !device.registered) {
    if (ctx.HasLogin()) {
      const auto& session = ctx.Session();
      DeviceRegisterService registerSvc;
      std::string regError;
      const auto newDfid = registerSvc.Register(device, session->userId, session->token, &regError);
      if (!newDfid.empty()) {
        device.dfid = newDfid;
        device.registered = true;
        ctx.SaveDevice(device);
      } else {
        ECHO_LOG("CompatApi", std::string("/register/dev upgrade failed: ") + regError);
        return JsonResponse({
            {"status", 1},
            {"data", ToJson(device)},
            {"register_error", regError},
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
