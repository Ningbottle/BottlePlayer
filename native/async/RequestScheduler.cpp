#include "echo/async/RequestScheduler.h"

namespace echo::async {

RequestScheduler::RequestScheduler(std::size_t workerCount)
    : workerCount_(workerCount == 0 ? 1 : workerCount),
      maxQueueSize_(workerCount_ * 4) {
  for (std::size_t i = 0; i < workerCount_; ++i) {
    workers_.emplace_back([this] { WorkerLoop(); });
  }
}

RequestScheduler::~RequestScheduler() {
  Shutdown();
}

void RequestScheduler::WorkerLoop() {
  while (true) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return shutdown_ || !queue_.empty(); });
      if (shutdown_ && queue_.empty()) {
        return;
      }
      job = std::move(queue_.front());
      queue_.pop_front();
    }
    cv_.notify_one();  // wake a submitter that may be waiting for space
    if (job.execute) {
      try {
        job.execute();
      } catch (...) {
        // Worker must stay alive even if a job throws. Promise safety is
        // handled inside Submit/SubmitLatest execute lambdas.
      }
    }
  }
}

std::shared_ptr<std::atomic_bool> RequestScheduler::PrepareLatestToken(
    RequestKind kind, std::uint64_t& outGen) {
  const std::size_t idx = static_cast<std::size_t>(kind);
  outGen = generations_[idx].fetch_add(1, std::memory_order_acq_rel) + 1;
  auto newFlag = std::make_shared<std::atomic_bool>(false);
  std::lock_guard<std::mutex> lock(kindMutex_);
  auto oldFlag = cancelledFlags_[idx];
  cancelledFlags_[idx] = newFlag;
  if (oldFlag) {
    oldFlag->store(true, std::memory_order_release);
  }
  return newFlag;
}

void RequestScheduler::EnqueueJob(Job job) {
  {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return shutdown_ || queue_.size() < maxQueueSize_; });
    if (shutdown_) {
      // Fallback: execute synchronously so the promise is always fulfilled.
      lock.unlock();
      if (job.execute) {
        job.execute();
      }
      return;
    }
    queue_.push_back(std::move(job));
  }
  cv_.notify_one();
}

void RequestScheduler::Cancel(RequestKind kind) {
  const std::size_t idx = static_cast<std::size_t>(kind);
  auto newFlag = std::make_shared<std::atomic_bool>(false);
  std::lock_guard<std::mutex> lock(kindMutex_);
  auto oldFlag = cancelledFlags_[idx];
  cancelledFlags_[idx] = newFlag;
  if (oldFlag) {
    oldFlag->store(true, std::memory_order_release);
  }
  generations_[idx].fetch_add(1, std::memory_order_acq_rel);
}

void RequestScheduler::Shutdown() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) return;
    shutdown_ = true;
  }
  cv_.notify_all();
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

}  // namespace echo::async
