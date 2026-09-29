#include "echo/core/HttpClient.h"
#include "echo/async/RequestWatchdog.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <memory>
#include <mutex>
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
// Test-only fault injection (see HttpClient.h): forces the per-op timeout
// setup to fail so the timeout_setup_failed path is deterministically
// reachable. Never set in production.
static std::atomic<bool> g_faultTimeoutSetup{false};

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

// Cooperative deadline arm (Stage 3a): the watchdog NEVER closes the
// request handle — cross-thread closes against synchronous WinHTTP calls
// violate the WinHTTP concurrency contract (plan R4). It only raises the
// deadline flag; the owner thread closes the handle itself after its
// WinHTTP calls return (it is the SOLE closer, so no close CAS is needed).
// Worst-case exposure after a fired deadline is bounded by the per-op
// timeout already set on the request.
// Returns false when the watchdog rejects the registration (backlog cap) —
// the caller must fail the request instead of running it unwatched.
bool ArmRequestHandleWatchdog(long timeoutMs,
                              std::shared_ptr<std::atomic_bool> deadlineFired) {
  if (timeoutMs <= 0 || !deadlineFired) return false;
  return echo::async::RequestWatchdog::Instance().Arm(
      timeoutMs, std::move(deadlineFired),
      [deadlineFired]() {
        deadlineFired->store(true, std::memory_order_release);
      });
}

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

  // Stage 3a cooperative deadline: the watchdog only raises
  // deadlineFiredFlag when the total budget expires (per-op timeouts are
  // what actually unblock a hung call). The handle is closed solely by
  // this thread, after its WinHTTP calls return.
  auto deadlineFiredFlag = std::make_shared<std::atomic_bool>(false);
  if (totalTimeoutMs > 0 &&
      !ArmRequestHandleWatchdog(totalTimeoutMs, deadlineFiredFlag)) {
    // Watchdog rejected the registration (pending backlog cap). Running
    // without deadline enforcement is not an option — a hung send/receive
    // would hold the handle indefinitely. Fail explicitly and release the
    // handle we own (nothing was armed, so there is no race; the pooled
    // connect stays healthy — this is admission control, not a poisoned
    // connection).
    result.error = "watchdog_overload";
    CloseRequestHandle(request);
    return result;
  }

  // CDN 30x 跳转必须显式跟随，否则封面/签名媒体 URL 会静默退化为占位/播放失败。
  DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

  // Bound per-op timeouts so a hung server can't block past the total
  // budget. All of them live on the REQUEST handle: WinHTTP rejects
  // WINHTTP_OPTION_CONNECT_TIMEOUT on a connect handle with
  // ERROR_WINHTTP_INCORRECT_HANDLE_TYPE (12018) — the old call failed
  // silently, leaving connects at the system default. After Stage 3a
  // removed the watchdog's cross-thread close, these per-op timeouts are
  // the load-bearing unblock mechanism, so every setup failure is
  // surfaced: a request whose timeouts cannot be enforced is not run.
  if (totalTimeoutMs > 0) {
    DWORD connectTimeout = static_cast<DWORD>(std::min<long>(totalTimeoutMs / 2, 6000));
    DWORD opTimeout = static_cast<DWORD>(std::min<long>(
        std::max<long>(totalTimeoutMs / 3, 100), 10000));
    DWORD responseTimeout = opTimeout;
    // WINHTTP_OPTION_RESPONSE_TIMEOUT bounds the WinHttpReceiveResponse
    // wait for response headers. Available on Windows 8.1+; defined here
    // as 7 because older Windows SDKs may not export the constant.
    constexpr DWORD kResponseTimeoutOption = 7;
    // g_faultTimeoutSetup: test-only injection that deterministically
    // produces the timeout_setup_failed path (quota release + GET no-retry
    // are pinned by behavioral tests).
    const bool timeoutsOk =
        !g_faultTimeoutSetup.load(std::memory_order_acquire) &&
        WinHttpSetOption(request, WINHTTP_OPTION_CONNECT_TIMEOUT,
                         &connectTimeout, sizeof(connectTimeout)) &&
        WinHttpSetOption(request, WINHTTP_OPTION_SEND_TIMEOUT,
                         &opTimeout, sizeof(opTimeout)) &&
        WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_TIMEOUT,
                         &opTimeout, sizeof(opTimeout)) &&
        WinHttpSetOption(request, kResponseTimeoutOption,
                         &responseTimeout, sizeof(responseTimeout));
    if (!timeoutsOk) {
      // The deadline entry was already armed (Arm precedes this block) —
      // release its admission budget before returning, exactly like every
      // other early-exit path. Review P1: the first draft leaked it here,
      // and GET then retried the error through its backoff schedule.
      echo::async::RequestWatchdog::Instance().Cancel(deadlineFiredFlag);
      result.error = "timeout_setup_failed";
      CloseRequestHandle(request);
      return result;  // connect stays healthy — admission-style rejection
    }
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
    // Stage 3a: the handle has no cross-thread closer anymore — this thread
    // is the SOLE closer. deadlineFiredFlag only records that the total
    // budget expired (the call itself was unblocked by its per-op timeout).
    const bool deadlineFired =
        deadlineFiredFlag->load(std::memory_order_acquire);
    result.timedOut = deadlineFired;
    result.error = LastErrorText("WinHttpSendRequest/WinHttpReceiveResponse");
    if (!deadlineFired) {
      // The deadline entry is still armed — release its admission budget
      // now instead of holding it until the deadline expires.
      echo::async::RequestWatchdog::Instance().Cancel(deadlineFiredFlag);
    }
    CloseRequestHandle(request);
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

  // Response Content-Length, for incomplete-body detection at EOF: a
  // response that ends with fewer bytes than promised is an error, never a
  // success (review probe: headers-only 200 with a stalled body).
  DWORD contentLength = 0;
  DWORD contentLenSize = sizeof(contentLength);
  const bool hasContentLength = WinHttpQueryHeaders(
      request,
      WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
      WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &contentLenSize,
      WINHTTP_NO_HEADER_INDEX);

  DWORD available = 0;
  bool receiveComplete = false;
  while (!receiveComplete) {
    // Deadline check between blocking calls: a fired deadline (watchdog
    // flag or elapsed budget) must NEVER surface as a successful response.
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count();
    if (deadlineFiredFlag->load(std::memory_order_acquire) ||
        elapsed >= totalTimeoutMs) {
      result.timedOut = true;
      result.error = "total_receive_timeout";
      break;
    }
    if (!WinHttpQueryDataAvailable(request, &available)) {
      // A query failure is an error, NOT a normal end-of-stream — the old
      // loop silently exited here and reported headers as success.
      result.error = LastErrorText("WinHttpQueryDataAvailable");
      break;
    }
    if (available == 0) {
      receiveComplete = true;  // clean end-of-stream
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
    if (read == 0) {
      // Data was announced but the connection delivered nothing: an early
      // close, not EOF.
      result.error = "connection_closed_early";
      break;
    }
    result.body.append(buffer.data(), buffer.data() + read);
  }
  if (receiveComplete && hasContentLength && result.body.size() < contentLength) {
    result.error = "incomplete_body";
  }

  // Stage 3a: this thread is the SOLE closer of the request handle. A fired
  // deadline must be reflected in the result — never a bare success; the
  // entry was already released by the executor in that case. Otherwise
  // cancel the still-armed entry to free its admission budget now.
  const bool deadlineFired =
      deadlineFiredFlag->load(std::memory_order_acquire);
  if (deadlineFired) {
    result.timedOut = true;
    if (result.error.empty()) {
      result.error = "total_receive_timeout";
    }
  } else {
    echo::async::RequestWatchdog::Instance().Cancel(deadlineFiredFlag);
  }
  CloseRequestHandle(request);  // 仅关 request；connect/session 由池管理
  return result;
}

}  // namespace

void CloseHttpConnectionPool() {
  HttpConnectionPool::Instance().CloseAll();
}

long HttpClientLiveRequestHandleCount() {
  return g_liveRequestHandles.load(std::memory_order_relaxed);
}

void HttpClientSetTimeoutSetupFaultForTest(bool enabled) {
  g_faultTimeoutSetup.store(enabled, std::memory_order_release);
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
    // Local admission/setup failures are not network conditions: return
    // them verbatim and immediately. Classifying them as transient used to
    // send them through the backoff schedule (review probe: 2527ms for a
    // 1000ms budget) while hammering an already-saturated watchdog.
    const bool admissionFailure = res.error == "watchdog_overload" ||
                                  res.error == "timeout_setup_failed";
    bool transient = !admissionFailure &&
                     (res.timedOut ||
                      (!res.error.empty() && res.statusCode == 0));
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
