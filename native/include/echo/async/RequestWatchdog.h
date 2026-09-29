#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace echo::async {

// Process-wide deadline watchdog (Stage 2, root cause R6).
//
// A single timer thread owns a min-heap of (deadline, claimed, action) and
// NEVER executes actions. On expiry it CAS-claims the entry and hands the
// action to a bounded executor: kExecutorThreads workers consuming one
// shared FIFO queue. "Running" and "queued" are distinct — a worker blocked
// in an action does not hold a private mailbox, so idle workers keep
// delivering (the slot-mailbox draft failed exactly here).
//
// Saturation protocol: when the shared queue is at kExecutorQueueCap the
// entry is re-armed into the heap with kRetryDelay and ownership retained
// (`owned` entries skip the CAS) — an accepted deadline is delayed, never
// dropped, never executed on the timer thread. Admission is bounded by
// kMaxPendingEntries = heap + queue + currently-running actions: Arm()
// returns false when that budget is exhausted, and the caller owns the
// rejection feedback (the scheduler fails the request with
// "watchdog_overload" instead of running it without deadline enforcement;
// the HttpClient wrapper's handling lands with Stage 3a). Delivery of a
// retried action terminates only when the action completes, so actions MUST
// be interruptible or bounded; blocking resource reclamation is forbidden
// inside watchdog actions (Stage 3 moves WinHTTP closes back to the owning
// thread). A blocked action stalls the worker pool and delays every later
// deadline — documented degradation, observable via DebugPendingActions().
//
// Lifetime/exit protocol: every thread captures its own shared_ptr<State>
// and touches ONLY State; the watchdog object owns just handles. The
// destructor sets the stop flags, notifies, and detaches. Workers drain the
// remaining queue before exiting; a running action finishes on its own with
// State kept alive by the thread's capture. Joining is deliberately not
// used — it would hang on a blocked action, the exact fault this class
// contains. At process exit the OS reclaims any detached thread still
// running an action; queued-but-undelivered actions whose workers are
// blocked are abandoned (documented process-exit semantics).
//
// Construction is public: production uses Instance() (process-lifetime
// singleton); tests and embedded owners may hold local instances with the
// same exit protocol.
class RequestWatchdog {
 public:
  RequestWatchdog() = default;
  ~RequestWatchdog();

  static RequestWatchdog& Instance();

  // On deadline, if CAS claims, deliver action() to the executor.
  // Returns false when the overall pending budget (kMaxPendingEntries) is
  // exhausted or the watchdog is stopping — the entry was NOT accepted and
  // no deadline was armed; callers must fail the operation explicitly.
  bool Arm(long timeoutMs, std::shared_ptr<std::atomic_bool> claimed,
           std::function<void()> action);

  // Cancel an armed entry after its owner won the claim CAS (job finished
  // before the deadline). Removes the heap entry and releases its pending
  // budget immediately; without this, resolved jobs would hold admission
  // budget until their deadline popped. Exactly one of {Cancel removal,
  // timer lazy-drop} releases the budget for a dead entry.
  void Cancel(const std::shared_ptr<std::atomic_bool>& claimed);

  // Diagnostics: accepted-but-not-completed actions (heap + queue +
  // running). Also the saturation signal for tests.
  std::size_t DebugPendingActions() const;

  // Diagnostics: actions already handed to the executor queue (a superset
  // check for "the timer has dispatched everything armed").
  std::size_t DebugQueuedActions() const;

  // Diagnostics: deadline entries the timer has CLAIMED (CAS won) — the
  // deterministic "deadline fired" signal for tests (the action itself may
  // still sit queued behind busy workers).
  std::uint64_t DebugClaimedCount() const;

  static constexpr std::size_t kExecutorThreads = 4;
  static constexpr std::size_t kExecutorQueueCap = 64;
  static constexpr std::size_t kMaxPendingEntries = 256;
  static constexpr std::chrono::milliseconds kRetryDelay{10};

  RequestWatchdog(const RequestWatchdog&) = delete;
  RequestWatchdog& operator=(const RequestWatchdog&) = delete;

 private:
  struct State;
  void EnsureWorkersLocked();
  static void TimerProc(std::shared_ptr<State> st);
  static void ExecutorProc(std::shared_ptr<State> st);

  struct State {
    struct Entry {
      std::chrono::steady_clock::time_point deadline;
      std::uint64_t seq = 0;
      std::shared_ptr<std::atomic_bool> claimed;
      std::function<void()> action;
      // Owned entries already won the claim CAS (or are timer re-arms);
      // delivery skips the CAS and retries until queue capacity frees.
      bool owned = false;
    };
    struct Cmp {
      bool operator()(const Entry& a, const Entry& b) const {
        if (a.deadline != b.deadline) return a.deadline > b.deadline;
        return a.seq > b.seq;
      }
    };

    std::mutex heapMu;
    std::condition_variable heapCv;
    std::priority_queue<Entry, std::vector<Entry>, Cmp> heap;
    std::uint64_t nextSeq = 1;
    bool timerStop = false;

    std::mutex execMu;
    std::condition_variable execCv;
    std::deque<std::function<void()>> queue;
    bool execStop = false;

    // Accepted-but-not-completed actions: incremented on Arm admission,
    // decremented when a worker finishes an action. Bounded by
    // kMaxPendingEntries — the whole backlog (heap + queue + running) has a
    // hard ceiling, unlike the first draft where the retry heap grew
    // without bound.
    std::atomic<std::size_t> pending{0};

    // Deadline entries the timer claimed (fresh CAS wins; owned retries
    // were counted when first claimed).
    std::atomic<std::uint64_t> claimedCount{0};
  };

  std::shared_ptr<State> state_ = std::make_shared<State>();
  std::thread timer_;
  std::vector<std::thread> executors_;
  bool workersStarted_ = false;
};

}  // namespace echo::async
