// Backend development regressions. All traffic is confined to 127.0.0.1.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include "echo/core/TestSocketsPosix.h"
#endif

#include "echo/core/HttpClient.h"
#include "echo/async/RequestWatchdog.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using echo::core::HttpClient;
using echo::core::HttpResult;

enum class Reply { Ok, SlowOk, Slow4sOk, Redirect, HoldHeaders, HoldBody, Truncated, NotFound };

class LoopbackServer {
 public:
  explicit LoopbackServer(std::vector<Reply> script) : script_(std::move(script)) {
    listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (listener_ == INVALID_SOCKET || bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ||
        listen(listener_, SOMAXCONN)) throw std::runtime_error("loopback listen failed");
    int len = sizeof(addr);
    getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &len);
    url_ = "http://127.0.0.1:" + std::to_string(ntohs(addr.sin_port)) + "/probe";
    acceptor_ = std::thread([this] {
      while (!stop_.load()) {
        fd_set reads;
        FD_ZERO(&reads);
        FD_SET(listener_, &reads);
        timeval timeout{0, 20000};
        if (select(0, &reads, nullptr, nullptr, &timeout) <= 0) continue;
        auto sock = accept(listener_, nullptr, nullptr);
        if (sock == INVALID_SOCKET) continue;
        const auto index = accepts_.fetch_add(1);
        const auto reply = index < script_.size() ? script_[index] : Reply::Ok;
        workers_.emplace_back([this, sock, reply] { Serve(sock, reply); });
      }
    });
  }

  ~LoopbackServer() {
    stop_.store(true);
    acceptor_.join();
    closesocket(listener_);
    for (auto& worker : workers_) worker.join();
  }

  const std::string& Url() const { return url_; }
  std::size_t Requests() const { return requests_.load(); }
  std::vector<std::string> RecordedRequests() const {
    std::lock_guard lock(mutex_);
    return recorded_;
  }

 private:
  void Serve(SOCKET sock, Reply reply) {
    DWORD receiveTimeout = 300;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&receiveTimeout), sizeof(receiveTimeout));
    std::string request;
    char buffer[4096];
    while (!stop_.load()) {
      const auto read = recv(sock, buffer, sizeof(buffer), 0);
      if (read <= 0) break;
      request.append(buffer, read);
      const auto headerEnd = request.find("\r\n\r\n");
      if (headerEnd == std::string::npos) continue;
      std::size_t bodyLen = 0;
      const auto length = request.find("Content-Length:");
      if (length != std::string::npos && length < headerEnd) {
        bodyLen = std::stoul(request.substr(length + 15));
      }
      if (request.size() >= headerEnd + 4 + bodyLen) break;
    }
    if (request.find("\r\n\r\n") != std::string::npos) {
      ++requests_;
      std::lock_guard lock(mutex_);
      recorded_.push_back(request);
    }
    if (reply == Reply::HoldHeaders || reply == Reply::HoldBody) {
      if (reply == Reply::HoldBody) {
        const std::string headers = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n";
        send(sock, headers.data(), static_cast<int>(headers.size()), 0);
      }
      while (!stop_.load()) std::this_thread::sleep_for(5ms);
    } else {
      if (reply == Reply::SlowOk || reply == Reply::Slow4sOk) {
        const auto until = std::chrono::steady_clock::now() +
            (reply == Reply::Slow4sOk ? 4000ms : 1500ms);
        while (!stop_.load() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(5ms);
      }
      const std::string response = reply == Reply::Truncated
          ? "HTTP/1.1 200 OK\r\nContent-Length: 10\r\nConnection: close\r\n\r\nx"
          : reply == Reply::Redirect
              ? "HTTP/1.1 302 Found\r\nLocation: " + url_ + "?secret=synthetic-secret\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
          : reply == Reply::NotFound
              ? "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
              : "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";
      send(sock, response.data(), static_cast<int>(response.size()), 0);
    }
    shutdown(sock, SD_BOTH);
    closesocket(sock);
  }

  SOCKET listener_ = INVALID_SOCKET;
  std::string url_;
  std::vector<Reply> script_;
  std::atomic_bool stop_{false};
  std::atomic<std::size_t> accepts_{0}, requests_{0};
  std::thread acceptor_;
  std::vector<std::thread> workers_;
  mutable std::mutex mutex_;
  std::vector<std::string> recorded_;
};

