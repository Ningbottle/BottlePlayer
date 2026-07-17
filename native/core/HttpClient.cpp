#include "echo/core/HttpClient.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace echo::core {

// Tracks WinHTTP request handles that were OpenRequest'd but not yet
// WinHttpCloseHandle'd. Used by resilience tests (P0-A) because
// GetProcessHandleCount does not reliably observe HINTERNET objects.
static std::atomic<long> g_liveRequestHandles{0};

// Thread-local cancel flag installed by HttpClientCancellationScope so
// nested service → HttpClient calls observe scheduler deadline cancel.
static thread_local const std::atomic_bool* t_threadCancelled = nullptr;

HttpClientCancellationScope::HttpClientCancellationScope(
    const std::atomic_bool* cancelled)
    : previous_(t_threadCancelled) {
  t_threadCancelled = cancelled;
}

HttpClientCancellationScope::~HttpClientCancellationScope() {
  t_threadCancelled = previous_;
}

namespace {

void CloseRequestHandle(HINTERNET request) {
  if (!request) return;
  WinHttpCloseHandle(request);
  g_liveRequestHandles.fetch_sub(1, std::memory_order_relaxed);
}

bool IsCancelled(const std::atomic_bool* cancelled) {
  if (cancelled && cancelled->load(std::memory_order_acquire)) return true;
  if (t_threadCancelled && t_threadCancelled->load(std::memory_order_acquire)) {
    return true;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────
// Process-wide request watchdog (action-typed entries)
//
// Single worker + min-heap of (deadline, action). Owner CAS-claims the shared
// atomic so only the winner runs action(). Expired-but-completed entries are
// lazy-dropped (claimed already true). Flag-only entries (scheduler) use
// actions that never touch HINTERNET — compile-time separation from close.
// ─────────────────────────────────────────────────────────────────────────

struct WatchdogEntry {
  std::chrono::steady_clock::time_point deadline;
  std::uint64_t seq = 0;
  std::shared_ptr<std::atomic_bool> claimed;
  std::function<void()> action;
};

// Min-heap by deadline (earliest first).
struct WatchdogCmp {
  bool operator()(const WatchdogEntry& a, const WatchdogEntry& b) const {
    if (a.deadline != b.deadline) return a.deadline > b.deadline;
    return a.seq > b.seq;
  }
};

class RequestWatchdog {
 public:
  static RequestWatchdog& Instance() {
    static RequestWatchdog wd;
    return wd;
  }

  // Generic arm: on deadline, if CAS claims, run action().
  void Arm(long timeoutMs, std::shared_ptr<std::atomic_bool> claimed,
           std::function<void()> action) {
    if (timeoutMs <= 0 || !claimed || !action) return;
    WatchdogEntry entry;
    entry.deadline = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(timeoutMs);
    entry.seq = nextSeq_.fetch_add(1, std::memory_order_relaxed);
    entry.claimed = std::move(claimed);
    entry.action = std::move(action);
    {
      std::lock_guard<std::mutex> lock(mu_);
      EnsureWorkerLocked();
      heap_.push(std::move(entry));
    }
    cv_.notify_one();
  }

  // WinHTTP thin wrapper: action closes the request handle.
  void Arm(HINTERNET request, long timeoutMs,
           std::shared_ptr<std::atomic_bool> claimed) {
    if (!request) return;
    Arm(timeoutMs, std::move(claimed), [request]() {
      CloseRequestHandle(request);
    });
  }

  RequestWatchdog(const RequestWatchdog&) = delete;
  RequestWatchdog& operator=(const RequestWatchdog&) = delete;

 private:
  RequestWatchdog() = default;

  ~RequestWatchdog() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  void EnsureWorkerLocked() {
    if (workerStarted_) return;
    workerStarted_ = true;
    worker_ = std::thread([this] { Loop(); });
  }

  void Loop() {
    for (;;) {
      WatchdogEntry expired;
      {
        std::unique_lock<std::mutex> lock(mu_);
        for (;;) {
          if (stop_) return;
          if (heap_.empty()) {
            cv_.wait(lock, [this] { return stop_ || !heap_.empty(); });
            if (stop_) return;
            continue;
          }
          const auto now = std::chrono::steady_clock::now();
          if (heap_.top().deadline > now) {
            cv_.wait_until(lock, heap_.top().deadline);
            continue;
          }
          expired = heap_.top();
          heap_.pop();
          break;
        }
      }
      // Outside the lock: winner of CAS runs action once.
      bool expected = false;
      if (expired.claimed &&
          expired.claimed->compare_exchange_strong(
              expected, true, std::memory_order_acq_rel)) {
        if (expired.action) expired.action();
      }
    }
  }

  std::mutex mu_;
  std::condition_variable cv_;
  std::priority_queue<WatchdogEntry, std::vector<WatchdogEntry>, WatchdogCmp>
      heap_;
  std::thread worker_;
  bool workerStarted_ = false;
  bool stop_ = false;
  std::atomic<std::uint64_t> nextSeq_{1};
};

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::wstring ToWide(const std::string& value) {
  if (value.empty()) return {};
  const int count =
      MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
  std::wstring wide(static_cast<std::size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), wide.data(), count);
  return wide;
}

std::string LastErrorText(const char* prefix) {
  std::ostringstream stream;
  stream << prefix << " failed with Win32 error " << GetLastError();
  return stream.str();
}

// ─────────────────────────────────────────────────────────────────────────
// 连接复用基础设施
//
// 旧实现：每次 Get/Post 都 WinHttpOpen + WinHttpConnect + WinHttpOpenRequest，
// 请求结束全部关闭。每个请求都付完整 DNS + TLS 握手，对高频打 *.kugou.com
// 的音乐播放器是可测量的延迟。
//
// 新实现：进程级共享一个 session 句柄（WinHttpOpen 一次），每个 host:port
// 的 connect 句柄缓存复用。只要 session + connect 存活，WinHTTP 内部会自动
// 对 keep-alive 的 TCP/TLS 连接做池化复用。request 句柄仍每请求新建（WinHTTP
// 的句柄层次要求如此），请求结束只关 request。
//
// 线程安全：g_pool 用 mutex 保护。WinHTTP 句柄本身在多线程并发使用时是
// 线程安全的（只要不同线程不同时操作同一个 request 句柄）；connect 句柄
// 可被多个 request 并发派生。EchoCore 的并发模型（FFI 读锁 + RequestScheduler
// 线程池）下，不同请求持有各自的 request，符合该约束。
// ─────────────────────────────────────────────────────────────────────────

struct ParsedUrl {
  std::wstring host;
  std::wstring path;  // path + extra info (query)
  INTERNET_PORT port = 0;
  int scheme = 0;     // INTERNET_SCHEME_HTTPS / _HTTP
};

bool CrackUrl(const std::wstring& wideUrl, ParsedUrl& out) {
  URL_COMPONENTS components{};
  components.dwStructSize = sizeof(components);
  components.dwSchemeLength = static_cast<DWORD>(-1);
  components.dwHostNameLength = static_cast<DWORD>(-1);
  components.dwUrlPathLength = static_cast<DWORD>(-1);
  components.dwExtraInfoLength = static_cast<DWORD>(-1);

  if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components)) return false;

