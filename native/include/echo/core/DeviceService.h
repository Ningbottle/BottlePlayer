#pragma once

#include "echo/core/Dto.h"
#include "echo/storage/DeviceRepository.h"

namespace echo::core {

class DeviceService {
 public:
  explicit DeviceService(storage::DeviceRepository& devices);

  DeviceInfo EnsureDeviceReady();

 private:
  storage::DeviceRepository& devices_;
};

// Concept-edition Android mid resolution.
// Prefers an already-stored decimal mid, then derives from guid, then
// falls back to whatever mid is available.  Returns "0" when nothing
// can be resolved.
std::string ResolveAndroidMid(const DeviceInfo& device);

// In-memory identity backfill used by EnsureDeviceReady before any request:
// empty mid/uuid are derived deterministically from dfid (never persisted).
// Exposed so the diagnostics snapshot-consistency check can normalize the
// PERSISTED row through the exact same derivation as the request path —
// calling ResolveAndroidMid on the raw persisted row would yield the guid
// fallback, a value the request path never uses once backfill is active,
// and would report a false identity mismatch on every round.
DeviceInfo BackfillInMemoryIdentity(DeviceInfo device);

}  // namespace echo::core

