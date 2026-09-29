// Native C++ request-lifecycle stress measurement (agent B, 2026-09-28).
//
// This test exercises the C++ RequestScheduler / RequestWatchdog and the
// HttpClient::Get + CompatResponse request shape against a gate-controlled
// loopback HTTP server. It measures whether native request functions remain
// active after caller futures resolve, watchdog admission recovery, and the
// lifecycle of this test's local server handlers.
//
// Scope boundary: this is a native C++ scheduler test. It does not exercise
// Rust dispatch_bounded_ffi closures, the Rust backend_api read lock, or
// EchoHandleRequest itself. RequestScheduler's four workers are not a
// bound on Rust closure lifetimes. The burst tests observed native scheduler
// admission and classifies each caller result without relying on a particular
// race between submit, start, and deadline processing. It is not a substitute
// for measuring live production task lifetimes or a real upstream.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>

#include "echo/async/RequestScheduler.h"
#include "echo/async/RequestWatchdog.h"
#include "echo/core/HttpClient.h"
#include "echo/core/CompatApi.h"

using echo::async::RequestScheduler;
using echo::async::RequestKind;
using echo::core::CompatResponse;
using echo::core::HttpClient;
using echo::core::HttpClientCancellationScope;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg) \
  do { \
    if (cond) { \
      std::cout << "  [ok] " << (msg) << "\n"; \
      ++g_passed; \
    } else { \
      std::cerr << "  [FAIL] " << (msg) << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
      ++g_failed; \
    } \
  } while (0)

namespace {

using Clock = std::chrono::steady_clock;

long MsSince(Clock::time_point since) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             Clock::now() - since)
      .count();
}

// ── Bounded local HTTP fixtures ───────────────────────────────────────────

constexpr std::size_t kMaxRequestHeadBytes = 1024;
constexpr long kRequestHeadTimeoutMs = 5000;
constexpr long kSocketPollMs = 20;

const char kHttpResponse[] =
    "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";

class RequestHeadBuffer {
 public:
  bool Append(const char* bytes, std::size_t count) {
    if (count > kMaxRequestHeadBytes - data_.size()) return false;
    data_.append(bytes, count);
    complete_ = data_.find("\r\n\r\n") != std::string::npos;
    return true;
  }

  bool complete() const { return complete_; }
  bool full() const { return data_.size() == kMaxRequestHeadBytes; }
  std::size_t remaining() const { return kMaxRequestHeadBytes - data_.size(); }

 private:
  std::string data_;
  bool complete_ = false;
};

enum class HeaderReadResult { Complete, Disconnected, TooLarge, TimedOut, Stopped };

HeaderReadResult ReadRequestHead(SOCKET client, const std::atomic<bool>& stopping) {
  RequestHeadBuffer head;
  const auto started = Clock::now();
  char buffer[kMaxRequestHeadBytes];

  while (!stopping.load(std::memory_order_acquire)) {
    if (MsSince(started) >= kRequestHeadTimeoutMs) return HeaderReadResult::TimedOut;
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(client, &readable);
    timeval timeout{};
    timeout.tv_usec = static_cast<long>(kSocketPollMs * 1000);
    const int ready = select(0, &readable, nullptr, nullptr, &timeout);
    if (ready == 0) continue;
    if (ready == SOCKET_ERROR) return HeaderReadResult::Disconnected;

    // Once the fixed-capacity buffer is full, no further recv is safe. It is
    // accepted only if its final bytes already contained the header delimiter.
    if (head.remaining() == 0) return HeaderReadResult::TooLarge;
    const int received = recv(client, buffer,
                              static_cast<int>(std::min(head.remaining(), sizeof(buffer))), 0);
    if (received == 0) return HeaderReadResult::Disconnected;
    if (received == SOCKET_ERROR) {
      const int error = WSAGetLastError();
      if (error == WSAEWOULDBLOCK) continue;
      return HeaderReadResult::Disconnected;
    }
    if (!head.Append(buffer, static_cast<std::size_t>(received)))
      return HeaderReadResult::TooLarge;
    if (head.complete()) return HeaderReadResult::Complete;
    if (head.full()) return HeaderReadResult::TooLarge;
  }
  return HeaderReadResult::Stopped;
}

