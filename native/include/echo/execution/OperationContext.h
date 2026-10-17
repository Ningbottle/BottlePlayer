#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include "echo/execution/OperationPolicy.h"

namespace echo::execution {

enum class StopReason { None, Cancelled, Deadline };

// Cancellation and commit admission share one lock. A granted permit authorizes
// one transaction to finish even if cancellation is accepted afterwards.
class StopState {
 public:
  void Cancel() {
    std::lock_guard lock(mutex_);
    cancelled_ = true;
  }
  bool Cancelled() const {
    std::lock_guard lock(mutex_);
    return cancelled_;
  }
  bool Admit(std::chrono::steady_clock::time_point deadline,
             const std::shared_ptr<const std::atomic_bool>& worker,
             const std::shared_ptr<const std::atomic_bool>& caller) {
    std::lock_guard lock(mutex_);
    return !cancelled_ && std::chrono::steady_clock::now() < deadline &&
        (!worker || !worker->load(std::memory_order_acquire)) &&
        (!caller || !caller->load(std::memory_order_acquire));
  }
 private:
  mutable std::mutex mutex_;
  bool cancelled_ = false;
};

struct OperationContext {
  std::uint64_t id = NextId();
  std::string policyId;
  ExecutionClass executionClass = ExecutionClass::Generic;
  CommitPolicy commitPolicy = CommitPolicy::Read;
  std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
  std::shared_ptr<StopState> stop = std::make_shared<StopState>();
  std::shared_ptr<const std::atomic_bool> workerCancelled;
  std::shared_ptr<const std::atomic_bool> callerCancelled;

  StopReason Reason() const {
    if (std::chrono::steady_clock::now() >= deadline) return StopReason::Deadline;
    if (stop->Cancelled() || (workerCancelled && workerCancelled->load(std::memory_order_acquire)) ||
        (callerCancelled && callerCancelled->load(std::memory_order_acquire))) return StopReason::Cancelled;
    return StopReason::None;
  }
  bool CanContinue() const { return Reason() == StopReason::None; }
  // Invoke only inside the storage transaction, after version validation and
  // payload preparation. No HTTP/TLS dependency and no borrowed cancel flags.
  bool TryCommit() const { return stop->Admit(deadline, workerCancelled, callerCancelled); }
 private:
  static std::uint64_t NextId() {
    static std::atomic<std::uint64_t> sequence{1};
    return sequence.fetch_add(1, std::memory_order_relaxed);
  }
};

// Transition adapter for unported services. New storage/application APIs take
// OperationContext explicitly; only the outer compat/transport boundary uses TLS.
inline thread_local OperationContext currentOperation;
inline OperationContext CurrentOperation() { return currentOperation; }
class OperationScope {
 public:
  explicit OperationScope(const OperationContext& operation) : previous_(currentOperation) {
    currentOperation = operation;
  }
  ~OperationScope() { currentOperation = previous_; }
 private:
  OperationContext previous_;
};

}  // namespace echo::execution
