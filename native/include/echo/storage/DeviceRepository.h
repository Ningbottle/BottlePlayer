#pragma once

#include <optional>

#include "echo/model/Identity.h"
#include "echo/execution/OperationContext.h"
#include "echo/storage/Database.h"

namespace echo::storage {

class DeviceRepository {
 public:
  explicit DeviceRepository(Database& database);

  std::optional<model::DeviceInfo> Load();
  model::DeviceInfo Ensure(const std::function<model::DeviceInfo(std::optional<model::DeviceInfo>)>& normalize);
  void Save(const model::DeviceInfo& device);
  void Clear();

 private:
  Database& database_;
};

}  // namespace echo::storage