bool SendResponse(SOCKET client, const std::atomic<bool>& stopping) {
  const char* next = kHttpResponse;
  int remaining = static_cast<int>(sizeof(kHttpResponse) - 1);
  while (remaining > 0 && !stopping.load(std::memory_order_acquire)) {
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(client, &writable);
    timeval timeout{};
    timeout.tv_usec = static_cast<long>(kSocketPollMs * 1000);
    const int ready = select(0, nullptr, &writable, nullptr, &timeout);
    if (ready == 0) continue;
    if (ready == SOCKET_ERROR) return false;
    const int sent = send(client, next, remaining, 0);
    if (sent == SOCKET_ERROR) {
      if (WSAGetLastError() == WSAEWOULDBLOCK) continue;
      return false;
    }
    if (sent == 0) return false;
    next += sent;
    remaining -= sent;
  }
  return remaining == 0;
}

SOCKET ListenOnLoopback(int backlog, int* outPort) {
  SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
  if (server == INVALID_SOCKET) return INVALID_SOCKET;
  int opt = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt),
             sizeof(opt));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
      listen(server, backlog) != 0) {
    closesocket(server);
    return INVALID_SOCKET;
  }
  int len = sizeof(addr);
  if (getsockname(server, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
    closesocket(server);
    return INVALID_SOCKET;
  }
  *outPort = ntohs(addr.sin_port);
  u_long nonblocking = 1;
  if (ioctlsocket(server, FIONBIO, &nonblocking) != 0) {
    closesocket(server);
    return INVALID_SOCKET;
  }
  return server;
}

class LocalHttpServer {
 public:
  enum class Mode { Immediate, HoldUntilReleased };

  explicit LocalHttpServer(Mode mode) : mode_(mode) {
    listener_ = ListenOnLoopback(256, &port_);
    if (listener_ == INVALID_SOCKET) {
      std::cerr << "  [FATAL] local HTTP fixture bind/listen failed\n";
      std::exit(2);
    }
    acceptThread_ = std::thread([this] { AcceptLoop(); });
  }

  LocalHttpServer(const LocalHttpServer&) = delete;
  LocalHttpServer& operator=(const LocalHttpServer&) = delete;

  ~LocalHttpServer() { StopAndJoin(); }

  int port() const { return port_; }

  void OpenGate() { gateOpen_.store(true, std::memory_order_release); }

  struct Snapshot {
    int activeHandlers = 0;
    int gateWaiters = 0;
    int responsesSent = 0;
  };

  Snapshot ReadSnapshot() const {
    return {activeHandlers_.load(std::memory_order_acquire),
            gateWaiters_.load(std::memory_order_acquire),
            responsesSent_.load(std::memory_order_acquire)};
  }

