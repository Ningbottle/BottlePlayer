#include "echo/async/RequestScheduler.h"

#include <algorithm>
#include <chrono>

namespace echo::async {

RequestScheduler::RequestScheduler(std::size_t workerCount)
    : workerCount_(workerCount == 0 ? 1 : workerCount),
      state_(std::make_shared<State>(workerCount_)) {
  StartWorkers();
}

void RequestScheduler::StartWorkers() {
  try {
    workers_.reserve(workerCount_);
    for (std::size_t i = 0; i < workerCount_; ++i)
      workers_.emplace_back(WorkerLoop, state_, i);
  } catch (...) {
    {
      std::lock_guard lock(state_->mutex);
      state_->shutdown = true;
    }
    state_->cv.notify_all();
    for (auto& worker : workers_) if (worker.joinable()) worker.join();
    workers_.clear();
    throw;
  }
}

RequestScheduler::~RequestScheduler() {
  Shutdown();
}

void RequestScheduler::WorkerLoop(std::shared_ptr<State> state, std::size_t index) {
  for (;;) {
    Job job;
    {
      std::unique_lock lock(state->mutex);
      const auto eligible = [&](const Job& item) {
        return item.kind != RequestKind::Image ||
               state->activeImageCount < state->maxImageConcurrent;
      };
      state->cv.wait(lock, [&] {
        return (state->shutdown && state->queue.empty()) ||
               std::any_of(state->queue.begin(), state->queue.end(), eligible);
      });
      if (state->shutdown && state->queue.empty()) {
        state->workerDone[index] = true;
        lock.unlock();
        state->stopped.notify_all();
        return;
      }
      const auto it = std::find_if(state->queue.begin(), state->queue.end(), eligible);
      job = std::move(*it);
      state->queue.erase(it);
      state->activeTokens[index] = job.tokenFlag;
      if (job.kind == RequestKind::Image) ++state->activeImageCount;
    }
    if (job.execute) {
      try { job.execute(); } catch (...) {
        // Promise completion belongs to the submission wrapper.
      }
    }
    {
      std::lock_guard lock(state->mutex);
      state->activeTokens[index].reset();
      if (job.kind == RequestKind::Image) --state->activeImageCount;
    }
    // Only image completion makes previously ineligible work runnable.
    if (job.kind == RequestKind::Image) state->cv.notify_all();
  }
}

std::shared_ptr<std::atomic_bool> RequestScheduler::PrepareLatestToken(
    RequestKind kind, std::uint64_t& outGen) {
  const std::size_t idx = static_cast<std::size_t>(kind);
  if (idx >= kKindCount) throw std::out_of_range("invalid_request_kind");
  auto newFlag = std::make_shared<std::atomic_bool>(false);
  std::lock_guard lock(kindMutex_);
  // Generation order must match token replacement order under concurrency.
  outGen = generations_[idx].fetch_add(1, std::memory_order_acq_rel) + 1;
  auto oldFlag = std::move(cancelledFlags_[idx]);
  cancelledFlags_[idx] = newFlag;
  if (oldFlag) oldFlag->store(true, std::memory_order_release);
  return newFlag;
}

bool RequestScheduler::EnqueueJob(Job job, const std::shared_ptr<std::atomic_bool>& deadlineClaimed) {
  {
    std::lock_guard lock(state_->mutex);
    // The watchdog can fire before submission reaches this lock. Its owner
    // will deliver job_deadline; do not retain or run that already expired job.
    if (deadlineClaimed && deadlineClaimed->load(std::memory_order_acquire)) return true;
    if (state_->shutdown || state_->queue.size() >= state_->maxQueueSize) return false;
    if (job.kind == RequestKind::Image &&
        state_->queue.size() >= state_->maxQueueSize - workerCount_) return false;
    state_->queue.push_back(std::move(job));
  }
  state_->cv.notify_one();
  return true;
}

void RequestScheduler::RetireExpiredJob(const std::weak_ptr<State>& weakState,
                                      const std::shared_ptr<std::atomic_bool>& tokenFlag) {
  const auto state = weakState.lock();
  if (!state) return;
  Job retired;
  {
    std::lock_guard lock(state->mutex);
    const auto it = std::find_if(state->queue.begin(), state->queue.end(), [&](const Job& job) {
      return job.tokenFlag == tokenFlag;
    });
    if (it == state->queue.end()) return; // A running worker owns its captures.
    retired = std::move(*it);
    state->queue.erase(it);
  }
  // Shutdown may now have an empty queue. Captures are destroyed after the
  // queue lock is released, allowing destructors to submit/cancel other work.
  state->cv.notify_all();
}

void RequestScheduler::Cancel(RequestKind kind) {
  const std::size_t idx = static_cast<std::size_t>(kind);
  if (idx >= kKindCount) return;
  std::lock_guard lock(kindMutex_);
  auto oldFlag = std::move(cancelledFlags_[idx]);
  if (oldFlag) oldFlag->store(true, std::memory_order_release);
  generations_[idx].fetch_add(1, std::memory_order_acq_rel);
}

void RequestScheduler::Shutdown() {
  std::lock_guard lifecycleLock(lifecycleMutex_);
  {
    std::lock_guard lock(state_->mutex);
    if (state_->shutdown) return;
    state_->shutdown = true;
  }
  state_->cv.notify_all();
  for (auto& worker : workers_) if (worker.joinable()) worker.join();
  workers_.clear();
}

std::size_t RequestScheduler::Shutdown(std::chrono::milliseconds maxWait) {
  std::lock_guard lifecycleLock(lifecycleMutex_);
  const auto deadline = std::chrono::steady_clock::now() + maxWait;
  std::deque<Job> rejected;
  {
    std::lock_guard lock(state_->mutex);
    if (state_->shutdown) return state_->abandonedWorkers;
    state_->shutdown = true;
    for (const auto& flag : state_->activeTokens)
      if (flag) flag->store(true, std::memory_order_release);
    for (const auto& job : state_->queue)
      job.tokenFlag->store(true, std::memory_order_release);
    rejected.swap(state_->queue);
  }
  state_->cv.notify_all();
  for (auto& job : rejected) {
    if (job.reject) {
      try { job.reject(); } catch (...) {}
    }
  }
  rejected.clear();  // Destroy request captures outside the scheduler mutex.

  {
    std::unique_lock lock(state_->mutex);
    state_->stopped.wait_until(lock, deadline, [&] {
      return std::all_of(state_->workerDone.begin(), state_->workerDone.end(),
                         [](bool done) { return done; });
    });
  }
  std::size_t abandoned = 0;
  for (std::size_t i = 0; i < workers_.size(); ++i) {
    auto& worker = workers_[i];
    if (!worker.joinable()) continue;
    bool completed;
    {
      std::lock_guard lock(state_->mutex);
      completed = state_->workerDone[i];
    }
    if (completed) worker.join();
    else {
      worker.detach();
      ++abandoned;
    }
  }
  workers_.clear();
  {
    std::lock_guard lock(state_->mutex);
    state_->abandonedWorkers = abandoned;
  }
  return abandoned;
}

bool RequestScheduler::Restart() {
  std::lock_guard lifecycleLock(lifecycleMutex_);
  {
    std::lock_guard lock(state_->mutex);
    if (!state_->shutdown) return true;
    if (state_->abandonedWorkers != 0) return false;
    std::fill(state_->workerDone.begin(), state_->workerDone.end(), false);
    state_->shutdown = false;
  }
  // State stays stable for concurrent submitters. Only a clean generation restarts.
  StartWorkers();
  return true;
}

}  // namespace echo::async
