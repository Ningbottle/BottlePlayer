#pragma once

#include <cstddef>
#include <string>

#include "echo/storage/Database.h"

namespace echo::storage {

struct AppSettings {
  double volume = 0.48;
  std::string startupPage = "home";
  std::size_t imageMemoryCacheMb = 32;
};

class SettingsRepository {
 public:
  explicit SettingsRepository(Database& database);

  AppSettings Load() const;
  void Save(const AppSettings& settings);

 private:
  Database& database_;
};

}  // namespace echo::storage