  bool WaitForGateWaiters(int target, long timeoutMs) const {
    const auto started = Clock::now();
    while (MsSince(started) < timeoutMs) {
      if (gateWaiters_.load(std::memory_order_acquire) >= target) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return gateWaiters_.load(std::memory_order_acquire) >= target;
  }

  bool WaitForHandlersToExit(long timeoutMs) const {
    const auto started = Clock::now();
    while (MsSince(started) < timeoutMs) {
      if (activeHandlers_.load(std::memory_order_acquire) == 0) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return activeHandlers_.load(std::memory_order_acquire) == 0;
  }

  void StopAndJoin() {
    if (stopped_.exchange(true, std::memory_order_acq_rel)) return;
    stopping_.store(true, std::memory_order_release);
    gateOpen_.store(true, std::memory_order_release);
    if (acceptThread_.joinable()) acceptThread_.join();
    if (listener_ != INVALID_SOCKET) {
      closesocket(listener_);
      listener_ = INVALID_SOCKET;
    }
    for (auto& handler : clientThreads_) {
      if (handler.joinable()) handler.join();
    }
    clientThreads_.clear();
  }

 private:
  void AcceptLoop() {
    while (!stopping_.load(std::memory_order_acquire)) {
      sockaddr_in clientAddress{};
      int addressLength = sizeof(clientAddress);
      const SOCKET client = accept(listener_, reinterpret_cast<sockaddr*>(&clientAddress),
                                   &addressLength);
      if (client == INVALID_SOCKET) {
        const int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) {
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
          continue;
        }
        if (stopping_.load(std::memory_order_acquire)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }
      activeHandlers_.fetch_add(1, std::memory_order_acq_rel);
      try {
        clientThreads_.emplace_back([this, client] { HandleClient(client); });
      } catch (...) {
        activeHandlers_.fetch_sub(1, std::memory_order_acq_rel);
        closesocket(client);
      }
    }
  }

  void HandleClient(SOCKET client) {
    struct HandlerExit {
      LocalHttpServer* server;
      SOCKET client;
      ~HandlerExit() {
        closesocket(client);
        server->activeHandlers_.fetch_sub(1, std::memory_order_acq_rel);
      }
    } exit{this, client};

    u_long nonblocking = 1;
    if (ioctlsocket(client, FIONBIO, &nonblocking) != 0) return;
    if (ReadRequestHead(client, stopping_) != HeaderReadResult::Complete) return;
    if (mode_ == Mode::HoldUntilReleased) {
      gateWaiters_.fetch_add(1, std::memory_order_acq_rel);
      struct GateWaiterExit {
        std::atomic<int>& waiters;
        ~GateWaiterExit() { waiters.fetch_sub(1, std::memory_order_acq_rel); }
      } gateWaiterExit{gateWaiters_};

      while (!stopping_.load(std::memory_order_acquire) &&
             !gateOpen_.load(std::memory_order_acquire)) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(client, &readable);
        timeval timeout{};
        timeout.tv_usec = static_cast<long>(kSocketPollMs * 1000);
        const int ready = select(0, &readable, nullptr, nullptr, &timeout);
        if (ready == 0) continue;
        if (ready == SOCKET_ERROR) return;
        char pending[64];
        const int peeked = recv(client, pending, sizeof(pending), MSG_PEEK);
        if (peeked == 0) return;  // caller closed while its native fn drained
        if (peeked == SOCKET_ERROR) {
          const int error = WSAGetLastError();
          if (error == WSAEWOULDBLOCK) continue;
          return;
        }
        // Ignore unexpected body bytes while waiting; consuming them avoids a
        // readable-socket spin and keeps all receive memory bounded.
        (void)recv(client, pending, sizeof(pending), 0);
      }
    }

    if (!stopping_.load(std::memory_order_acquire) && SendResponse(client, stopping_)) {
      responsesSent_.fetch_add(1, std::memory_order_acq_rel);
    }
  }

  Mode mode_;
  int port_ = 0;
  SOCKET listener_ = INVALID_SOCKET;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> stopped_{false};
  std::atomic<bool> gateOpen_{false};
  std::atomic<int> activeHandlers_{0};
  std::atomic<int> gateWaiters_{0};
  std::atomic<int> responsesSent_{0};
  std::thread acceptThread_;
  std::vector<std::thread> clientThreads_;  // mutated only by acceptThread_
};

bool CheckRequestHeadBufferBoundaries() {
  std::string maxHeader(kMaxRequestHeadBytes, 'x');
  const std::string prefix = "GET / HTTP/1.1\r\nX-Pad: ";
  maxHeader.replace(0, prefix.size(), prefix);
  maxHeader.replace(maxHeader.size() - 4, 4, "\r\n\r\n");
  RequestHeadBuffer exact;
  const bool exactAccepted = exact.Append(maxHeader.data(), maxHeader.size());
  const bool exactCompleted = exact.complete();
  const char extra = 'x';
  const bool overCapacityAccepted = exact.Append(&extra, 1);
  return exactAccepted && exactCompleted && !overCapacityAccepted;
}

// ── Production-shaped native fn ───────────────────────────────────────────

struct BackgroundTracker {
  std::atomic<int> alive{0};
  std::atomic<int> peak{0};
  std::mutex endMu;
  std::vector<long> fnEndsMsSinceBurstStart;

