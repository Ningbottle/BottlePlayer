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
      ECHO_LOG("DeviceRegister", std::string("registration_attempt=") +
                                     (force ? "force " : "initial ") +
                                     DescribeDeviceIdentity(device));
      const auto& session = ctx.Session();
      DeviceRegisterService registerSvc;
      std::string regError;
      const auto newDfid = registerSvc.Register(device, session->userId, session->token, &regError);
      if (!newDfid.empty()) {
        const bool dfidChanged = newDfid != device.dfid;
        device.dfid = newDfid;
        device.registered = true;
        ctx.SaveDevice(device);
        ECHO_LOG("DeviceRegister", std::string("registration_result=success dfid_changed=") +
                                       (dfidChanged ? "Y " : "N ") +
                                       DescribeDeviceIdentity(device));
      } else {
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
