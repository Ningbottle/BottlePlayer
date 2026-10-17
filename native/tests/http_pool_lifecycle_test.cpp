#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "echo/core/HttpClient.h"

using namespace std::chrono_literals;
namespace {
int failures = 0;
void Check(bool ok, const char* message) {
  std::cout << (ok ? "[ok] " : "[FAIL] ") << message << '\n';
  if (!ok) ++failures;
}

class LoopbackServer {
 public:
  explicit LoopbackServer(std::shared_future<void> gate = {}) : gate_(std::move(gate)) {
    listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener_ == INVALID_SOCKET) throw std::runtime_error("test_socket_failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int bytes = sizeof(address);
    if (bind(listener_, reinterpret_cast<sockaddr*>(&address), bytes) != 0 ||
        getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &bytes) != 0 ||
        listen(listener_, 4) != 0) {
      closesocket(listener_);
      throw std::runtime_error("test_listen_failed");
    }
    url_ = "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/pool";
    worker_ = std::thread([this, listener = listener_] {
      while (!stop_.load()) {
        SOCKET connection = accept(listener, nullptr, nullptr);
        if (connection == INVALID_SOCKET) break;
        DWORD timeout = 2000;
        setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
        std::string request;
        char buffer[1024];
        while (request.find("\r\n\r\n") == std::string::npos) {
          const int count = recv(connection, buffer, sizeof(buffer), 0);
          if (count <= 0) break;
          request.append(buffer, count);
        }
        if (request.find("\r\n\r\n") != std::string::npos) {
          if (!arrived_.exchange(true)) firstRequest_.set_value();
          const std::string headers = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n";
          send(connection, headers.data(), static_cast<int>(headers.size()), 0);
          if (gate_.valid()) gate_.wait_for(8s);
          send(connection, "ok", 2, 0);
        }
        closesocket(connection);
      }
    });
  }
  ~LoopbackServer() {
    stop_.store(true);
    shutdown(listener_, SD_BOTH);
    closesocket(listener_);
    if (worker_.joinable()) worker_.join();
  }
  const std::string& Url() const { return url_; }
  bool AwaitRequest() { return firstRequest_.get_future().wait_for(2s) == std::future_status::ready; }

 private:
  SOCKET listener_ = INVALID_SOCKET;
  std::thread worker_;
  std::shared_future<void> gate_;
  std::promise<void> firstRequest_;
  std::atomic_bool stop_{false}, arrived_{false};
  std::string url_;
};

bool Success(const echo::core::HttpResult& result) {
  static int reportedFailures = 0;
  if ((!result.error.empty() || result.statusCode != 200) && reportedFailures++ < 3)
    std::cout << "http_failure status=" << result.statusCode << " error=" << result.error
              << " winhttp=" << result.winHttpErrorCode << '\n';
  return result.error.empty() && result.statusCode == 200 && result.body == "ok";
}

void TestPoolCapacityAndLeases() {
  echo::core::CloseHttpConnectionPool();
  std::promise<void> release;
  LoopbackServer held(release.get_future().share());
  auto active = std::async(std::launch::async, [&] {
    return echo::core::HttpClient().Get(held.Url(), {}, 10000, 1024, nullptr, false);
  });
  Check(held.AwaitRequest(), "an active request holds the oldest connection lease");
  std::vector<std::unique_ptr<LoopbackServer>> servers;
  bool succeeded = true, bounded = true;
  for (std::size_t i = 0; i < echo::core::kHttpConnectionPoolCapacity + 8; ++i) {
    auto server = std::make_unique<LoopbackServer>();
    succeeded &= Success(echo::core::HttpClient().Get(server->Url(), {}, 3000, 1024, nullptr, false));
    bounded &= echo::core::HttpClientCachedConnectionCount() <= echo::core::kHttpConnectionPoolCapacity;
    // Keep listeners open so every iteration uses a distinct host:port.
    servers.push_back(std::move(server));
  }
  Check(succeeded, "requests across more hosts than the cache capacity all succeed");
  Check(bounded && echo::core::HttpClientCachedConnectionCount() == echo::core::kHttpConnectionPoolCapacity,
        "connection cache stays bounded throughout host churn");
  release.set_value();
  Check(Success(active.get()), "LRU retirement preserves an active child request");
  echo::core::CloseHttpConnectionPool();
  Check(echo::core::HttpClientCachedConnectionCount() == 0, "pool shutdown releases every cached lease");
}

void TestOldFailurePreservesNewGeneration() {
  std::promise<void> release;
  LoopbackServer server(release.get_future().share());
  std::atomic_bool cancelled{false};
  auto old = std::async(std::launch::async, [&] {
    return echo::core::HttpClient().Get(server.Url(), {}, 5000, 1024, &cancelled, false);
  });
  Check(server.AwaitRequest(), "old generation reached the controlled upstream");
  echo::core::CloseHttpConnectionPool();
  auto current = std::async(std::launch::async, [&] {
    return echo::core::HttpClient().Get(server.Url(), {}, 5000, 1024, nullptr, false);
  });
  const auto until = std::chrono::steady_clock::now() + 2s;
  while (echo::core::HttpClientCachedConnectionCount() != 1 && std::chrono::steady_clock::now() < until)
    std::this_thread::sleep_for(1ms);
  Check(echo::core::HttpClientCachedConnectionCount() == 1, "a new session cached a replacement for the same host");
  cancelled.store(true);
  const auto failed = old.get();
  Check(failed.error == "cancelled", "the old request fails through caller cancellation");
  Check(echo::core::HttpClientCachedConnectionCount() == 1,
        "a late failure cannot evict the replacement connection");
  release.set_value();
  Check(Success(current.get()), "the replacement generation completes successfully");
  echo::core::CloseHttpConnectionPool();
}
}  // namespace

int main() {
  std::cout << std::unitbuf;
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;
  TestPoolCapacityAndLeases();
  TestOldFailurePreservesNewGeneration();
  Check(echo::core::HttpClientLiveRequestHandleCount() == 0, "all request handles are released");
  Check(echo::core::HttpClientLiveAsyncRequestCount() == 0, "all completed callback contexts are released");
  WSACleanup();
  return failures ? 1 : 0;
}
