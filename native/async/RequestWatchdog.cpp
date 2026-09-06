#include "echo/async/RequestWatchdog.h"

namespace echo::async {

RequestWatchdog& RequestWatchdog::Instance() {
  static RequestWatchdog wd;
  return wd;
}

bool RequestWatchdog::Arm(long timeoutMs,
                          std::shared_ptr<std::atomic_bool> claimed,
                          std::function<void()> action) {
  if (timeoutMs <= 0 || !claimed || !action) return false;

  // Admission control: the WHOLE backlog (heap + queue + running) has a
  // hard ceiling. CAS loop keeps the cap exact under concurrent Arms. A
  // rejected Arm means the caller must fail its operation explicitly —
  // arming silently without deadline enforcement is not an option.
  std::size_t current = state_->pending.load(std::memory_order_relaxed);
  for (;;) {
    if (current >= kMaxPendingEntries) return false;
    if (state_->pending.compare_exchange_weak(current, current + 1,
                                              std::memory_order_acq_rel)) {
      break;
    }
  }

  {
    std::lock_guard<std::mutex> lock(state_->heapMu);
    if (state_->timerStop) {
      state_->pending.fetch_sub(1, std::memory_order_acq_rel);
      return false;
    }
    EnsureWorkersLocked();
    State::Entry entry;
    entry.deadline = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(timeoutMs);
    entry.seq = state_->nextSeq++;
    entry.claimed = std::move(claimed);
    entry.action = std::move(action);
    state_->heap.push(std::move(entry));
  }
  state_->heapCv.notify_one();
  return true;
}

std::size_t RequestWatchdog::DebugPendingActions() const {
  return state_->pending.load(std::memory_order_relaxed);
}

std::size_t RequestWatchdog::DebugQueuedActions() const {
  std::lock_guard<std::mutex> lock(state_->execMu);
  return state_->queue.size();
}

std::uint64_t RequestWatchdog::DebugClaimedCount() const {
  return state_->claimedCount.load(std::memory_order_relaxed);
}

void RequestWatchdog::Cancel(const std::shared_ptr<std::atomic_bool>& claimed) {
  if (!claimed) return;
  std::lock_guard<std::mutex> lock(state_->heapMu);
  if (state_->heap.empty()) return;
  // priority_queue has no erase: rebuild without the matching entry.
  // O(armed) under the lock; armed is bounded by the pending cap.
  std::vector<State::Entry> keep;
  keep.reserve(state_->heap.size());
  bool removed = false;
  while (!state_->heap.empty()) {
    State::Entry entry = state_->heap.top();
    state_->heap.pop();
    if (!removed && entry.claimed.get() == claimed.get()) {
      removed = true;
      continue;
    }
    keep.push_back(std::move(entry));
  }
  // ALWAYS restore the surviving entries: a miss (foreign flag, duplicate
  // cancel, flag whose entry the timer already popped, never-armed flag
  // from a no-deadline job) must leave every other deadline intact.
  // Only a real removal releases pending budget. (The first draft leaked
  // the whole heap on the miss path — caught by review probe.)
  for (auto& entry : keep) state_->heap.push(std::move(entry));
  if (removed) state_->pending.fetch_sub(1, std::memory_order_acq_rel);
}

RequestWatchdog::~RequestWatchdog() {
  auto st = state_;
  {
    std::lock_guard<std::mutex> lock(st->heapMu);
    st->timerStop = true;
  }
  st->heapCv.notify_all();
  {
    std::lock_guard<std::mutex> lock(st->execMu);
    st->execStop = true;
  }
  st->execCv.notify_all();
  // Detach, never join: a blocked action would hang the join (the fault
  // this class contains). Safe because every thread captured state_ and
  // touches only State — see the header's lifetime protocol.
  if (timer_.joinable()) timer_.detach();
  for (auto& t : executors_) {
    if (t.joinable()) t.detach();
  }
}

void RequestWatchdog::EnsureWorkersLocked() {
  if (workersStarted_) return;
  workersStarted_ = true;
  timer_ = std::thread(TimerProc, state_);
  for (std::size_t i = 0; i < kExecutorThreads; ++i) {
    executors_.emplace_back(ExecutorProc, state_);
  }
}

void RequestWatchdog::TimerProc(std::shared_ptr<State> st) {
  for (;;) {
    State::Entry expired;
    {
      std::unique_lock<std::mutex> lock(st->heapMu);
      for (;;) {
        if (st->timerStop) return;
        if (st->heap.empty()) {
          st->heapCv.wait(lock, [&] { return st->timerStop || !st->heap.empty(); });
          if (st->timerStop) return;
          continue;
        }
        const auto now = std::chrono::steady_clock::now();
        if (st->heap.top().deadline > now) {
          st->heapCv.wait_until(lock, st->heap.top().deadline);
          continue;
        }
        expired = st->heap.top();
        st->heap.pop();
        break;
      }
    }

    // Claim arbitration: owned entries already won; fresh entries must win
    // the CAS or be lazy-dropped (the job completed first).
    if (!expired.owned) {
      bool expected = false;
      if (!expired.claimed ||
          !expired.claimed->compare_exchange_strong(
              expected, true, std::memory_order_acq_rel)) {
        // Dead entry: the job won the claim. Its Cancel() either already
        // removed the heap copy (never decrement twice — this popped copy
        // decrements) or raced and found nothing (this decrement is the
        // only one). Exactly one side releases the budget.
        st->pending.fetch_sub(1, std::memory_order_acq_rel);
        continue;
      }
      st->claimedCount.fetch_add(1, std::memory_order_acq_rel);
    }
    if (!expired.action) {
      st->pending.fetch_sub(1, std::memory_order_acq_rel);
      continue;
    }

    bool delivered = false;
    {
      std::lock_guard<std::mutex> lock(st->execMu);
      if (!st->execStop && st->queue.size() < kExecutorQueueCap) {
        st->queue.push_back(std::move(expired.action));
        delivered = true;
      }
    }
    if (delivered) {
      st->execCv.notify_one();
      continue;
    }

    // Saturation: the queue is full (all workers busy on earlier actions)
    // — re-arm with a retry delay, keeping ownership. The accepted deadline
    // is delayed, never dropped, and never executed on this thread.
    std::lock_guard<std::mutex> lock(st->heapMu);
    if (st->timerStop) return;
    State::Entry retry;
    retry.deadline = std::chrono::steady_clock::now() + kRetryDelay;
    retry.seq = st->nextSeq++;
    retry.claimed = std::move(expired.claimed);
    retry.action = std::move(expired.action);
    retry.owned = true;
    st->heap.push(std::move(retry));
  }
}

void RequestWatchdog::ExecutorProc(std::shared_ptr<State> st) {
  std::unique_lock<std::mutex> lock(st->execMu);
  for (;;) {
    st->execCv.wait(lock, [&] { return st->execStop || !st->queue.empty(); });
    if (st->execStop && st->queue.empty()) return;
    // Drain accepted actions before exiting on stop.
    std::function<void()> task = std::move(st->queue.front());
    st->queue.pop_front();
    lock.unlock();
    task();
    st->pending.fetch_sub(1, std::memory_order_acq_rel);
    lock.lock();
  }
}

}  // namespace echo::async
