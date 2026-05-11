#include "echo/storage/SettingsRepository.h"

#include <algorithm>
#include <cstdint>

namespace echo::storage {
namespace {

constexpr const char* kSettingsKey = "app.settings";

std::string ReadString(const nlohmann::json& value, const char* key, std::string fallback) {
  const auto found = value.find(key);
  return found != value.end() && found->is_string() ? found->get<std::string>() : std::move(fallback);
}

std::size_t ReadSize(const nlohmann::json& value, const char* key, std::size_t fallback) {
  const auto found = value.find(key);
  if (found == value.end()) {
    return fallback;
  }
  if (found->is_number_unsigned()) {
    return static_cast<std::size_t>(found->get<std::uint64_t>());
  }
  if (found->is_number_integer()) {
    return static_cast<std::size_t>(std::max<std::int64_t>(0, found->get<std::int64_t>()));
  }
  return fallback;
}

}  // namespace

SettingsRepository::SettingsRepository(Database& database) : database_(database) {}

AppSettings SettingsRepository::Load() const {
  AppSettings settings;
  const auto stored = database_.GetJson(kSettingsKey);
  if (!stored || !stored->is_object()) {
    return settings;
  }

  settings.volume = std::clamp(stored->value("volume", settings.volume), 0.0, 1.0);
  settings.startupPage = ReadString(*stored, "startupPage", settings.startupPage);
  settings.imageMemoryCacheMb = std::clamp<std::size_t>(
      ReadSize(*stored, "imageMemoryCacheMb", settings.imageMemoryCacheMb),
      8,
      128);
  return settings;
}

void SettingsRepository::Save(const AppSettings& settings) {
  database_.SetJson(
      kSettingsKey,
      nlohmann::json{
          {"volume", std::clamp(settings.volume, 0.0, 1.0)},
          {"startupPage", settings.startupPage.empty() ? "home" : settings.startupPage},
          {"imageMemoryCacheMb", std::clamp<std::size_t>(settings.imageMemoryCacheMb, 8, 128)},
      });
}

}  // namespace echo::storage
