#include "echo/async/RequestWatchdog.h"

#include <windows.h>

namespace echo::async {

namespace {
bool PinWatchdogModule() {
  // This watchdog deliberately has process-lifetime threads (see header).
  // FreeLibrary must not unmap their code after a clean C API shutdown.
  static const bool pinned = [] {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&PinWatchdogModule), &module) != FALSE;
  }();
  return pinned;
}
}  // namespace

RequestWatchdog& RequestWatchdog::Instance() {
  static RequestWatchdog wd;
  return wd;
}

void RequestWatchdog::InsertEntry(State& state, State::Entry entry) {
  const State::DeadlineKey key{entry.deadline, entry.seq};
  const auto it = state.deadlines.emplace(key, std::move(entry)).first;
  try {
    state.byFlag.emplace(it->second.claimed.get(), it);
  } catch (...) {
    state.deadlines.erase(it);
    throw;
  }
}

RequestWatchdog::State::Entry RequestWatchdog::ExtractEntry(
    State& state, State::Deadlines::iterator it) {
  const auto range = state.byFlag.equal_range(it->second.claimed.get());
  for (auto index = range.first; index != range.second; ++index) {
    if (index->second == it) {
      state.byFlag.erase(index);
      break;
    }
  }
  auto node = state.deadlines.extract(it);
  return std::move(node.mapped());
}

bool RequestWatchdog::Arm(long timeoutMs,
                          std::shared_ptr<std::atomic_bool> claimed,
                          std::function<void()> action) {
  if (timeoutMs <= 0 || !claimed || !action) return false;
  if (!PinWatchdogModule()) return false;  // start no background code unpinned

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

  try {
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
    InsertEntry(*state_, std::move(entry));
  } catch (...) {
    state_->pending.fetch_sub(1, std::memory_order_acq_rel);
    throw;
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
  State::Entry retired;
  {
    std::lock_guard lock(state_->heapMu);
    const auto index = state_->byFlag.find(claimed.get());
    if (index == state_->byFlag.end()) return;
    retired = ExtractEntry(*state_, index->second);
  }
  // O(log n) erasure; no copies of callbacks or their captured resources.
  // Reclaim captures after unlocking: their destructors may call Cancel.
  state_->pending.fetch_sub(1, std::memory_order_acq_rel);
  state_->heapCv.notify_one();
}

RequestWatchdog::~RequestWatchdog() {
  auto st = state_;
  State::Deadlines retired;
  {
    std::lock_guard<std::mutex> lock(st->heapMu);
    st->timerStop = true;
    retired.swap(st->deadlines);
    st->byFlag.clear();
  }
  st->pending.fetch_sub(retired.size(), std::memory_order_acq_rel);
  retired.clear();  // Release undelivered captures even if an executor is blocked.
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
  // A failed thread creation must allow a later Arm to start only the
  // missing workers, without replacing a still-joinable timer thread.
  if (!timer_.joinable()) timer_ = std::thread(TimerProc, state_);
  while (executors_.size() < kExecutorThreads) {
    executors_.emplace_back(ExecutorProc, state_);
  }
  workersStarted_ = true;
}

void RequestWatchdog::TimerProc(std::shared_ptr<State> st) {
  for (;;) {
    State::Entry expired;
    {
      std::unique_lock<std::mutex> lock(st->heapMu);
      for (;;) {
        if (st->timerStop) return;
        if (st->deadlines.empty()) {
          st->heapCv.wait(lock, [&] { return st->timerStop || !st->deadlines.empty(); });
          if (st->timerStop) return;
          continue;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto first = st->deadlines.begin();
        if (first->second.deadline > now) {
          // Copy the time point: Cancel can erase this node during the wait.
          const auto deadline = first->second.deadline;
          st->heapCv.wait_until(lock, deadline);
          continue;
        }
        expired = ExtractEntry(*st, first);
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
    if (st->timerStop) {
      st->pending.fetch_sub(1, std::memory_order_acq_rel);
      return;
    }
    State::Entry retry;
    retry.deadline = std::chrono::steady_clock::now() + kRetryDelay;
    retry.seq = st->nextSeq++;
    retry.claimed = std::move(expired.claimed);
    retry.action = std::move(expired.action);
    retry.owned = true;
    InsertEntry(*st, std::move(retry));
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
    try { task(); } catch (...) {
      // One deadline action must not terminate the executor or lose quota.
    }
    task = {};  // Reclaim captures before relocking or reporting completion.
    st->pending.fetch_sub(1, std::memory_order_acq_rel);
    lock.lock();
  }
}

}  // namespace echo::async
