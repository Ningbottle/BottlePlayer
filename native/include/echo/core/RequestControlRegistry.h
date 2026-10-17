#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include "echo/execution/OperationContext.h"

namespace echo::core {

// Tracks caller cancellation that can arrive before request admission. Orphan
// flags are short-lived and are the only entries evicted at capacity.
class RequestControlRegistry {
 public:
  explicit RequestControlRegistry(
      std::chrono::seconds orphanTtl = std::chrono::seconds(5),
      std::size_t maxEntries = 64)
      : orphanTtl_(orphanTtl), maxEntries_(maxEntries) {}

  std::shared_ptr<std::atomic_bool> Register(const std::string& id) {
    std::lock_guard lock(mutex_);
    Prune();
    auto found = entries_.find(id);
    if (found != entries_.end()) {
      if (found->second.started) throw std::runtime_error("duplicate_request_id");
      found->second.started = true;
      return found->second.flag;
    }
    if (!EnsureCapacity()) throw std::runtime_error("request_control_overload");
    auto& entry = entries_.try_emplace(id, orphanTtl_).first->second;
    entry.started = true;
    return entry.flag;
  }

  int Cancel(const std::string& id) {
    std::lock_guard lock(mutex_);
    Prune();
    auto found = entries_.find(id);
    if (found == entries_.end()) {
      if (!EnsureCapacity()) return -2;
      found = entries_.try_emplace(id, orphanTtl_).first;
    }
    if (found->second.stop) found->second.stop->Cancel();
    found->second.flag->store(true, std::memory_order_release);
    return 0;
  }

  void BindStop(const std::string& id, std::shared_ptr<execution::StopState> stop) {
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(id);
    if (found == entries_.end()) return;
    found->second.stop = std::move(stop);
    if (found->second.flag->load(std::memory_order_acquire)) found->second.stop->Cancel();
  }

  void Finish(const std::string& id) {
    std::lock_guard lock(mutex_);
    entries_.erase(id);
  }

 private:
  struct Entry {
    explicit Entry(std::chrono::seconds ttl)
        : expires(std::chrono::steady_clock::now() + ttl) {}
    std::shared_ptr<std::atomic_bool> flag = std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<execution::StopState> stop;
    bool started = false;
    std::chrono::steady_clock::time_point expires;
  };

  void Prune() {
    const auto now = std::chrono::steady_clock::now();
    for (auto it = entries_.begin(); it != entries_.end();) {
      if (!it->second.started && it->second.expires < now) it = entries_.erase(it);
      else ++it;
    }
  }

  bool EnsureCapacity() {
    if (entries_.size() < maxEntries_) return true;
    auto oldest = entries_.end();
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (it->second.started) continue;
      if (oldest == entries_.end() || it->second.expires < oldest->second.expires) oldest = it;
    }
    if (oldest == entries_.end()) return false;
    entries_.erase(oldest);
    return true;
  }

  std::chrono::seconds orphanTtl_;
  std::size_t maxEntries_;
  std::mutex mutex_;
  std::unordered_map<std::string, Entry> entries_;
};

}  // namespace echo::core
