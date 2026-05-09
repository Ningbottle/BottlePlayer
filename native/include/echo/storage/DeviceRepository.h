#pragma once

#include <optional>

#include "echo/core/Dto.h"
#include "echo/storage/Database.h"

namespace echo::storage {

class DeviceRepository {
 public:
  explicit DeviceRepository(Database& database);

  std::optional<echo::core::DeviceInfo> Load();
  void Save(const echo::core::DeviceInfo& device);

 private:
  Database& database_;
};

}  // namespace echo::storage