  out.host.assign(components.lpszHostName, components.dwHostNameLength);
  out.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
  if (components.dwExtraInfoLength > 0) {
    out.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
  }
  out.port = components.nPort;
  out.scheme = components.nScheme;
  return true;
}

using HandlePtr = std::shared_ptr<void>;

HandlePtr WrapHandle(HINTERNET h) {
  return HandlePtr(h, [](void* p) {
    if (p) WinHttpCloseHandle(static_cast<HINTERNET>(p));
  });
}

class HttpConnectionPool {
 public:
  // 返回进程级共享 session（首次调用惰性创建）。connect 池依附于该 session。
  // 如果初始化失败，后续调用会重试（不再永久缓存失败状态）。
  HandlePtr Session() {
    std::lock_guard<std::mutex> lock(session_mutex_);
    if (session_) return session_;

    HINTERNET raw = WinHttpOpen(
        L"EchoMusicNative/0.1",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (raw) {
      // 进程级默认超时：解析 5s / 连接 5s / 发送 10s / 接收 10s。
      WinHttpSetTimeouts(raw, 5000, 5000, 10000, 10000);
      session_ = WrapHandle(raw);
    }
    return session_;
  }

  // 取（或创建并缓存）指定 host:port 的 connect 句柄。
  // 返回 shared_ptr；请求持有 lease 直到析构。
  // 失败返回 nullptr。
  HandlePtr Connect(const std::wstring& host, INTERNET_PORT port) {
    auto sess = Session();
    if (!sess) return nullptr;
    const std::wstring key = host + L":" + std::to_wstring(port);
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = connects_.find(key);
    if (it != connects_.end()) return it->second;

    HINTERNET raw = WinHttpConnect(
        static_cast<HINTERNET>(sess.get()), host.c_str(), port, 0);
    if (!raw) return nullptr;
    auto entry = WrapHandle(raw);
    connects_[key] = entry;
    return entry;
  }

  // 剔除指定 host:port 的 connect：从 map 中移除，标记退役。
  // 实际关闭延迟到最后一个 shared_ptr 引用释放（即最后一个使用该 connect
  // 的请求结束）。这确保不会并发关闭正在被其他请求使用的 WinHTTP 句柄。
  void Evict(const std::wstring& host, INTERNET_PORT port) {
    const std::wstring key = host + L":" + std::to_wstring(port);
    std::lock_guard<std::mutex> lock(mutex_);
    connects_.erase(key);
  }

  // 优雅关闭所有句柄（保留对象壳）。
  // 清空 map/session → shared_ptr 引用计数降零时自动 WinHttpCloseHandle。
  // 使用 std::lock 同时锁定两把 mutex，消除 TOCTOU 窗口。
  // 关闭后 Session()/Connect() 会尝试重新创建，实现优雅降级。
  void CloseAll() {
    std::lock(mutex_, session_mutex_);
    std::lock_guard<std::mutex> lock(mutex_, std::adopt_lock);
    std::lock_guard<std::mutex> slock(session_mutex_, std::adopt_lock);
    connects_.clear();
    session_.reset();
  }

  ~HttpConnectionPool() {
    // 析构顺序：先 connect 后 session（WinHTTP 要求子句柄先于父句柄关闭）。
    // shared_ptr 保证：connects_ 中的引用先于 session_ 释放。
    std::lock(mutex_, session_mutex_);
    std::lock_guard<std::mutex> lock(mutex_, std::adopt_lock);
    std::lock_guard<std::mutex> slock(session_mutex_, std::adopt_lock);
    connects_.clear();
    session_.reset();
  }

  // 单例：进程内一份，随全局析构销毁。
  static HttpConnectionPool& Instance() {
    static HttpConnectionPool pool;
    return pool;
  }

  HttpConnectionPool(const HttpConnectionPool&) = delete;
  HttpConnectionPool& operator=(const HttpConnectionPool&) = delete;

 private:
  HttpConnectionPool() = default;

  std::mutex session_mutex_;
  HandlePtr session_;  // shared_ptr: WrapHandle → WinHttpCloseHandle on last release
  std::unordered_map<std::wstring, HandlePtr> connects_;
  std::mutex mutex_;  // 保护 connects_（session_ 由 session_mutex_ 保护）
};

// 一次请求的公共执行逻辑：Get/Post 共用。
// method 为 L"GET"/L"POST"；postBody/postLen 为空表示 GET。
HttpResult ExecuteRequest(
    const ParsedUrl& url,
    const wchar_t* method,
    const std::unordered_map<std::string, std::string>& headers,
    const void* postBody,
    DWORD postLen,
    bool ensureJsonContentType,
    long totalTimeoutMs,
    std::size_t maxBodyBytes) {
  HttpResult result;
  auto& pool = HttpConnectionPool::Instance();
  auto startTime = std::chrono::steady_clock::now();

  auto conn = pool.Connect(url.host, url.port);
  if (!conn) {
    result.error = LastErrorText("WinHttpConnect");
    return result;
  }
  HINTERNET connect = static_cast<HINTERNET>(conn.get());

  const DWORD flags = url.scheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET request = WinHttpOpenRequest(
      connect, method, url.path.c_str(), nullptr,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!request) {
    result.error = LastErrorText("WinHttpOpenRequest");
    return result;  // conn lease 由 shared_ptr 管理，析构时自动回收
  }
  g_liveRequestHandles.fetch_add(1, std::memory_order_relaxed);

  // Watchdog: WinHttpSendRequest/WinHttpReceiveResponse don't always honor
  // per-op timeouts on older Windows. A process-wide RequestWatchdog owns a
  // min-heap of (deadline, HINTERNET) and aborts hung calls via close.
  //
  // RACE-CRITICAL ordering: the watchdog CAS-sets claimed = true BEFORE
  // WinHttpCloseHandle, AND the main thread skips its own close when it
  // loses the CAS. Prevents double-close (can crash winhttp.dll on older OS).
  //
  // IMPORTANT: disarm ONLY once, at final cleanup (after body read or on
  // send/receive failure). Early disarm after ReceiveResponse used to skip
  // the final close — leaking every successful request handle (P0-A).
  auto watchdogCancelled = std::make_shared<std::atomic_bool>(false);
  if (totalTimeoutMs > 0) {
    RequestWatchdog::Instance().Arm(request, totalTimeoutMs, watchdogCancelled);
  }

  // CDN 30x 跳转必须显式跟随，否则封面/签名媒体 URL 会静默退化为占位/播放失败。
  DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

  // Bound per-op timeouts so a hung server can't block past the total
  // budget. WinHttpSendRequest / WinHttpReceiveResponse each have their own
  // timeout option; without these, the default is 30s per op, which can
  // exceed the 9s (or 500ms) total budget the caller asked for. We split
  // the total budget roughly in thirds: connect / send / receive.
  if (totalTimeoutMs > 0) {
    // Connect timeout applies to the connection, not the request handle.
    // Reuse the same budget as a conservative cap; the connect itself is
    // typically fast when reusing a pooled connection.
    DWORD connectTimeout = static_cast<DWORD>(std::min<long>(totalTimeoutMs / 2, 6000));
    WinHttpSetOption(connect, WINHTTP_OPTION_CONNECT_TIMEOUT,
                     &connectTimeout, sizeof(connectTimeout));
    DWORD opTimeout = static_cast<DWORD>(std::min<long>(
        std::max<long>(totalTimeoutMs / 3, 100), 10000));
    WinHttpSetOption(request, WINHTTP_OPTION_SEND_TIMEOUT, &opTimeout, sizeof(opTimeout));
    WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_TIMEOUT, &opTimeout, sizeof(opTimeout));
    // WINHTTP_OPTION_RESPONSE_TIMEOUT bounds the WinHttpReceiveResponse
    // wait for response headers. Available on Windows 8.1+; defined here
    // as 7 because older Windows SDKs may not export the constant.
    DWORD responseTimeout = opTimeout;
    constexpr DWORD kResponseTimeoutOption = 7;
    WinHttpSetOption(request, kResponseTimeoutOption,
                     &responseTimeout, sizeof(responseTimeout));
  }