  void RecordEnd(long msSinceBurstStart) {
    std::lock_guard<std::mutex> lock(endMu);
    fnEndsMsSinceBurstStart.push_back(msSinceBurstStart);
  }
};

struct NativeFnExit {
  BackgroundTracker& tracker;
  Clock::time_point burstStart;

  ~NativeFnExit() {
    tracker.RecordEnd(MsSince(burstStart));
    tracker.alive.fetch_sub(1, std::memory_order_acq_rel);
  }
};

// Mirrors the C++ HttpClient request shape: cancellation scope + real
// HttpClient + production CompatResponse return type. This remains a native
// scheduler fixture, not a Rust FFI/closure-lifetime test.
auto MakeRequestFn(std::shared_ptr<BackgroundTracker> tracker, std::string url,
                   long totalTimeoutMs, Clock::time_point burstStart) {
  return [tracker, url, totalTimeoutMs, burstStart](
             echo::async::CancellationToken token) -> CompatResponse {
    const int now = tracker->alive.fetch_add(1, std::memory_order_acq_rel) + 1;
    int previousPeak = tracker->peak.load(std::memory_order_relaxed);
    while (now > previousPeak &&
           !tracker->peak.compare_exchange_weak(previousPeak, now,
                                                std::memory_order_acq_rel)) {
    }
    NativeFnExit finish{*tracker, burstStart};
    CompatResponse response;
    {
      HttpClientCancellationScope scope(token.Flag());
      HttpClient client;
      auto result = client.Get(url, {}, totalTimeoutMs);
      response.httpStatus = result.statusCode;
      if (!result.error.empty()) {
        response.body = {{"error", result.error}};
      } else {
        response.body = result.body;
      }
    }
    return response;
  };
}

struct RoundMetrics {
  int round = 0;
  int submitted = 0;
  int queueFull = 0;
  int jobDeadline = 0;
  int okOutcome = 0;
  int otherOutcome = 0;
  int nativeFnAliveAtCallerResolve = 0;
  int peakNativeFnConcurrent = 0;
  long maxNativeFnSurvivorWindowMs = 0;
  long nativeFnZeroObservationWaitAfterCallerResolveMs = 0;
  bool nativeFnsDrainedWithinObservation = false;
  long admissionRecoveryAfterCallerResolveMs = 0;
  std::size_t watchdogPendingAtCallerResolve = 0;
  std::size_t watchdogPendingAfterFnDrainObservation = 0;
  int serverHandlersAtCallerResolve = 0;
  int gateWaitersAtCallerResolve = 0;
  int serverHandlersAfterNativeFnObservation = 0;
  int gateResponsesAfterNativeFnObservation = 0;
  int serverHandlersAfterStop = -1;
};

void Print(const RoundMetrics& metrics) {
  std::cout << "  [round " << metrics.round << "] submitted=" << metrics.submitted
            << " queue_full=" << metrics.queueFull
            << " job_deadline=" << metrics.jobDeadline
            << " ok=" << metrics.okOutcome
            << " other=" << metrics.otherOutcome << "\n"
            << "           native_fn_alive@caller_resolve="
            << metrics.nativeFnAliveAtCallerResolve
            << " peak_native_fn_concurrent=" << metrics.peakNativeFnConcurrent
            << " max_native_fn_survivor_ms="
            << metrics.maxNativeFnSurvivorWindowMs << "\n"
            << "           native_fn_zero_observation_wait_ms="
            << metrics.nativeFnZeroObservationWaitAfterCallerResolveMs
            << " native_fns_drained="
            << (metrics.nativeFnsDrainedWithinObservation ? "yes" : "no")
            << " admission_recovery_ms="
            << metrics.admissionRecoveryAfterCallerResolveMs << "\n"
            << "           watchdog_pending@caller_resolve="
            << metrics.watchdogPendingAtCallerResolve
            << " @fn_drain_observation="
            << metrics.watchdogPendingAfterFnDrainObservation << "\n"
            << "           server_handlers@caller_resolve="
            << metrics.serverHandlersAtCallerResolve
            << " gate_waiters@caller_resolve=" << metrics.gateWaitersAtCallerResolve
            << " server_handlers@native_fn_observation="
            << metrics.serverHandlersAfterNativeFnObservation
            << " gate_responses@native_fn_observation="
            << metrics.gateResponsesAfterNativeFnObservation
            << " server_handlers@after_stop=" << metrics.serverHandlersAfterStop
            << "\n";
}

}  // namespace

