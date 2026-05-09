#pragma once

#include <cstddef>
#include <string>

namespace echo::diagnostics {

struct MemorySnapshot {
  std::size_t workingSetBytes = 0;
  std::size_t privateBytes = 0;
  std::size_t imageCacheBytes = 0;
  std::size_t pendingTaskCount = 0;
  std::string playbackState;
};

class MemorySnapshotProvider {
 public:
  MemorySnapshot Capture(std::size_t imageCacheBytes,
                         std::size_t pendingTaskCount,
                         std::string playbackState) const;
};

std::string FormatMemorySnapshot(const MemorySnapshot& snapshot);

}  // namespace echo::diagnostics