  // 组装 header 块；POST 在缺省时补 Content-Type: application/json。
  std::wstring headerBlock;
  bool hasContentType = false;
  for (const auto& [key, value] : headers) {
    headerBlock += ToWide(key);
    headerBlock += L": ";
    headerBlock += ToWide(value);
    headerBlock += L"\r\n";
    if (ensureJsonContentType && Lower(key) == "content-type") hasContentType = true;
  }
  if (ensureJsonContentType && !hasContentType) {
    headerBlock += L"Content-Type: application/json\r\n";
  }

  const wchar_t* headerPtr =
      headerBlock.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerBlock.c_str();
  const DWORD headerLength =
      headerBlock.empty() ? 0 : static_cast<DWORD>(headerBlock.size());

  const bool sent = WinHttpSendRequest(
      request, headerPtr, headerLength,
      const_cast<void*>(postBody), postLen, postLen, 0);
  if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
    // Determine whether the watchdog fired (and therefore already closed
    // the handle). Use compare_exchange so the main thread only closes
    // when it successfully claims the close right — preventing a
    // double-close race with the watchdog.
    bool expected = false;
    bool watchdogFired = !watchdogCancelled->compare_exchange_strong(
        expected, true, std::memory_order_acq_rel);
    result.timedOut = watchdogFired;
    result.error = LastErrorText("WinHttpSendRequest/WinHttpReceiveResponse");
    if (!watchdogFired) {
      // We own the close.
      CloseRequestHandle(request);
    }
    pool.Evict(url.host, url.port);  // 剔除坏 connect，避免永久复用中毒句柄
    return result;
  }

  DWORD statusCode = 0;
  DWORD statusSize = sizeof(statusCode);
  if (WinHttpQueryHeaders(
          request,
          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
    result.statusCode = static_cast<long>(statusCode);
  }

  DWORD available = 0;
  while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
    // Total receive deadline check
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count();
    if (elapsed >= totalTimeoutMs) {
      result.timedOut = true;
      result.error = "total_receive_timeout";
      break;
    }
    // Max body size guard
    if (result.body.size() + available > maxBodyBytes) {
      result.error = "max_body_exceeded";
      break;
    }
    std::vector<char> buffer(available);
    DWORD read = 0;
    if (!WinHttpReadData(request, buffer.data(), available, &read)) {
      result.error = LastErrorText("WinHttpReadData");
      break;
    }
    result.body.append(buffer.data(), buffer.data() + read);
  }

  // Disarm the watchdog (CAS to avoid overwriting a watchdog-claimed
  // true) and only close if the watchdog didn't already close.
  // This is the SOLE success-path close — do not CAS-disarm earlier.
  bool expectedFinal = false;
  bool watchdogClaimed = !watchdogCancelled->compare_exchange_strong(
      expectedFinal, true, std::memory_order_acq_rel);
  if (!watchdogClaimed) {
    CloseRequestHandle(request);  // 仅关 request；connect/session 由池管理
  }
  return result;
}

}  // namespace

