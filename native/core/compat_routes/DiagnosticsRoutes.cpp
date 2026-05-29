#include "echo/core/CompatApiUtils.h"

namespace echo::core {

CompatResponse HandleHealth() {
  return JsonResponse({
      {"status", 1},
      {"data",
       {
           {"service", "EchoCompatServer"},
           {"state", "ok"},
           {"compat_port", 6609},
           {"native", true},
       }},
  });
}

CompatResponse HandleServerNow() {
  return JsonResponse({
      {"status", 1},
      {"data",
       {
           {"now", UnixSeconds()},
           {"time", UnixSeconds()},
           {"timestamp", UnixMilliseconds()},
           {"server_time", UnixSeconds()},
           {"serverTime", UnixSeconds()},
       }},
  });
}

}  // namespace echo::core
