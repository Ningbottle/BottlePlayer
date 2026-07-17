#include "echo/async/RequestWatchdog.h"

namespace echo::async {

RequestWatchdog& RequestWatchdog::Instance() {
  static RequestWatchdog wd;
  return wd;
}

void RequestWatchdog::Arm(long timeoutMs,
                          std::shared_ptr<std::atomic_bool> claimed,
                          std::function<void()> action) {
  if (timeoutMs <= 0 || !claimed || !action) return;
  Entry entry;
  entry.deadline = std::chrono::steady_clock::now() +
                   std::chrono::milliseconds(timeoutMs);
  entry.seq = nextSeq_.fetch_add(1, std::memory_order_relaxed);
  entry.claimed = std::move(claimed);
  entry.action = std::move(action);
  {
    std::lock_guard<std::mutex> lock(mu_);
    EnsureWorkerLocked();
    heap_.push(std::move(entry));
  }
  cv_.notify_one();
}

RequestWatchdog::~RequestWatchdog() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    stop_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void RequestWatchdog::EnsureWorkerLocked() {
  if (workerStarted_) return;
  workerStarted_ = true;
  worker_ = std::thread([this] { Loop(); });
}

void RequestWatchdog::Loop() {
  for (;;) {
    Entry expired;
    {
      std::unique_lock<std::mutex> lock(mu_);
      for (;;) {
        if (stop_) return;
        if (heap_.empty()) {
          cv_.wait(lock, [this] { return stop_ || !heap_.empty(); });
          if (stop_) return;
          continue;
        }
        const auto now = std::chrono::steady_clock::now();
        if (heap_.top().deadline > now) {
          cv_.wait_until(lock, heap_.top().deadline);
          continue;
        }
        expired = heap_.top();
        heap_.pop();
        break;
      }
    }
    bool expected = false;
    if (expired.claimed &&
        expired.claimed->compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
      if (expired.action) expired.action();
    }
  }
}

}  // namespace echo::async