void CloseHttpConnectionPool() {
  HttpConnectionPool::Instance().CloseAll();
}

long HttpClientLiveRequestHandleCount() {
  return g_liveRequestHandles.load(std::memory_order_relaxed);
}

HttpResult HttpClient::Get(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers,
    long totalTimeoutMs,
    std::size_t maxBodyBytes,
    const std::atomic_bool* cancelled) const {
  ParsedUrl parsed;
  if (!CrackUrl(ToWide(url), parsed)) {
    HttpResult r;
    r.error = LastErrorText("WinHttpCrackUrl");
    return r;
  }
  // Bounded retry with shared budget: the totalTimeoutMs is the *entire*
  // budget across all attempts + backoff, not per-attempt. This prevents
  // retry from amplifying 9s into 27s+. GET only — unique retry owner.
  auto budgetStart = std::chrono::steady_clock::now();
  static const long backoffMs[] = {500, 2000};
  for (int attempt = 0; attempt <= 2; ++attempt) {
    if (IsCancelled(cancelled)) {
      HttpResult r;
      r.timedOut = true;
      r.error = "cancelled";
      return r;
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - budgetStart).count();
    long remaining = totalTimeoutMs - static_cast<long>(elapsed);
    // Don't retry if there isn't enough budget left to even attempt
    // (we need at least totalTimeoutMs/3 for per-op timeouts, plus some
    // overhead). On the first attempt, always run.
    if (attempt > 0 && remaining < totalTimeoutMs / 3 + 100) {
      HttpResult r;
      r.timedOut = true;
      r.error = "total_budget_exhausted";
      return r;
    }
    if (remaining < 100) remaining = 100;
    auto res = ExecuteRequest(parsed, L"GET", headers, nullptr, 0,
                              /*ensureJsonContentType=*/false,
                              remaining, maxBodyBytes);
    if (IsCancelled(cancelled)) {
      res.timedOut = true;
      res.error = "cancelled";
      return res;
    }
    bool transient = res.timedOut ||
                     (!res.error.empty() && res.statusCode == 0);
    if (!transient || attempt == 2) return res;
    std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs[attempt]));
  }
  HttpResult r;
  r.error = "retry_exhausted";
  return r;
}

HttpResult HttpClient::Post(
    const std::string& url,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& headers,
    long totalTimeoutMs,
    std::size_t maxBodyBytes,
    const std::atomic_bool* cancelled) const {
  ParsedUrl parsed;
  if (!CrackUrl(ToWide(url), parsed)) {
    HttpResult r;
    r.error = LastErrorText("WinHttpCrackUrl");
    return r;
  }
  // P1-F: Post is non-idempotent (e.g. /playhistory/upload). Single attempt only.
  if (IsCancelled(cancelled)) {
    HttpResult r;
    r.timedOut = true;
    r.error = "cancelled";
    return r;
  }
  long remaining = totalTimeoutMs > 0 ? totalTimeoutMs : 9000;
  if (remaining < 100) remaining = 100;
  return ExecuteRequest(
      parsed, L"POST", headers, body.data(), static_cast<DWORD>(body.size()),
      /*ensureJsonContentType=*/true,
      remaining, maxBodyBytes);
}

}  // namespace echo::core