int failures = 0;
void Check(bool ok, const char* name) {
  std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
  if (!ok) ++failures;
}
long Elapsed(std::chrono::steady_clock::time_point started) {
  return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count());
}

int main() {
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data)) return 2;
  HttpClient client;
  {
    LoopbackServer server({Reply::Truncated, Reply::Ok});
    const auto result = client.Get(server.Url(), {}, 4000, 1024, nullptr, false);
    Check(!result.error.empty() && result.attemptCount == 1 && server.Requests() == 1,
          "mutating GET policy never replays a remote side effect after an incomplete response");
  }
  {
    LoopbackServer server({Reply::SlowOk});
    const auto started = std::chrono::steady_clock::now();
    const auto result = client.GetInteractive(server.Url(), {}, 4000);
    Check(result.statusCode == 200 && result.body == "ok" && result.attemptCount == 1,
          "1500 ms normal response succeeds within 4000 ms without premature retry");
    Check(Elapsed(started) >= 1400 && Elapsed(started) < 2300, "slow success keeps useful first-attempt budget");
  }
  {
    // Each of the first two responses is slower than its allocated slice, but
    // the final attempt still has enough shared budget to recover immediately.
    // This pins the interaction between SingleAttemptCap and retry admission.
    LoopbackServer server({Reply::Slow4sOk, Reply::Slow4sOk, Reply::Ok});
    const auto started = std::chrono::steady_clock::now();
    const auto result = client.Get(server.Url(), {}, 9000);
    std::cout << "MEASURE slow-sliced-recovery elapsed_ms=" << Elapsed(started)
              << " attempts=" << result.attemptCount << " requests=" << server.Requests() << '\n';
    Check(result.statusCode == 200 && result.body == "ok" &&
              result.attemptCount == 3 && server.Requests() == 3,
          "a 9000 ms GET budget reaches its viable third slice after two slow responses");
    Check(Elapsed(started) < 9000, "slow sliced retries remain inside the shared GET budget");
  }
  {
    LoopbackServer server({Reply::Redirect, Reply::Ok});
    const auto result = client.Post(server.Url(), "synthetic-credential", {}, 1500);
    Check(result.statusCode == 302 && server.Requests() == 1,
          "POST redirect is returned without replaying query/body credentials");
  }
  {
    LoopbackServer server({Reply::HoldHeaders});
    std::atomic_bool cancelled{false};
    std::thread abort([&] { std::this_thread::sleep_for(100ms); cancelled.store(true); });
    const auto started = std::chrono::steady_clock::now();
    const auto result = client.Get(server.Url(), {}, 4000, 1024, &cancelled);
    abort.join();
    Check(result.error == "cancelled" && result.attemptCount == 1 && Elapsed(started) < 600,
          "ordinary GET cancellation releases the worker promptly");
  }
  {
    LoopbackServer server({Reply::HoldHeaders});
    echo::core::HttpClientCancellationScope route(nullptr, 350);
    const auto started = std::chrono::steady_clock::now();
    const auto result = client.Post(server.Url(), "{}", {}, 9000);
    Check(!result.error.empty() && result.elapsedMs > 0 && Elapsed(started) < 700,
          "serial HTTP step consumes route remaining budget instead of a fresh 9000 ms");
  }
  {
    LoopbackServer server({Reply::Truncated, Reply::Ok});
    const auto started = std::chrono::steady_clock::now();
    const auto result = client.Get(server.Url(), {}, 4000, 1024);
    std::cout << "MEASURE retry-after-first-failure elapsed_ms=" << Elapsed(started)
              << " attempts=" << result.attemptCount << " requests=" << server.Requests() << '\n';
    Check(result.statusCode == 200 && server.Requests() >= 2,
          "a failed first attempt is retried and stays inside the shared budget");
    Check(Elapsed(started) < 3900, "the retried GET still finishes inside its budget");
  }
  {
    LoopbackServer server({Reply::HoldHeaders});
    echo::core::HttpClientCancellationScope route(nullptr, 300);
    std::atomic_bool notCancelled{false};
    const auto result = client.Get(server.Url(), {}, 9000, 1024, &notCancelled);
    if (result.error != "total_budget_exhausted" || !result.timedOut) {
      std::cout << "MEASURE route-deadline error=" << result.error
                << " timed_out=" << result.timedOut
                << " elapsed_ms=" << result.elapsedMs << '\n';
    }
    Check(result.error == "total_budget_exhausted" && result.timedOut,
          "a spent route budget is reported as a deadline, not as a user cancellation");
  }
  {
    LoopbackServer server({Reply::Ok});
    echo::core::HttpClientCancellationScope route(nullptr, 300);
    std::atomic_bool cancelled{true};
    const auto result = client.Get(server.Url(), {}, 9000, 1024, &cancelled);
    Check(result.error == "cancelled" && server.Requests() == 0,
          "an explicit caller cancel still wins over the route deadline");
  }
  {
    LoopbackServer server({Reply::HoldHeaders, Reply::HoldHeaders, Reply::Ok});
    const auto started = std::chrono::steady_clock::now();
    const auto result = client.GetInteractive(server.Url(), {{"X-Probe", "same-headers"}}, 2500);
    std::cout << "MEASURE header-recovery elapsed_ms=" << Elapsed(started)
              << " attempts=" << result.attemptCount << " requests=" << server.Requests() << '\n';
    Check(result.error.empty() && result.body == "ok" && server.Requests() == 3 && result.attemptCount == 3,
          "two header stalls recover inside the original request");
    Check(Elapsed(started) < 2900, "all GET attempts share 2500 ms");
    const auto requests = server.RecordedRequests();
    bool same = requests.size() == 3;
    for (const auto& request : requests) same &= request.find("X-Probe: same-headers") != std::string::npos;
    Check(same, "retry keeps the same headers and query");
  }
  {
    LoopbackServer server({Reply::HoldHeaders, Reply::HoldHeaders, Reply::HoldHeaders});
    const auto started = std::chrono::steady_clock::now();
    const auto result = client.GetInteractive(server.Url(), {}, 1000);
    std::cout << "MEASURE header-deadline elapsed_ms=" << Elapsed(started) << '\n';
    Check(!result.error.empty() && result.timedOut, "header deadline is reported as a timeout");
    Check(Elapsed(started) < 1350 && server.Requests() <= 3, "header timeout is wall-clock bounded");
  }
  {
    LoopbackServer server({Reply::HoldBody, Reply::Ok});
    const auto result = client.GetInteractive(server.Url(), {}, 1500);
    Check(result.error.empty() && result.body == "ok" && server.Requests() == 2,
          "HTTP 200 body timeout is retryable, not false success");
  }
  {
    LoopbackServer server({Reply::Truncated, Reply::Ok});
    const auto result = client.GetInteractive(server.Url(), {}, 1500);
    Check(result.error.empty() && result.body == "ok" && server.Requests() == 2,
          "truncated HTTP 200 body is recovered");
  }
  {
    LoopbackServer server({Reply::HoldHeaders});
    const auto started = std::chrono::steady_clock::now();
    const auto result = client.PostInteractive(server.Url(), "{\"probe\":1}", {}, 700);
    std::cout << "MEASURE post-deadline elapsed_ms=" << Elapsed(started) << '\n';
    Check(!result.error.empty() && result.timedOut && server.Requests() == 1,
          "POST timeout remains single shot");
    Check(Elapsed(started) < 1050, "POST timeout is wall-clock bounded");
  }
  {
    LoopbackServer server({Reply::HoldHeaders});
    std::atomic_bool cancelled{false};
    std::thread cancel([&] { std::this_thread::sleep_for(50ms); cancelled.store(true); });
    const auto started = std::chrono::steady_clock::now();
    echo::core::HttpClientCancellationScope scope(&cancelled);
    const auto result = client.GetInteractive(server.Url(), {}, 2500);
    cancel.join();
    std::cout << "MEASURE cancel elapsed_ms=" << Elapsed(started) << '\n';
    Check(result.error == "cancelled" && Elapsed(started) < 400 && server.Requests() == 1,
          "scheduler cancellation interrupts I/O without retries");
  }
  {
    LoopbackServer server({Reply::NotFound});
    const auto result = client.GetInteractive(server.Url(), {}, 1500);
    Check(result.statusCode == 404 && server.Requests() == 1, "business HTTP failures are not retried");
    const auto tooLarge = client.GetInteractive(server.Url(), {}, 1500, 1);
    Check(tooLarge.error == "max_body_exceeded" && server.Requests() == 2,
          "body size rejection is not retried");
    echo::core::HttpClientSetTimeoutSetupFaultForTest(true);
    const auto setup = client.GetInteractive(server.Url(), {}, 1500);
    echo::core::HttpClientSetTimeoutSetupFaultForTest(false);
    Check(setup.error == "timeout_setup_failed" && server.Requests() == 2,
          "timeout setup failure sends nothing and is not retried");
  }
  {
    echo::core::HttpClientSetTransientFaultForTest(true);
    const auto small = client.GetInteractive("http://127.0.0.1/probe", {}, 300);
    Check(small.attemptCount == 1 && echo::core::HttpClientTransientFaultCountForTest() == 1,
          "tiny budgets make one attempt without spending retry backoff");
    echo::core::HttpClientSetTransientFaultForTest(false);
  }
  {
    // Quota/setup failures must never be treated as transport instability.
    auto& watchdog = echo::async::RequestWatchdog::Instance();
    std::vector<std::shared_ptr<std::atomic_bool>> flags;
    for (;;) {
      auto flag = std::make_shared<std::atomic_bool>(false);
      if (!watchdog.Arm(60000, flag, [] {})) break;
      flags.push_back(flag);
    }
    LoopbackServer server({Reply::Ok});
    const auto get = client.GetInteractive(server.Url(), {}, 1500);
    const auto post = client.PostInteractive(server.Url(), "{}", {}, 1500);
    Check(get.error == "watchdog_overload" && post.error == "watchdog_overload" && server.Requests() == 0,
          "admission overload returns immediately without network retries");
    for (const auto& flag : flags) watchdog.Cancel(flag);
  }
  {
    LoopbackServer server({Reply::Ok});
    std::atomic<int> bad{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) workers.emplace_back([&] {
      for (int j = 0; j < 16; ++j) {
        const auto result = client.GetInteractive(server.Url(), {}, 1500);
        if (!result.error.empty() || result.body != "ok") ++bad;
      }
    });
    std::thread closePools([] {
      for (int i = 0; i < 4; ++i) {
        std::this_thread::sleep_for(5ms);
        echo::core::CloseHttpConnectionPool();
      }
    });
    for (auto& worker : workers) worker.join();
    closePools.join();
    Check(bad == 0 && server.Requests() == 64,
          "concurrent requests and pool resets retain independent buffers and parent leases");
  }
  const auto drainDeadline = std::chrono::steady_clock::now() + 2s;
  while (echo::core::HttpClientLiveAsyncRequestCount() && std::chrono::steady_clock::now() < drainDeadline) {
    std::this_thread::sleep_for(10ms);
  }
  Check(echo::core::HttpClientLiveAsyncRequestCount() == 0, "final callbacks and parent leases are reclaimed");
  Check(echo::core::HttpClientLiveRequestHandleCount() == 0, "no request handle leaks");
  echo::core::CloseHttpConnectionPool();
  WSACleanup();
  return failures ? 1 : 0;
}
