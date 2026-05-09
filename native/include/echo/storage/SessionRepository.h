#pragma once

#include <optional>

#include "echo/core/Dto.h"
#include "echo/storage/Database.h"

namespace echo::storage {

class SessionRepository {
 public:
  explicit SessionRepository(Database& database);

  std::optional<echo::core::SessionInfo> Load();
  void Save(const echo::core::SessionInfo& session);
  void Clear();

 private:
  Database& database_;
};

}  // namespace echo::storage

