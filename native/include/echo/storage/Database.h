#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#if defined(ECHO_NATIVE_HAS_SQLITE)
#include <sqlite3.h>
#endif

namespace echo::storage {

class Database {
 public:
  Database();
  ~Database();

  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  void Open(const std::filesystem::path& path);
  void Close();
  void Initialize();

  void Execute(const std::string& sql);
  void SetJson(const std::string& key, const nlohmann::json& value);
  std::optional<nlohmann::json> GetJson(const std::string& key) const;
  void PutApiCache(const std::string& key, const nlohmann::json& value, std::int64_t expiresAt);
  std::optional<nlohmann::json> GetApiCache(const std::string& key, std::int64_t now) const;
  void PruneExpiredApiCache(std::int64_t now);

 private:
  std::filesystem::path path_;

#if defined(ECHO_NATIVE_HAS_SQLITE)
  void InitializeSchema();

  sqlite3* db_ = nullptr;
#else
  void FlushFallback() const;

  nlohmann::json fallback_;
#endif
};

}  // namespace echo::storage
