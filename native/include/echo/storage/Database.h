#pragma once

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
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

  // All public DB access is serialized on a single storage actor thread (no TLS
  // snapshot isolation). Cross-thread SetJson then GetJson is linearizable:
  // Submit enqueues under queue_mutex_ and future.get() establishes happens-before
  // via the actor queue, so a completed SetJson is visible to a later GetJson.
  void Execute(const std::string& sql);
  void ExecuteBound(const std::string& sql, const std::vector<BindValue>& params);

  // Reads share the same actor serialization as writes (linearizable).
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
  enum class ActorState { Closed, Starting, Open, Closing, Failed };

  // Actor infrastructure (queue/state under queue_mutex_).
  std::thread actor_;
  mutable std::queue<std::function<void()>> task_queue_;
  mutable std::mutex queue_mutex_;
  mutable std::condition_variable queue_cv_;
  ActorState state_{ActorState::Closed};
  std::thread::id actor_tid_{};

  void StartActor();
  void ActorLoop();

  // Submit: lock-held state==Open check; callable + promise owned by value.
  template <typename F>
  auto Submit(F&& fn) const -> std::invoke_result_t<F> {
    using R = std::invoke_result_t<F>;
    auto promise = std::make_shared<std::promise<R>>();
    auto future = promise->get_future();
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (state_ != ActorState::Open) {
        throw std::runtime_error("database_not_accepting");
      }
      if (std::this_thread::get_id() == actor_tid_) {
        throw std::runtime_error("actor_reentrancy");
      }
      task_queue_.emplace([fn = std::forward<F>(fn), promise]() mutable {
        try {
          if constexpr (std::is_void_v<R>) {
            fn();
            promise->set_value();
          } else {
            promise->set_value(fn());
          }
        } catch (...) {
          promise->set_exception(std::current_exception());
        }
      });
    }
    queue_cv_.notify_one();
    return future.get();
  }

  // path_ written/read only on the actor thread (via *Locked methods).
  std::filesystem::path path_;

  void OpenLocked(std::filesystem::path path);
  void CloseLocked();
  void InitializeLocked();

#if defined(ECHO_NATIVE_HAS_SQLITE)
  void InitializeSchema();
  void ApplyBusyTimeout(sqlite3* db) const;
  void ExecuteLocked(const std::string& sql);
  void ExecuteBoundLocked(const std::string& sql, const std::vector<BindValue>& params);
  std::vector<std::vector<std::string>> ExecuteQueryBoundLocked(
      const std::string& sql, const std::vector<BindValue>& params) const;
  void SetJsonLocked(const std::string& key, const nlohmann::json& value);
  std::optional<nlohmann::json> GetJsonLocked(const std::string& key) const;
  void PutApiCacheLocked(const std::string& key, const nlohmann::json& value, std::int64_t expiresAt);
  std::optional<nlohmann::json> GetApiCacheLocked(const std::string& key, std::int64_t now) const;
  void PruneExpiredApiCacheLocked(std::int64_t now);

  sqlite3* db_ = nullptr;
  bool schema_ready_ = false;
#else
  void FlushFallback() const;
  void SetJsonLocked(const std::string& key, const nlohmann::json& value);
  std::optional<nlohmann::json> GetJsonLocked(const std::string& key) const;
  void PutApiCacheLocked(const std::string& key, const nlohmann::json& value, std::int64_t expiresAt);
  std::optional<nlohmann::json> GetApiCacheLocked(const std::string& key, std::int64_t now) const;
  void PruneExpiredApiCacheLocked(std::int64_t now);

  nlohmann::json fallback_;
#endif
};

}  // namespace echo::storage
