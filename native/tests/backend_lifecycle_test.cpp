#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "echo/async/RequestScheduler.h"
#include "echo/async/RequestWatchdog.h"
#include "echo/core/CompatRequestContext.h"
#include "echo/core/RequestErrorMapping.h"
#include "echo/storage/Database.h"
#include "echo/storage/SessionRepository.h"

using namespace std::chrono_literals;
using echo::async::CancellationToken;
using echo::async::RequestKind;
using echo::async::RequestScheduler;
using echo::async::RequestWatchdog;

namespace {
int failures = 0;
void Check(bool ok, const char* message) {
  std::cout << (ok ? "[ok] " : "[FAIL] ") << message << '\n';
  if (!ok) ++failures;
}

void TestShutdownCancellation() {
  auto scheduler = std::make_unique<RequestScheduler>(2);
  struct Running {
    std::promise<void> started;
  };
  auto plain = std::make_shared<Running>();
  auto timed = std::make_shared<Running>();
  const auto work = [](std::shared_ptr<Running> state, CancellationToken token) {
    state->started.set_value();
    const auto until = std::chrono::steady_clock::now() + 1500ms;
    while (!token.IsCancellationRequested() && std::chrono::steady_clock::now() < until)
      std::this_thread::sleep_for(1ms);
    return token.IsCancellationRequested();
  };
  auto a = scheduler->Submit(RequestKind::Generic, [plain, work](auto token) {
    return work(plain, token);
  });
  auto b = scheduler->SubmitWithDeadline(RequestKind::Search, [timed, work](auto token) {
    return work(timed, token);
  }, 10000);
  plain->started.get_future().wait();
  timed->started.get_future().wait();
  const auto begin = std::chrono::steady_clock::now();
  const auto abandoned = scheduler->Shutdown(500ms);
  const auto elapsed = std::chrono::steady_clock::now() - begin;
  std::cout << "cooperative_shutdown_ms=" <<
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << '\n';
  Check(abandoned == 0, "bounded shutdown cancels ordinary and deadline jobs without abandonment");
  const bool plainCancelled = a.get(), timedCancelled = b.get();
  Check(plainCancelled && timedCancelled, "both active jobs observe their cancellation token");
  Check(elapsed < 400ms, "cooperative shutdown completes without spending its wait budget");
  // Keep the old implementation safe during the RED reproduction: its
  // abandoned workers still reference the scheduler after their fn returns.
  if (abandoned != 0) (void)scheduler.release();
}

void TestQueuedRelease() {
  auto scheduler = std::make_unique<RequestScheduler>(1);
  auto entered = std::make_shared<std::promise<void>>();
  auto release = std::make_shared<std::promise<void>>();
  auto gate = release->get_future().share();
  auto blocker = scheduler->Submit(RequestKind::Generic, [entered, gate](auto) {
    entered->set_value();
    gate.wait();
  });
  entered->get_future().wait();
  auto payload = std::make_shared<std::vector<char>>(1024 * 1024);
  std::weak_ptr<std::vector<char>> retained = payload;
  auto queued = scheduler->SubmitWithDeadline(RequestKind::Search, [payload](auto) {
    return payload->size();
  }, 10000);
  auto queuedVoid = scheduler->Submit(RequestKind::Generic, [payload](auto) {});
  auto reference = std::make_shared<int>(42);
  auto queuedRef = scheduler->SubmitLatest(RequestKind::Playlist,
      [payload, reference](auto) -> int& { return *reference; });
  auto detachedRuns = std::make_shared<std::atomic_int>(0);
  scheduler->SubmitDetached(RequestKind::Generic, [payload, detachedRuns](auto) { ++*detachedRuns; });
  payload.reset();
  const auto abandoned = scheduler->Shutdown(30ms);
  Check(abandoned == 1, "uncooperative active work still reports abandonment");
  Check(scheduler->Shutdown(30ms) == 1, "repeated shutdown retains the abandonment result");
  Check(!scheduler->Restart(), "an abandoned generation cannot be restarted");
  Check(retained.expired(), "shutdown promptly releases queued request payloads");
  bool rejected = false;
  if (queued.wait_for(0ms) == std::future_status::ready) {
    try { (void)queued.get(); }
    catch (const std::runtime_error& e) { rejected = std::string(e.what()) == "scheduler_shutdown"; }
  }
  Check(rejected, "a queued deadline job receives a ready shutdown error");
  const auto shutdownError = [](auto& future) {
    if (future.wait_for(0ms) != std::future_status::ready) return false;
    try { (void)future.get(); }
    catch (const std::runtime_error& e) { return std::string(e.what()) == "scheduler_shutdown"; }
    return false;
  };
  Check(shutdownError(queuedVoid) && shutdownError(queuedRef),
        "queued void and reference futures receive shutdown errors");
  Check(detachedRuns->load() == 0, "rejected detached work does not execute");
  scheduler.reset();  // The worker must finish safely after its owner is destroyed.
  release->set_value();
  blocker.wait();
}

void TestExpiredQueuedRelease() {
  RequestScheduler scheduler(1);
  std::promise<void> entered, release;
  auto gate = release.get_future().share();
  auto blocker = scheduler.Submit(RequestKind::Generic, [&entered, gate](auto) {
    entered.set_value();
    gate.wait();
  });
  entered.get_future().wait();

  struct Payload {
    std::vector<char> bytes = std::vector<char>(4 * 1024 * 1024);
    std::function<void()> onRelease;
    ~Payload() { if (onRelease) onRelease(); }
  };
  std::vector<std::weak_ptr<Payload>> retained;
  std::vector<std::future<std::size_t>> requests;
  auto destroyed = std::make_shared<std::promise<void>>();
  auto destruction = destroyed->get_future();
  std::atomic_int runs{0};
  for (int i = 0; i < 4; ++i) {
    auto payload = std::make_shared<Payload>();
    if (i == 0) payload->onRelease = [&scheduler, destroyed] {
      // Destructors may submit more work: reclamation must happen outside the queue lock.
      scheduler.SubmitDetached(RequestKind::Generic, [](auto) {});
      destroyed->set_value();
    };
    retained.push_back(payload);
    requests.push_back(scheduler.SubmitWithDeadline(RequestKind::Search,
        [payload, &runs](auto) { ++runs; return payload->bytes.size(); }, 80));
  }
  bool deadlines = true;
  for (auto& request : requests) {
    if (request.wait_for(2s) != std::future_status::ready) { deadlines = false; continue; }
    try { (void)request.get(); deadlines = false; }
    catch (const std::runtime_error& e) { deadlines &= std::string(e.what()) == "job_deadline"; }
  }
  Check(deadlines, "expired queued requests receive the deadline error while the worker is blocked");
  const bool released = destruction.wait_for(500ms) == std::future_status::ready;
  const auto cleanupDeadline = std::chrono::steady_clock::now() + 500ms;
  while (std::any_of(retained.begin(), retained.end(), [](const auto& weak) { return !weak.expired(); }) &&
         std::chrono::steady_clock::now() < cleanupDeadline) std::this_thread::sleep_for(1ms);
  std::size_t retainedBytes = 0;
  for (const auto& weak : retained) if (const auto payload = weak.lock()) retainedBytes += payload->bytes.size();
  std::cout << "expired_queued_retained_bytes=" << retainedBytes << '\n';
  Check(released && retainedBytes == 0, "deadline promptly releases all queued captures and supports destructor reentry");

  auto replacement = scheduler.Submit(RequestKind::Generic, [](auto) { return 42; });
  const bool admitted = replacement.wait_for(0ms) != std::future_status::ready;
  Check(admitted, "expired jobs return queue capacity before the blocked worker finishes");
  release.set_value();
  blocker.get();
  if (admitted) Check(replacement.get() == 42, "replacement completes once the worker resumes");
  else { try { (void)replacement.get(); } catch (...) {} }
  scheduler.Shutdown();
  Check(runs.load() == 0, "retired queued requests never run their business callable");
}

struct CountedAction {
  std::shared_ptr<std::atomic_size_t> copies;
  explicit CountedAction(std::shared_ptr<std::atomic_size_t> count) : copies(std::move(count)) {}
  CountedAction(const CountedAction& other) : copies(other.copies) { ++*copies; }
  CountedAction(CountedAction&&) noexcept = default;
  void operator()() const {}
};

void TestWatchdogCopies() {
  RequestWatchdog watchdog;
  auto copies = std::make_shared<std::atomic_size_t>(0);
  std::vector<std::shared_ptr<std::atomic_bool>> flags;
  bool accepted = true;
  for (int i = 0; i < 64; ++i) {
    auto flag = std::make_shared<std::atomic_bool>(false);
    accepted &= watchdog.Arm(30000, flag, CountedAction(copies));
    flags.push_back(std::move(flag));
  }
  Check(accepted, "watchdog accepts all long deadlines");
  copies->store(0);
  watchdog.Cancel(flags.front());
  std::cout << "cancel_callback_copies=" << copies->load() << '\n';
  Check(copies->load() == 0, "cancelling one deadline does not copy any callback captures");
  for (const auto& flag : flags) watchdog.Cancel(flag);
  Check(watchdog.DebugPendingActions() == 0, "watchdog cancellation releases all admission slots");
}

void TestWatchdogLifetime() {
  auto watchdog = std::make_unique<RequestWatchdog>();
  auto entered = std::make_shared<std::promise<void>>();
  auto release = std::make_shared<std::promise<void>>();
  auto gate = release->get_future().share();
  Check(watchdog->Arm(1, std::make_shared<std::atomic_bool>(false), [entered, gate] {
    entered->set_value();
    gate.wait();
  }), "watchdog accepts a blocked executor action");
  entered->get_future().wait();
  auto payload = std::make_shared<std::vector<char>>(1024 * 1024);
  std::weak_ptr<std::vector<char>> retained = payload;
  Check(watchdog->Arm(30000, std::make_shared<std::atomic_bool>(false), [payload] {}),
        "watchdog accepts a queued payload");
  payload.reset();
  watchdog.reset();
  Check(retained.expired(), "watchdog destruction drops future captures despite a blocked executor");
  release->set_value();

  RequestWatchdog live;
  auto done = std::make_shared<std::promise<void>>();
  Check(live.Arm(1, std::make_shared<std::atomic_bool>(false), [] {
    throw std::runtime_error("test_deadline_action_failure");
  }), "watchdog accepts a throwing action");
  Check(live.Arm(5, std::make_shared<std::atomic_bool>(false), [done] { done->set_value(); }),
        "watchdog accepts work after a throwing action");
  Check(done->get_future().wait_for(2s) == std::future_status::ready,
        "a throwing action leaves executors alive");
  const auto until = std::chrono::steady_clock::now() + 2s;
  while (live.DebugPendingActions() && std::chrono::steady_clock::now() < until)
    std::this_thread::yield();
  Check(live.DebugPendingActions() == 0, "throwing actions also release admission slots");

  auto repeated = std::make_shared<std::atomic_bool>(false);
  live.Arm(30000, repeated, [] {});
  live.Arm(30000, repeated, [] {});
  live.Cancel(repeated);
  Check(live.DebugPendingActions() == 1, "duplicate claim flags are indexed separately");
  live.Cancel(repeated);
  Check(live.DebugPendingActions() == 0, "duplicate cancellation releases exactly one slot each time");

  struct CancelOnRelease {
    RequestWatchdog* watchdog = nullptr;
    std::shared_ptr<std::atomic_bool> flag;
    std::shared_ptr<std::atomic_bool> destroyed;
    ~CancelOnRelease() {
      if (watchdog) {
        watchdog->Cancel(flag);
        destroyed->store(true);
      }
    }
  };
  auto nestedFlag = std::make_shared<std::atomic_bool>(false);
  auto outerFlag = std::make_shared<std::atomic_bool>(false);
  auto destroyed = std::make_shared<std::atomic_bool>(false);
  auto capture = std::make_shared<CancelOnRelease>();
  capture->watchdog = &live;
  capture->flag = nestedFlag;
  capture->destroyed = destroyed;
  live.Arm(30000, nestedFlag, [] {});
  live.Arm(30000, outerFlag, [capture] {});
  capture.reset();
  live.Cancel(outerFlag);
  Check(destroyed->load() && live.DebugPendingActions() == 0,
        "capture destruction can cancel another deadline without deadlocking");

  struct InspectQueueOnRelease {
    RequestWatchdog* watchdog = nullptr;
    std::shared_ptr<std::promise<void>> destroyed;
    ~InspectQueueOnRelease() {
      if (watchdog) {
        (void)watchdog->DebugQueuedActions();
        destroyed->set_value();
      }
    }
  };
  auto released = std::make_shared<std::promise<void>>();
  auto executorCapture = std::make_shared<InspectQueueOnRelease>();
  executorCapture->watchdog = &live;
  executorCapture->destroyed = released;
  std::promise<void> finishAction;
  auto finishGate = finishAction.get_future().share();
  live.Arm(1, std::make_shared<std::atomic_bool>(false), [executorCapture, finishGate] {
    finishGate.wait();
  });
  executorCapture.reset();
  finishAction.set_value();
  Check(released->get_future().wait_for(2s) == std::future_status::ready,
        "executor completion destroys captures outside the executor mutex");
}

void TestAbsentSessionSnapshot() {
  const auto path = std::filesystem::temp_directory_path() /
      ("echo-backend-lifecycle-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
  {
    echo::storage::Database db;
    db.Open(path);
    db.Initialize();
    echo::core::CompatRequestContext request(db);
    Check(!request.Session().has_value(), "request initially observes an absent session");
    const auto generation = request.SessionGeneration();
    echo::storage::SessionRepository repository(db);
    echo::core::SessionInfo session;
    session.userId = "offline-test-user";
    session.token = "offline-test-token";
    repository.Save(session);
    Check(!request.HasLogin() && request.TokenOrEmpty().empty(),
          "an absent session is cached for the complete request");
    Check(request.SessionGeneration() == generation,
          "session absence and account generation form one stable snapshot");
    echo::core::CompatRequestContext next(db);
    Check(next.HasLogin(), "the next request sees the newly saved session");
    Check(!request.PatchProfile({"late-profile", ""}),
          "a stale request cannot adopt a newer login through a profile patch");
    Check(!request.HasLogin(), "the ended snapshot remains absent after refused patch");
  }
  std::filesystem::remove(path);
  std::filesystem::remove(path.wstring() + L"-wal");
  std::filesystem::remove(path.wstring() + L"-shm");
}

void BenchmarkWatchdog() {
  std::cout << "armed_entries,rounds,arm_cancel_us\n";
  for (int size : {64, 128, 256}) {
    RequestWatchdog watchdog;
    constexpr int rounds = 100;
    const auto begin = std::chrono::steady_clock::now();
    for (int round = 0; round < rounds; ++round) {
      std::vector<std::shared_ptr<std::atomic_bool>> flags;
      flags.reserve(size);
      for (int i = 0; i < size; ++i) {
        auto flag = std::make_shared<std::atomic_bool>(false);
        if (!watchdog.Arm(300000, flag, [] {})) throw std::runtime_error("benchmark_arm_failed");
        flags.push_back(std::move(flag));
      }
      for (const auto& flag : flags) watchdog.Cancel(flag);
    }
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - begin).count();
    std::cout << size << ',' << rounds << ',' << micros << '\n';
  }
}
}  // namespace

int main(int argc, char** argv) {
  std::cout << std::unitbuf;
  const std::string mode = argc > 1 ? argv[1] : "all";
  if (mode == "benchmark") { BenchmarkWatchdog(); return 0; }
  if (mode == "all" || mode == "shutdown") { TestShutdownCancellation(); TestQueuedRelease(); }
  if (mode == "all" || mode == "expiry") TestExpiredQueuedRelease();
  if (mode == "all" || mode == "watchdog") { TestWatchdogCopies(); TestWatchdogLifetime(); }
  if (mode == "all" || mode == "snapshot") TestAbsentSessionSnapshot();
  const auto shutdown = echo::core::MapCApiRequestError("scheduler_shutdown");
  Check(shutdown.httpStatus == 503 && shutdown.errorCode == "scheduler_shutdown",
        "shutdown rejection is reported as local unavailability at the C ABI boundary");
  return failures == 0 ? 0 : 1;
}
