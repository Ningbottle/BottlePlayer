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

// TEST SEAM (B04): when installed, quarantine renames go through this hook
// instead of std::filesystem::rename; returning false (with ec set) forces
// the recovery paths to observe a rename failure, so tests can verify the
// original database and its WAL/SHM sidecars survive byte-for-byte. Storage
// of the hook is global; production code never installs one.
using QuarantineRenameHook = bool (*)(const std::filesystem::path& from,
                                      const std::filesystem::path& to,
                                      std::error_code& ec);
void SetQuarantineRenameHookForTest(QuarantineRenameHook hook);

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
  // Prepare, bind, and step failures throw std::runtime_error — a failed
  // query must never be observable as an empty result (B03: missing tables,
  // SQL errors, locks, and I/O faults used to masquerade as "no rows" and
  // partial row sets as complete ones). Normal zero-row results remain
  // normal results.
  std::vector<std::vector<std::string>> ExecuteQuery(const std::string& sql) const;
  std::vector<std::vector<std::string>> ExecuteQueryBound(
      const std::string& sql, const std::vector<BindValue>& params) const;

  void SetJson(const std::string& key, const nlohmann::json& value);
  std::optional<nlohmann::json> GetJson(const std::string& key) const;
  // Atomic read-modify-write of one JSON key, executed as a single actor
  // operation. The mutator receives the current value (nullopt when the key
  // is absent) and returns the new value; returning nullopt leaves the key
  // unchanged (a "reject / no-op" outcome). To CLEAR the key, return an
  // explicit empty object instead. Because read, decide, and write all run
  // inside one actor task, no interleaving operation can observe or modify
  // the key between them — this is the primitive that makes conditional
  // session commits (account-generation checks) race-free.
  std::optional<nlohmann::json> UpdateJson(
      const std::string& key,
      const std::function<std::optional<nlohmann::json>(std::optional<nlohmann::json>)>& mutator);
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
  std::optional<nlohmann::json> UpdateJsonLocked(
      const std::string& key,
      const std::function<std::optional<nlohmann::json>(std::optional<nlohmann::json>)>& mutator);
  void PutApiCacheLocked(const std::string& key, const nlohmann::json& value, std::int64_t expiresAt);
  std::optional<nlohmann::json> GetApiCacheLocked(const std::string& key, std::int64_t now) const;
  void PruneExpiredApiCacheLocked(std::int64_t now);

  sqlite3* db_ = nullptr;
  bool schema_ready_ = false;
#else
  void FlushFallback() const;
  void SetJsonLocked(const std::string& key, const nlohmann::json& value);
  std::optional<nlohmann::json> GetJsonLocked(const std::string& key) const;
  std::optional<nlohmann::json> UpdateJsonLocked(
      const std::string& key,
      const std::function<std::optional<nlohmann::json>(std::optional<nlohmann::json>)>& mutator);
  void PutApiCacheLocked(const std::string& key, const nlohmann::json& value, std::int64_t expiresAt);
  std::optional<nlohmann::json> GetApiCacheLocked(const std::string& key, std::int64_t now) const;
  void PruneExpiredApiCacheLocked(std::int64_t now);

  nlohmann::json fallback_;
#endif
};

}  // namespace echo::storage
