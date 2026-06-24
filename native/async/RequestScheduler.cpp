#include "echo/async/RequestScheduler.h"

#include <chrono>

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

bool RequestScheduler::EnqueueJob(Job job) {
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (shutdown_) return false;
    if (queue_.size() >= maxQueueSize_) return false;
    queue_.push_back(std::move(job));
  }
  cv_.notify_one();
  return true;
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

std::size_t RequestScheduler::Shutdown(std::chrono::milliseconds maxWait) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) return 0;
    shutdown_ = true;
  }
  // Best-effort: signal cancellation on all kinds so jobs that honor
  // cancellation can abort before maxWait elapses.
  for (std::size_t i = 0; i < kKindCount; ++i) {
    Cancel(static_cast<RequestKind>(i));
  }
  cv_.notify_all();

  const auto deadline = std::chrono::steady_clock::now() + maxWait;
  std::size_t abandoned = 0;
  // Swap out workers_ so the destructor doesn't try to join threads we
  // may abandon. Abandoned workers are detached below; the std::thread
  // objects are destroyed (now non-joinable) at end of scope.
  std::vector<std::thread> workersToReap;
  workersToReap.swap(workers_);

  for (auto& worker : workersToReap) {
    if (!worker.joinable()) continue;
    // Try to join with a deadline. We poll a shared atomic flag set by a
    // helper thread that calls join() on the worker.
    auto done = std::make_shared<std::atomic_bool>(false);
    // We move `worker` into a unique_ptr so we can transfer ownership to
    // either the helper (on success) or detach (on timeout). After
    // ownership transfer, the std::thread in this loop is no longer valid
    // for join/detach — the helper or the OS owns it.
    auto* workerPtr = new std::thread(std::move(worker));
    std::thread helper([workerPtr, done] {
      workerPtr->join();
      done->store(true, std::memory_order_release);
      delete workerPtr;
    });
    bool finished = false;
    while (std::chrono::steady_clock::now() < deadline) {
      if (done->load(std::memory_order_acquire)) {
        finished = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (finished) {
      helper.join();
    } else {
      // Worker is stuck in an uninterruptible call. Detach the helper —
      // the helper will return when the worker eventually finishes its
      // job and WorkerLoop returns (it will see shutdown_=true and
      // queue_.empty() and exit). The process is expected to be exiting
      // so any remaining lifetime issues are acceptable.
      helper.detach();
      ++abandoned;
    }
  }
  return abandoned;
}

}  // namespace echo::async
