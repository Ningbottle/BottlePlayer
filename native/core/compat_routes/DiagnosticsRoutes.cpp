#include "echo/core/CompatApiUtils.h"
#include "echo/diagnostics/MemorySnapshot.h"

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

CompatResponse HandleDiagnosticsMemory() {
  echo::diagnostics::MemorySnapshotProvider provider;
  // workingSet / private 由 PSAPI 真实读取；image cache / pending task / playback
  // 不在 FFI 请求路径内（播放在 WebView），如实报 0 / "webview"。
  const auto snapshot = provider.Capture(0, 0, "webview");
  return JsonResponse({
      {"status", 1},
      {"data",
       {
           {"working_set_bytes", snapshot.workingSetBytes},
           {"private_bytes", snapshot.privateBytes},
           {"image_cache_bytes", snapshot.imageCacheBytes},
           {"pending_task_count", snapshot.pendingTaskCount},
           {"playback_state", snapshot.playbackState},
           {"text", echo::diagnostics::FormatMemorySnapshot(snapshot)},
       }},
  });
}

}  // namespace echo::core
