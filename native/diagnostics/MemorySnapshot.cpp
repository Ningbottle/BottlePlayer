#include "echo/diagnostics/MemorySnapshot.h"

#include <sstream>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

namespace echo::diagnostics {

MemorySnapshot MemorySnapshotProvider::Capture(std::size_t imageCacheBytes,
                                               std::size_t pendingTaskCount,
                                               std::string playbackState) const {
  MemorySnapshot snapshot{};
  snapshot.imageCacheBytes = imageCacheBytes;
  snapshot.pendingTaskCount = pendingTaskCount;
  snapshot.playbackState = std::move(playbackState);

#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  if (GetProcessMemoryInfo(
          GetCurrentProcess(),
          reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
          sizeof(counters))) {
    snapshot.workingSetBytes = static_cast<std::size_t>(counters.WorkingSetSize);
    snapshot.privateBytes = static_cast<std::size_t>(counters.PrivateUsage);
  }
#endif

  return snapshot;
}

std::string FormatMemorySnapshot(const MemorySnapshot& snapshot) {
  std::ostringstream stream;
  stream << "memory working_set=" << snapshot.workingSetBytes
         << " private=" << snapshot.privateBytes
         << " image_cache=" << snapshot.imageCacheBytes
         << " pending_tasks=" << snapshot.pendingTaskCount
         << " playback=" << snapshot.playbackState;
  return stream.str();
}

}  // namespace echo::diagnostics