int main() {
  std::cout << std::unitbuf;
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::cerr << "[FATAL] WSAStartup failed\n";
    return 2;
  }

  CHECK(CheckRequestHeadBufferBoundaries(),
        "request-head buffer accepts a complete 1024-byte header and rejects overflow");

  LocalHttpServer okServer(LocalHttpServer::Mode::Immediate);
  const std::string okUrl =
      "http://127.0.0.1:" + std::to_string(okServer.port()) + "/ok";

  // These counts describe only RequestScheduler's native C++ worker/queue
  // behavior; they do not describe a Rust FFI closure cap.
  constexpr int kWorkerCount = 4;
  constexpr int kRounds = 5;
  constexpr int kPerRound = 30;
  constexpr long kDeadlineMs = 1500;
  constexpr long kFnBudgetMs = 6000;
  constexpr long kFnDrainObservationMs = 20000;

  RequestScheduler scheduler{kWorkerCount};
  echo::async::RequestWatchdog& watchdog =
      echo::async::RequestWatchdog::Instance();

  for (int i = 0; i < 500 && watchdog.DebugPendingActions() != 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  int maxObservedNativeFnSurvivors = 0;

  for (int round = 1; round <= kRounds + 1; ++round) {
    const bool gateRound = (round == kRounds + 1);
    LocalHttpServer slowServer(LocalHttpServer::Mode::HoldUntilReleased);
    const std::string slowUrl =
        "http://127.0.0.1:" + std::to_string(slowServer.port()) + "/slow";
    std::cout << "[Test] native C++ lifecycle round " << round
              << (gateRound ? " (gate released after caller resolution)\n"
                            : " (gate stays closed until native requests cancel)\n");

    auto tracker = std::make_shared<BackgroundTracker>();
    RoundMetrics metrics;
    metrics.round = round;
    int callerResolved = 0;
    Clock::time_point allResolvedAt{};
    const auto burstStart = Clock::now();
    std::vector<std::future<CompatResponse>> futures;
    futures.reserve(kPerRound);

    // Start one native request per worker and wait until the local server has
    // observed all four blocked requests. The rest of the burst then measures
    // normal queue/deadline races without assuming their exact classification.
    for (int i = 0; i < kWorkerCount; ++i) {
      futures.push_back(scheduler.SubmitWithDeadline(
          RequestKind::SongUrl,
          MakeRequestFn(tracker, slowUrl, kFnBudgetMs, burstStart),
          kDeadlineMs));
    }
    const bool workersReachedGate =
        slowServer.WaitForGateWaiters(kWorkerCount, 3000);
    CHECK(workersReachedGate,
          "round " + std::to_string(round) +
              ": all native worker requests reached the local gate before burst remainder");

    for (int i = kWorkerCount; i < kPerRound; ++i) {
      futures.push_back(scheduler.SubmitWithDeadline(
          RequestKind::SongUrl,
          MakeRequestFn(tracker, slowUrl, kFnBudgetMs, burstStart),
          kDeadlineMs));
    }
    metrics.submitted = static_cast<int>(futures.size());

    for (auto& future : futures) {
      try {
        auto response = future.get();
        if (response.httpStatus == 200) ++metrics.okOutcome;
        else ++metrics.otherOutcome;
      } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        if (message == "job_deadline") ++metrics.jobDeadline;
        else if (message == "queue_full") ++metrics.queueFull;
        else ++metrics.otherOutcome;
      } catch (...) {
        ++metrics.otherOutcome;
      }
      if (++callerResolved == metrics.submitted) {
        allResolvedAt = Clock::now();
      }
    }

    metrics.nativeFnAliveAtCallerResolve = tracker->alive.load();
    metrics.peakNativeFnConcurrent = tracker->peak.load();
    metrics.watchdogPendingAtCallerResolve = watchdog.DebugPendingActions();
    const auto serverAtCallerResolve = slowServer.ReadSnapshot();
    metrics.serverHandlersAtCallerResolve = serverAtCallerResolve.activeHandlers;
    metrics.gateWaitersAtCallerResolve = serverAtCallerResolve.gateWaiters;
    maxObservedNativeFnSurvivors =
        std::max(maxObservedNativeFnSurvivors,
                 metrics.nativeFnAliveAtCallerResolve);

    // Capture caller-timeout survivors before allowing the upstream gate to
    // release; no server cleanup is inferred from this native-fn counter.
    if (gateRound) slowServer.OpenGate();

    bool admitted = false;
    int recoveryQueueFull = 0;
    int recoveryDeadline = 0;
    int recoveryHttpFail = 0;
    int recoveryOther = 0;
    const auto admissionStart = Clock::now();
    while (MsSince(admissionStart) < 15000) {
      auto fast = scheduler.SubmitWithDeadline(
          RequestKind::Generic,
          MakeRequestFn(std::make_shared<BackgroundTracker>(), okUrl, 3000,
                        burstStart),
          5000);
      try {
        auto response = fast.get();
        if (response.httpStatus == 200) {
          admitted = true;
          break;
        }
        ++recoveryHttpFail;
      } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        if (message == "queue_full") ++recoveryQueueFull;
        else if (message == "job_deadline") ++recoveryDeadline;
        else ++recoveryOther;
      } catch (...) {
        ++recoveryOther;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    metrics.admissionRecoveryAfterCallerResolveMs = MsSince(allResolvedAt);
    std::cout << "  [admission probe] queue_full=" << recoveryQueueFull
              << " job_deadline=" << recoveryDeadline
              << " http_fail=" << recoveryHttpFail
              << " other=" << recoveryOther << "\n";

    // The successful fast probe was enqueued after this round's burst. Wait
    // for the native function counter only after that queue-progress fence,
    // so a transient zero between worker jobs is not reported as a drain.
    const auto drainStarted = Clock::now();
    while (tracker->alive.load(std::memory_order_acquire) != 0 &&
           MsSince(drainStarted) < kFnDrainObservationMs) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    metrics.nativeFnZeroObservationWaitAfterCallerResolveMs = MsSince(allResolvedAt);
    metrics.nativeFnsDrainedWithinObservation =
        admitted && tracker->alive.load(std::memory_order_acquire) == 0;

    if (metrics.nativeFnsDrainedWithinObservation) {
      const long callerResolveMs =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              allResolvedAt - burstStart)
              .count();
      long maxEndMs = 0;
      {
        std::lock_guard<std::mutex> lock(tracker->endMu);
        for (long endMs : tracker->fnEndsMsSinceBurstStart)
          maxEndMs = std::max(maxEndMs, endMs);
      }
      metrics.maxNativeFnSurvivorWindowMs =
          std::max(0L, maxEndMs - callerResolveMs);
    }

    // Give socket handlers their own bounded observation and then stop/join
    // them explicitly. The two lifecycle measurements remain separate.
    (void)slowServer.WaitForHandlersToExit(2000);
    const auto serverAfterFnObservation = slowServer.ReadSnapshot();
    metrics.serverHandlersAfterNativeFnObservation =
        serverAfterFnObservation.activeHandlers;
    metrics.gateResponsesAfterNativeFnObservation =
        serverAfterFnObservation.responsesSent;
    metrics.watchdogPendingAfterFnDrainObservation = watchdog.DebugPendingActions();

    slowServer.StopAndJoin();
    metrics.serverHandlersAfterStop = slowServer.ReadSnapshot().activeHandlers;
    Print(metrics);

    const int classified = metrics.queueFull + metrics.jobDeadline +
                           metrics.okOutcome + metrics.otherOutcome;
    CHECK(classified == metrics.submitted,
          "round " + std::to_string(round) +
              ": every submitted caller future has exactly one outcome classification");
    CHECK(metrics.otherOutcome == 0,
          "round " + std::to_string(round) +
              ": no caller future ended with an unexpected error classification");
    CHECK(metrics.okOutcome == 0,
          "round " + std::to_string(round) +
              ": the closed gate did not produce a caller-visible successful response");
    CHECK(metrics.jobDeadline > 0,
          "round " + std::to_string(round) +
              ": at least one caller deadline fired while the gate remained closed");
    CHECK(metrics.queueFull > 0,
          "round " + std::to_string(round) +
              ": the burst exercised native queue rejection without fixing an exact split");
    CHECK(metrics.peakNativeFnConcurrent <= kWorkerCount,
          "round " + std::to_string(round) +
              ": native C++ request-function concurrency stays within scheduler workers");
    CHECK(metrics.nativeFnAliveAtCallerResolve <= kWorkerCount,
          "round " + std::to_string(round) +
              ": caller-timeout native C++ request functions stay within worker concurrency");
    CHECK(metrics.nativeFnsDrainedWithinObservation,
          "round " + std::to_string(round) +
              ": native C++ request functions drained within bounded observation");
    CHECK(metrics.watchdogPendingAfterFnDrainObservation == 0,
          "round " + std::to_string(round) +
              ": watchdog admission actions return to zero after native-fn drain observation");
    CHECK(admitted,
          "round " + std::to_string(round) +
              ": a subsequent fast native request was admitted and completed");
    CHECK(metrics.admissionRecoveryAfterCallerResolveMs < 15000,
          "round " + std::to_string(round) +
              ": fast-request recovery completed within the observation window");
    CHECK(metrics.serverHandlersAfterStop == 0,
          "round " + std::to_string(round) +
              ": local server client handlers are zero after explicit stop and join");
    CHECK(metrics.serverHandlersAfterNativeFnObservation == 0,
          "round " + std::to_string(round) +
              ": local server handlers exit during the bounded observation before fixture stop");
  }

  CHECK(maxObservedNativeFnSurvivors <= kWorkerCount,
        "native C++ headline: caller-timeout survivors never exceeded native scheduler workers");

  int successfulBaselineRequests = 0;
  for (int i = 0; i < 10; ++i) {
    auto future = scheduler.SubmitWithDeadline(
        RequestKind::Generic,
        MakeRequestFn(std::make_shared<BackgroundTracker>(), okUrl, 3000,
                      Clock::now()),
        5000);
    try {
      if (future.get().httpStatus == 200) ++successfulBaselineRequests;
    } catch (...) {
    }
  }
  CHECK(successfulBaselineRequests == 10,
        "baseline: 10 sequential fast local requests succeed after stress rounds");

  okServer.StopAndJoin();
  CHECK(okServer.ReadSnapshot().activeHandlers == 0,
        "fast local server handlers are zero after explicit stop and join");

  scheduler.Shutdown();
  std::cout << "[Test] native C++ lifecycle measurements completed.\n";
  std::cout << "  Passed: " << g_passed << "  Failed: " << g_failed << "\n";
  WSACleanup();
  return g_failed == 0 ? 0 : 1;
}
