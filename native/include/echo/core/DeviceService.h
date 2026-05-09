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

}  // namespace echo::core

