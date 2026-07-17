#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#if defined(ECHO_NATIVE_HAS_SQLITE)
#include <sqlite3.h>
#endif

namespace echo::storage {

// Bound parameter for ExecuteBound / ExecuteQueryBound.
using BindValue = std::variant<std::int64_t, double, std::string>;

class Database {
 public:
  Database();
  ~Database();

  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  void Open(const std::filesystem::path& path);
  void Close();
  void Initialize();

  // Write path (serialized on write_mutex_).
  void Execute(const std::string& sql);
  void ExecuteBound(const std::string& sql, const std::vector<BindValue>& params);

  // Read path (WAL snapshot via thread_local read connection when SQLite).
  // Prepare failure returns empty rows (legacy tolerance).
  std::vector<std::vector<std::string>> ExecuteQuery(const std::string& sql) const;
  std::vector<std::vector<std::string>> ExecuteQueryBound(
      const std::string& sql, const std::vector<BindValue>& params) const;

  void SetJson(const std::string& key, const nlohmann::json& value);
  std::optional<nlohmann::json> GetJson(const std::string& key) const;
  void PutApiCache(const std::string& key, const nlohmann::json& value, std::int64_t expiresAt);
  std::optional<nlohmann::json> GetApiCache(const std::string& key, std::int64_t now) const;
  void PruneExpiredApiCache(std::int64_t now);

 private:
  std::filesystem::path path_;

  // Serializes writers only (Execute*/SetJson/Put*/Prune). Reads use WAL RO
  // connections and do not take this lock once opened.
  mutable std::mutex write_mutex_;

#if defined(ECHO_NATIVE_HAS_SQLITE)
  void InitializeSchema();
  sqlite3* WriteDb();  // requires write_mutex_ held for mutators as needed
  sqlite3* ReadDb() const;
  void ApplyBusyTimeout(sqlite3* db) const;

  sqlite3* db_ = nullptr;  // write connection
  bool schema_ready_ = false;
#else
  void FlushFallback() const;

  nlohmann::json fallback_;
#endif
};

}  // namespace echo::storage
