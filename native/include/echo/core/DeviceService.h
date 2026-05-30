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

}  // namespace echo::core

