#include "echo/core/HttpClient.h"
#include "echo/async/RequestWatchdog.h"
#include "echo/diagnostics/EchoDiagnostics.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cwctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
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
static std::atomic<long> g_liveAsyncRequests{0};
// Test-only fault injection (see HttpClient.h): forces the per-op timeout
// setup to fail so the timeout_setup_failed path is deterministically
// reachable. Never set in production.
static std::atomic<bool> g_faultTimeoutSetup{false};
static thread_local bool t_transientFaultForTest = false;
static thread_local long t_transientFaultCountForTest = 0;

// Thread-local cancel flag installed by HttpClientCancellationScope so
// nested service → HttpClient calls observe scheduler deadline cancel.
static thread_local const std::atomic_bool* t_threadCancelled = nullptr;
static thread_local const std::atomic_bool* t_callerCancelled = nullptr;
static thread_local auto t_routeDeadline = std::chrono::steady_clock::time_point::max();

HttpClientCancellationScope::HttpClientCancellationScope(
    const std::atomic_bool* cancelled, long remainingBudgetMs, const std::atomic_bool* callerCancelled)
    : previous_(t_threadCancelled), previousDeadline_(t_routeDeadline), previousCaller_(t_callerCancelled),
      previousOperation_(execution::CurrentOperation()) {
  t_threadCancelled = cancelled;
  t_callerCancelled = callerCancelled;
  if (remainingBudgetMs > 0) t_routeDeadline = std::min(t_routeDeadline,
      std::chrono::steady_clock::now() + std::chrono::milliseconds(remainingBudgetMs));
  // Legacy scope borrows flags only for its synchronous lifetime. Production
  // C_API installs an owning OperationContext before entering the transport.
  auto legacy = previousOperation_;
  legacy.deadline = std::min(legacy.deadline, t_routeDeadline);
  if (cancelled && !legacy.workerCancelled)
    legacy.workerCancelled = std::shared_ptr<const std::atomic_bool>(cancelled, [](auto*) {});
  if (callerCancelled && !legacy.callerCancelled)
    legacy.callerCancelled = std::shared_ptr<const std::atomic_bool>(callerCancelled, [](auto*) {});
  execution::currentOperation = std::move(legacy);
}

HttpClientCancellationScope::~HttpClientCancellationScope() {
  t_threadCancelled = previous_;
  t_routeDeadline = previousDeadline_;
  t_callerCancelled = previousCaller_;
  execution::currentOperation = previousOperation_;
}

bool HttpRouteDeadlineExpired() {
  return std::chrono::steady_clock::now() >=
      std::min(t_routeDeadline, execution::currentOperation.deadline);
}

bool HttpRequestCanContinue() {
  return (!t_threadCancelled || !t_threadCancelled->load(std::memory_order_acquire)) &&
      (!t_callerCancelled || !t_callerCancelled->load(std::memory_order_acquire)) &&
      !HttpRouteDeadlineExpired() && execution::currentOperation.CanContinue();
}


namespace {

void CloseRequestHandle(HINTERNET request) {
  if (!request) return;
  WinHttpCloseHandle(request);
  g_liveRequestHandles.fetch_sub(1, std::memory_order_relaxed);
}

bool IsCancelled(const std::atomic_bool* cancelled) {
  if (execution::currentOperation.Reason() == execution::StopReason::Cancelled) return true;
  if (cancelled && cancelled->load(std::memory_order_acquire)) return true;
  if (t_threadCancelled && t_threadCancelled->load(std::memory_order_acquire)) {
    return true;
  }
  if (t_callerCancelled && t_callerCancelled->load(std::memory_order_acquire)) return true;
  return false;
}

// A stop caused by the route deadline is a deadline, not a user cancellation.
// The scheduler watchdog raises its token at that same deadline, so check the
// route clock first to keep that production path distinct from caller aborts.
bool StoppedForDeadline() {
  return HttpRouteDeadlineExpired();
}

void SetStopReason(HttpResult& result, const std::atomic_bool* cancelled) {
  if (StoppedForDeadline()) {
    result.error = "total_budget_exhausted";
    result.timedOut = true;
    return;
  }
  if (IsCancelled(cancelled)) {
    result.error = "cancelled";
    result.timedOut = true;
  }
}

long BoundToRouteBudget(long requested) {
  const auto deadline = std::min(t_routeDeadline, execution::currentOperation.deadline);
  if (deadline == std::chrono::steady_clock::time_point::max()) return requested;
  // Round up, not down: request-side timeouts and the watchdog must land at or
  // after the route deadline so the route check wins the classification. A
  // truncated slice could expire a hair early and misreport route-budget
  // exhaustion as a request timeout or cancellation.
  const auto left = static_cast<long>(std::chrono::ceil<std::chrono::milliseconds>(
      deadline - std::chrono::steady_clock::now()).count());
  return std::min(requested, std::max(1L, left));
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
      timeoutMs, deadlineFired,
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

std::string ErrorText(const char* prefix, DWORD error) {
  std::ostringstream stream;
  stream << prefix << " failed with Win32 error " << error;
  return stream.str();
}

std::string LastErrorText(const char* prefix) { return ErrorText(prefix, GetLastError()); }

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
// Cache at most kHttpConnectionPoolCapacity host:port leases. Retirement
// drops only the cache's reference; active requests keep their parent alive.
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

std::string DiagnosticHost(const std::wstring& host) {
  std::string safe;
  safe.reserve(host.size());
  for (wchar_t ch : host) {
    if (ch > 0x7f) return "unknown";
    const char c = static_cast<char>(ch);
    const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ':' || c == '[' || c == ']';
    if (!allowed) return "unknown";
    safe.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return safe.empty() ? "unknown" : safe;
}

struct RedirectLocationDiagnostic {
  std::string host = "unknown";
  bool schemeChanged = false;
  bool portChanged = false;
};

RedirectLocationDiagnostic RedirectLocationInfo(HINTERNET request, const ParsedUrl& original) {
  RedirectLocationDiagnostic info;
  DWORD byteCount = 0;
  WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
      nullptr, &byteCount, WINHTTP_NO_HEADER_INDEX);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || byteCount == 0) return info;

  std::vector<wchar_t> buffer(byteCount / sizeof(wchar_t) + 1, L'\0');
  DWORD bufferBytes = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
  if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
          buffer.data(), &bufferBytes, WINHTTP_NO_HEADER_INDEX)) {
    return info;
  }
  const std::wstring location(buffer.data());
  std::wstring lowerLocation = location;
  std::transform(lowerLocation.begin(), lowerLocation.end(), lowerLocation.begin(),
      [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
  ParsedUrl target;
  const bool absoluteHttp = lowerLocation.rfind(L"http://", 0) == 0 ||
      lowerLocation.rfind(L"https://", 0) == 0;
  bool parsed = absoluteHttp && CrackUrl(location, target);
  if (!parsed && location.size() >= 2 && location[0] == L'/' && location[1] == L'/') {
    const std::wstring absolute =
        (original.scheme == INTERNET_SCHEME_HTTPS ? L"https:" : L"http:") + location;
    parsed = CrackUrl(absolute, target);
  } else if (!parsed && !location.empty() && location[0] == L'/' &&
             (location.size() == 1 || location[1] != L'/')) {
    // An origin-relative path stays on the same scheme, host, and port.
    target = original;
    parsed = true;
  }
  if (!parsed) return info;

  info.schemeChanged = target.scheme != original.scheme;
  info.portChanged = target.port != original.port;
  if ((target.scheme == INTERNET_SCHEME_HTTP || target.scheme == INTERNET_SCHEME_HTTPS) &&
      !target.host.empty()) {
    info.host = DiagnosticHost(target.host);
  }
  return info;
}

using HandlePtr = std::shared_ptr<void>;

HandlePtr WrapHandle(HINTERNET h, HandlePtr parent = {}) {
  return HandlePtr(h, [parent = std::move(parent)](void* p) {
    if (p) WinHttpCloseHandle(static_cast<HINTERNET>(p));
  });
}

class HttpConnectionPool {
 private:
  // 返回进程级共享 session（首次调用惰性创建）。connect 池依附于该 session。
  // 如果初始化失败，后续调用会重试（不再永久缓存失败状态）。
  HandlePtr SessionLocked() {
    if (session_) return session_;

    HINTERNET raw = WinHttpOpen(
        L"EchoMusicNative/0.1",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        WINHTTP_FLAG_ASYNC);
    if (!raw && GetLastError() == ERROR_INVALID_PARAMETER) {
      raw = WinHttpOpen(
          L"EchoMusicNative/0.1",
          WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
          WINHTTP_NO_PROXY_NAME,
          WINHTTP_NO_PROXY_BYPASS,
          WINHTTP_FLAG_ASYNC);
    }
    if (raw) {
      // 进程级默认超时：解析 5s / 连接 5s / 发送 10s / 接收 10s。
      WinHttpSetTimeouts(raw, 5000, 5000, 10000, 10000);
      session_ = WrapHandle(raw);
    }
    return session_;
  }

 public:
  // 取（或创建并缓存）指定 host:port 的 connect 句柄。
  // 返回 shared_ptr；请求持有 lease 直到析构。
  // 失败返回 nullptr。
  HandlePtr Connect(const std::wstring& host, INTERNET_PORT port) {
    HandlePtr retired;
    // Serialize the session generation and map together with CloseAll.
    // Otherwise an old session acquired just before CloseAll could repopulate
    // the freshly-cleared connect map after shutdown/reinitialization.
    std::lock_guard lock(mutex_);
    auto sess = SessionLocked();
    if (!sess) return nullptr;
    const std::wstring key = host + L":" + std::to_wstring(port);

    auto it = connects_.find(key);
    if (it != connects_.end()) {
      it->second.lastUsed = ++useSequence_;
      return it->second.handle;
    }

    HINTERNET raw = WinHttpConnect(
        static_cast<HINTERNET>(sess.get()), host.c_str(), port, 0);
    if (!raw) return nullptr;
    // Carry the exact parent generation in the connect lease. CloseAll cannot
    // invalidate an active child, even if a new session is created meanwhile.
    auto entry = WrapHandle(raw, std::move(sess));
    if (connects_.size() >= kHttpConnectionPoolCapacity) {
      const auto oldest = std::min_element(connects_.begin(), connects_.end(),
          [](const auto& a, const auto& b) { return a.second.lastUsed < b.second.lastUsed; });
      retired = std::move(oldest->second.handle);
      connects_.erase(oldest);
    }
    connects_.emplace(key, Connection{entry, ++useSequence_});
    return entry;
  }

  // 剔除指定 host:port 的 connect：从 map 中移除，标记退役。
  // 实际关闭延迟到最后一个 shared_ptr 引用释放（即最后一个使用该 connect
  // 的请求结束）。这确保不会并发关闭正在被其他请求使用的 WinHTTP 句柄。
  void Evict(const std::wstring& host, INTERNET_PORT port, const HandlePtr& expected) {
    const std::wstring key = host + L":" + std::to_wstring(port);
    HandlePtr retired;
    {
      std::lock_guard lock(mutex_);
      const auto it = connects_.find(key);
      // A failure from an old session must not evict a newer generation.
      if (it != connects_.end() && it->second.handle == expected) {
        retired = std::move(it->second.handle);
        connects_.erase(it);
      }
    }
  }

  // 优雅关闭所有句柄（保留对象壳）。
  // 清空 map/session → shared_ptr 引用计数降零时自动 WinHttpCloseHandle。
  // session 与 map 在同一把锁下退役，关闭在锁外完成。
  // 关闭后 Connect() 会尝试重新创建，实现优雅降级。
  void CloseAll() {
    HandlePtr retiredSession;
    decltype(connects_) retiredConnections;
    {
      std::lock_guard lock(mutex_);
      retiredConnections.swap(connects_);
      retiredSession = std::move(session_);
    }
    // Child leases are released before their session, outside the pool lock.
  }

  ~HttpConnectionPool() {
    // 析构顺序：先 connect 后 session（WinHTTP 要求子句柄先于父句柄关闭）。
    // shared_ptr 保证：connects_ 中的引用先于 session_ 释放。
    CloseAll();
  }

  // 单例：进程内一份，随全局析构销毁。
  static HttpConnectionPool& Instance() {
    static HttpConnectionPool pool;
    return pool;
  }

  std::size_t CachedCount() {
    std::lock_guard lock(mutex_);
    return connects_.size();
  }

  HttpConnectionPool(const HttpConnectionPool&) = delete;
  HttpConnectionPool& operator=(const HttpConnectionPool&) = delete;

 private:
  HttpConnectionPool() = default;
  struct Connection {
    HandlePtr handle;
    std::uint64_t lastUsed;
  };
  std::uint64_t useSequence_ = 0;
  HandlePtr session_;  // shared_ptr: WrapHandle → WinHttpCloseHandle on last release
  std::unordered_map<std::wstring, Connection> connects_;
  std::mutex mutex_;  // session generation and connection map
};

constexpr std::size_t kMaxAsyncRequests = 128;

struct AsyncQuota {
  AsyncQuota() { g_liveAsyncRequests.fetch_add(1, std::memory_order_relaxed); }
  ~AsyncQuota() { g_liveAsyncRequests.fetch_sub(1, std::memory_order_release); }
};

struct AsyncRequestState {
  // Declared first, destroyed last: admission includes buffer/lease teardown.
  AsyncQuota quota;
  HandlePtr connection;
  std::wstring headers;
  std::string requestBody;
  std::array<char, 8192> readBuffer{};
  std::mutex mutex;
  std::condition_variable cv;
  DWORD expected = 0;
  DWORD error = 0;
  DWORD bytes = 0;
  bool completed = false;
  std::atomic_bool closed{false}, ownerFinished{false};
  std::atomic<long> callbacks{0};
};

// WinHTTP may call back after CloseHandle returns. Keep the raw context alive
// until HANDLE_CLOSING and callback exit; reap only on an owning/caller thread,
// never inside the callback (parent lease destructors call WinHttpCloseHandle).
// The bounded registry is intentionally process-lifetime, as is its module.
class AsyncRequestRegistry {
 public:
  static AsyncRequestRegistry& Instance() {
    static auto* registry = new AsyncRequestRegistry;
    return *registry;
  }
  void Reap() {
    std::array<std::shared_ptr<AsyncRequestState>, kMaxAsyncRequests> retired;
    {
      std::lock_guard lock(mutex_);
      for (std::size_t i = 0; i < states_.size(); ++i) {
        const auto& state = states_[i];
        if (state && state->closed.load(std::memory_order_acquire) &&
            state->ownerFinished.load(std::memory_order_acquire) &&
            state->callbacks.load(std::memory_order_acquire) == 0) {
          retired[i] = std::move(states_[i]);
        }
      }
    }
  }
  std::shared_ptr<AsyncRequestState> Acquire() {
    Reap();
    std::lock_guard lock(mutex_);
    if (g_liveAsyncRequests.load(std::memory_order_acquire) >= kMaxAsyncRequests) return {};
    for (auto& slot : states_) {
      if (!slot) {
        slot = std::make_shared<AsyncRequestState>();
        return slot;
      }
    }
    return {};
  }
 private:
  std::mutex mutex_;
  std::array<std::shared_ptr<AsyncRequestState>, kMaxAsyncRequests> states_;
};

void CALLBACK InteractiveStatusCallback(HINTERNET, DWORD_PTR context, DWORD status,
                                       void* information, DWORD length) noexcept {
  auto* state = reinterpret_cast<AsyncRequestState*>(context);
  if (!state) return;
  state->callbacks.fetch_add(1, std::memory_order_acquire);
  try {
    {
      std::lock_guard lock(state->mutex);
      if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
        state->closed.store(true, std::memory_order_release);
      } else if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR || status == state->expected) {
        state->error = status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR && information &&
                length >= sizeof(WINHTTP_ASYNC_RESULT)
            ? static_cast<WINHTTP_ASYNC_RESULT*>(information)->dwError : 0;
        state->bytes = status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE ? length : 0;
        state->completed = true;
      }
    }
    state->cv.notify_one();
  } catch (...) {
    // No exception may unwind through a system callback. Owner's deadline
    // still bounds progress; retain an unprovably closed state conservatively.
  }
  // Last context access. Registry cannot reap a callback still using buffers.
  state->callbacks.fetch_sub(1, std::memory_order_release);
}

bool PinInteractiveModule() {
  static const bool pinned = [] {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&InteractiveStatusCallback), &module) != FALSE;
  }();
  return pinned;
}

struct AsyncRequestOwner {
  std::shared_ptr<AsyncRequestState> state;
  HINTERNET request = nullptr;
  bool callbackInstalled = false;
  ~AsyncRequestOwner() {
    // No WinHTTP call is still on this thread's stack. It is the sole closer.
    if (request) CloseRequestHandle(request);
    if (!callbackInstalled) state->closed.store(true, std::memory_order_release);
    state->ownerFinished.store(true, std::memory_order_release);
  }
};

struct InteractiveDeadline {
  std::shared_ptr<std::atomic_bool> flag = std::make_shared<std::atomic_bool>(false);
  bool armed = false;
  explicit InteractiveDeadline(long budget) : armed(ArmRequestHandleWatchdog(budget, flag)) {}
  ~InteractiveDeadline() {
    if (armed) {
      try { echo::async::RequestWatchdog::Instance().Cancel(flag); } catch (...) {}
    }
  }
};

// Asynchronous WinHTTP, synchronous service contract. Only the owner performs
// WinHTTP operations; callbacks publish completion. A deadline can therefore
// interrupt pending I/O without closing a synchronous handle across threads.
HttpResult ExecuteInteractiveRequest(
    const ParsedUrl& url, const wchar_t* method,
    const std::unordered_map<std::string, std::string>& headers,
    const std::string& body, bool ensureJsonContentType, long budgetMs,
    std::size_t maxBodyBytes, const std::atomic_bool* cancelled,
    const std::shared_ptr<std::atomic_bool>& outerDeadline) {
  HttpResult result;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
  if (t_transientFaultForTest) {
    ++t_transientFaultCountForTest;
    result.error = "test_transient_failure";
    return result;
  }
  if (!PinInteractiveModule()) {
    result.error = "module_pin_failed";
    return result;
  }
  auto state = AsyncRequestRegistry::Instance().Acquire();
  if (!state) {
    result.error = "http_callback_overload";
    return result;
  }
  AsyncRequestOwner owner{state};
  state->requestBody = body;  // valid even after timeout until HANDLE_CLOSING
  bool hasContentType = false;
  for (const auto& [key, value] : headers) {
    state->headers += ToWide(key) + L": " + ToWide(value) + L"\r\n";
    hasContentType |= Lower(key) == "content-type";
  }
  if (ensureJsonContentType && !hasContentType) state->headers += L"Content-Type: application/json\r\n";

  auto& pool = HttpConnectionPool::Instance();
  state->connection = pool.Connect(url.host, url.port);
  if (!state->connection) {
    result.winHttpErrorCode = GetLastError();
    result.error = ErrorText("WinHttpConnect", result.winHttpErrorCode);
    return result;
  }
  owner.request = WinHttpOpenRequest(static_cast<HINTERNET>(state->connection.get()), method,
      url.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
      url.scheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
  if (!owner.request) {
    result.winHttpErrorCode = GetLastError();
    result.error = ErrorText("WinHttpOpenRequest", result.winHttpErrorCode);
    return result;
  }
  g_liveRequestHandles.fetch_add(1, std::memory_order_relaxed);
  DWORD_PTR context = reinterpret_cast<DWORD_PTR>(state.get());
  if (!WinHttpSetOption(owner.request, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) ||
      WinHttpSetStatusCallback(owner.request, InteractiveStatusCallback,
          WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) ==
          WINHTTP_INVALID_STATUS_CALLBACK) {
    result.error = "callback_setup_failed";
    return result;
  }
  owner.callbackInstalled = true;
  // A signed query or POST body can carry credentials. Do not replay it to
  // another origin or downgrade HTTPS. Routes consume 3xx as an explicit error.
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  if (!WinHttpSetOption(owner.request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)) ||
      g_faultTimeoutSetup.load(std::memory_order_acquire) ||
      !WinHttpSetTimeouts(owner.request, budgetMs, budgetMs, budgetMs, budgetMs)) {
    result.error = "timeout_setup_failed";
    return result;
  }

  auto interrupted = [&]() {
    if (HttpRouteDeadlineExpired()) {
      result.error = "total_budget_exhausted";
      result.timedOut = true;
      return true;
    }
    if (IsCancelled(cancelled)) {
      result.error = "cancelled";
      result.timedOut = true;
      return true;
    }
    if (outerDeadline->load(std::memory_order_acquire) || std::chrono::steady_clock::now() >= deadline) {
      result.error = "total_request_timeout";
      result.winHttpErrorCode = ERROR_WINHTTP_TIMEOUT;
      result.timedOut = true;
      return true;
    }
    return false;
  };
  DWORD completedBytes = 0;
  auto complete = [&](DWORD expected, auto issue, const char* phase) {
    if (interrupted()) return false;
    {
      std::lock_guard lock(state->mutex);
      state->expected = expected;
      state->completed = false;
      state->error = 0;
      state->bytes = 0;
    }
    // Do not hold the mutex: WinHTTP can complete synchronously and reenter
    // its callback on this same thread.
    if (!issue()) {
      result.winHttpErrorCode = GetLastError();
      result.error = ErrorText(phase, result.winHttpErrorCode);
      result.timedOut = result.winHttpErrorCode == ERROR_WINHTTP_TIMEOUT;
      return false;
    }
    std::unique_lock lock(state->mutex);
    while (!state->completed) {
      if (interrupted()) return false;
      state->cv.wait_until(lock, std::min(deadline, std::chrono::steady_clock::now() +
          std::chrono::milliseconds(20)));
    }
    if (interrupted()) return false;
    if (state->error) {
      result.winHttpErrorCode = state->error;
      result.error = ErrorText(phase, state->error);
      result.timedOut = state->error == ERROR_WINHTTP_TIMEOUT;
      return false;
    }
    completedBytes = state->bytes;
    return true;
  };

  auto perform = [&]() {
    if (!complete(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, [&] {
          return WinHttpSendRequest(owner.request,
              state->headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : state->headers.c_str(),
              static_cast<DWORD>(state->headers.size()),
              state->requestBody.empty() ? WINHTTP_NO_REQUEST_DATA : state->requestBody.data(),
              static_cast<DWORD>(state->requestBody.size()),
              static_cast<DWORD>(state->requestBody.size()), context);
        }, "WinHttpSendRequest")) return;
    if (!complete(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, [&] {
          return WinHttpReceiveResponse(owner.request, nullptr);
        }, "WinHttpReceiveResponse")) return;
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(owner.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
      result.winHttpErrorCode = GetLastError();
      result.error = ErrorText("WinHttpQueryHeaders", result.winHttpErrorCode);
      result.timedOut = result.winHttpErrorCode == ERROR_WINHTTP_TIMEOUT;
      return;
    }
    result.statusCode = status;
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      const auto location = RedirectLocationInfo(owner.request, url);
      std::ostringstream diagnostic;
      diagnostic << "http_redirect status=" << status
                 << " host=" << DiagnosticHost(url.host)
                 << " location_host=" << location.host
                 << " scheme_change=" << (location.schemeChanged ? 1 : 0)
                 << " port_change=" << (location.portChanged ? 1 : 0);
      echo::diagnostics::LogDebug("HttpClient", diagnostic.str());
    }
    DWORD length = 0;
    size = sizeof(length);
    const bool hasLength = WinHttpQueryHeaders(owner.request,
        WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
        &length, &size, WINHTTP_NO_HEADER_INDEX) != FALSE;
    if (hasLength && length > maxBodyBytes) {
      result.error = "max_body_exceeded";
      return;
    }
    for (;;) {
      if (!complete(WINHTTP_CALLBACK_STATUS_READ_COMPLETE, [&] {
            // Async reads MUST use a null bytes-read pointer. The 8 KiB buffer
            // stays in state; no recursive read calls from the callback.
            return WinHttpReadData(owner.request, state->readBuffer.data(),
                static_cast<DWORD>(state->readBuffer.size()), nullptr);
          }, "WinHttpReadData")) return;
      const auto bytes = completedBytes;
      if (bytes == 0) {
        if (hasLength && result.body.size() < length) result.error = "incomplete_body";
        return;
      }
      if (bytes > maxBodyBytes || result.body.size() > maxBodyBytes - bytes) {
        result.error = "max_body_exceeded";
        return;
      }
      result.body.append(state->readBuffer.data(), bytes);
    }
  };
  perform();
  if (!result.error.empty() && result.error != "max_body_exceeded")
    pool.Evict(url.host, url.port, state->connection);
  return result;
}

bool InteractiveTransient(const HttpResult& result) {
  if (result.error == "cancelled") return false;
  if (result.timedOut || result.error == "incomplete_body" ||
      result.error == "connection_closed_early" || result.error == "test_transient_failure") return true;
  switch (result.winHttpErrorCode) {
    case ERROR_WINHTTP_TIMEOUT:
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_CONNECTION_ERROR:
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
    case ERROR_WINHTTP_INVALID_SERVER_RESPONSE: return true;
    default: return false;  // no business/setup/quota/TLS validation retries
  }
}

}  // namespace

RequestTimeouts DeriveRequestTimeouts(long totalTimeoutMs) {
  RequestTimeouts t;
  if (totalTimeoutMs <= 0) return t;

  // WinHTTP applies resolve, connect and send STRICTLY IN SEQUENCE before any
  // response byte can arrive, so those three must sum to no more than the
  // caller's budget or the number the caller passed means nothing. The formulas
  // this replaces were total/2 + total/3 + total/3 = 7/6 of the budget, i.e. a
  // request could not fit inside its own nominal deadline even before resolve
  // was counted (resolve was not set per-request at all and inherited the 5000ms
  // session default, making the real worst case 12500ms at the 9000 default
  // against a 10000ms /song/url deadline).
  const long quarter = std::max<long>(totalTimeoutMs / 4, 100);
  t.resolve = std::min<long>(quarter, 5000);
  t.connect = std::min<long>(quarter, 6000);
  t.send = std::min<long>(quarter, 10000);

  // receive is deliberately left on its original formula. It bounds each
  // individual read rather than the whole response, so shrinking it would turn a
  // slow-but-progressing CDN read into a failure without buying any deadline
  // headroom in the pre-response prefix.
  t.receive =
      std::min<long>(std::max<long>(totalTimeoutMs / 3, 100), 10000);
  return t;
}

void CloseHttpConnectionPool() {
  HttpConnectionPool::Instance().CloseAll();
  AsyncRequestRegistry::Instance().Reap();
  // Normally final callbacks have already arrived. Give late close events a
  // bounded grace without ever freeing unproven contexts or blocking exit.
  const auto grace = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
  while (g_liveAsyncRequests.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < grace) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    AsyncRequestRegistry::Instance().Reap();
  }
}

long HttpClientLiveRequestHandleCount() {
  return g_liveRequestHandles.load(std::memory_order_relaxed);
}

std::size_t HttpClientCachedConnectionCount() {
  return HttpConnectionPool::Instance().CachedCount();
}

// Handing an attempt everything that is left lets one stall consume the retries
// the caller asked for, so the earlier attempts carry the reserve for the ones
// behind them. A slice is only taken when the leftovers can still fund a viable
// retry: below that the current attempt keeps the whole remainder, because an
// unusably short slice wastes the last chance instead of sharing it.
namespace {
// Reserve 1500ms for future attempts; admit a retry with 1000ms after real
// elapsed-time overhead so millisecond rounding/setup does not reject a slice
// that the shared-budget calculation intentionally reserved.
constexpr long kReservedGetAttemptMs = 1500;
constexpr long kMinimumGetAttemptBudgetMs = 1000;
}

long SingleAttemptCap(long left, int attempt, int maxAttempts, const long* backoffMs, long backoffCount) {
  if (left <= 0) return 0;
  if (attempt + 1 >= maxAttempts) return left;
  long reserve = 0;
  for (int next = attempt; next < maxAttempts - 1; ++next) {
    reserve += next < backoffCount ? backoffMs[next] : backoffMs[backoffCount - 1];
  }
  const long attemptsLeft = maxAttempts - attempt;
  const long reserveForRest = reserve + kReservedGetAttemptMs * (attemptsLeft - 1);
  if (left <= reserveForRest) return left;
  return left - reserveForRest;
}

long HttpClientLiveAsyncRequestCount() {
  AsyncRequestRegistry::Instance().Reap();
  return g_liveAsyncRequests.load(std::memory_order_acquire);
}

void HttpClientSetTimeoutSetupFaultForTest(bool enabled) {
  g_faultTimeoutSetup.store(enabled, std::memory_order_release);
}

void HttpClientSetTransientFaultForTest(bool enabled) {
  t_transientFaultForTest = enabled;
  if (enabled) t_transientFaultCountForTest = 0;
}

long HttpClientTransientFaultCountForTest() {
  return t_transientFaultCountForTest;
}

HttpResult HttpClient::Get(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers,
    long totalTimeoutMs,
    std::size_t maxBodyBytes,
    const std::atomic_bool* cancelled,
    bool retryAllowed) const {
  ParsedUrl parsed;
  if (!CrackUrl(ToWide(url), parsed)) {
    HttpResult r;
    r.error = LastErrorText("WinHttpCrackUrl");
    r.attemptCount = 0;
    return r;
  }
  // Bounded retry with shared budget: the totalTimeoutMs is the *entire*
  // budget across all attempts + backoff, not per-attempt. This prevents
  // retry from amplifying 9s into 27s+. GET only — unique retry owner.
  auto budgetStart = std::chrono::steady_clock::now();
  totalTimeoutMs = BoundToRouteBudget(totalTimeoutMs > 0 ? totalTimeoutMs : kHttpGetDefaultBudgetMs);
  InteractiveDeadline deadline(totalTimeoutMs);
  if (!deadline.armed) {
    HttpResult r;
    r.error = "watchdog_overload";
    r.attemptCount = 0;
    return r;
  }
  static const long backoffMs[] = {500, 2000};
  const int maxAttempts = retryAllowed ? 3 : 1;
  int attempts = 0;
  for (int attempt = 0; attempt < maxAttempts; ++attempt) {
    if (IsCancelled(cancelled) || HttpRouteDeadlineExpired()) {
      HttpResult r;
      SetStopReason(r, cancelled);
      r.attemptCount = attempts;
      r.elapsedMs = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - budgetStart).count());
      return r;
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - budgetStart).count();
    long remaining = totalTimeoutMs - static_cast<long>(elapsed);
    // Retry admission must use the same slice model as the attempts themselves.
    // A fraction of the original total budget rejects viable late attempts
    // after earlier attempts have already reserved their backoffs and slices.
    const long attemptBudget =
        SingleAttemptCap(remaining, attempt, maxAttempts, backoffMs, 2);
    if (attempt > 0 && attemptBudget < kMinimumGetAttemptBudgetMs) {
      HttpResult r;
      r.timedOut = true;
      r.error = "total_budget_exhausted";
      r.attemptCount = attempts;
      r.elapsedMs = static_cast<long>(elapsed);
      return r;
    }
    if (remaining <= 0) {
      HttpResult r;
      r.error = "total_budget_exhausted";
      r.timedOut = true;
      r.attemptCount = attempts;
      r.elapsedMs = static_cast<long>(elapsed);
      return r;
    }
    ++attempts;
    auto res = ExecuteInteractiveRequest(parsed, L"GET", headers, {}, false,
        attemptBudget, maxBodyBytes, cancelled, deadline.flag);
    AsyncRequestRegistry::Instance().Reap();
    res.attemptCount = attempts;
    res.elapsedMs = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - budgetStart).count());
    if (IsCancelled(cancelled) || HttpRouteDeadlineExpired()) {
      SetStopReason(res, cancelled);
      return res;
    }
    // Local admission/setup failures are not network conditions: return
    // them verbatim and immediately. Classifying them as transient used to
    // send them through the backoff schedule (review probe: 2527ms for a
    // 1000ms budget) while hammering an already-saturated watchdog.
    const bool admissionFailure = res.error == "watchdog_overload" ||
                                  res.error == "timeout_setup_failed";
    bool transient = !admissionFailure && InteractiveTransient(res);
    if (!transient || attempt + 1 == maxAttempts) return res;

    // Charge retry backoff to this request's total budget, not a fresh one.
    // Preserve the original transport failure when there is no room for both
    // the delay and a useful next attempt; do not sleep just to time out later.
    const long backoff = backoffMs[attempt];
    const auto elapsedAfterAttempt = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - budgetStart).count();
    const long remainingAfterAttempt =
        totalTimeoutMs - static_cast<long>(elapsedAfterAttempt);
    const long nextAttemptBudget = SingleAttemptCap(
        remainingAfterAttempt - backoff, attempt + 1, maxAttempts, backoffMs, 2);
    if (nextAttemptBudget < kMinimumGetAttemptBudgetMs) return res;
    const auto backoffEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds(backoff);
    while (!IsCancelled(cancelled) && std::chrono::steady_clock::now() < backoffEnd) {
      std::this_thread::sleep_until(std::min(backoffEnd,
          std::chrono::steady_clock::now() + std::chrono::milliseconds(20)));
    }
  }
  HttpResult r;
  r.error = "retry_exhausted";
  r.attemptCount = attempts;
  r.elapsedMs = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - budgetStart).count());
  return r;
}

HttpResult HttpClient::Post(
    const std::string& url,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& headers,
    long totalTimeoutMs,
    std::size_t maxBodyBytes,
    const std::atomic_bool* cancelled) const {
  const auto started = std::chrono::steady_clock::now();
  ParsedUrl parsed;
  if (!CrackUrl(ToWide(url), parsed)) {
    HttpResult r;
    r.error = LastErrorText("WinHttpCrackUrl");
    r.attemptCount = 0;
    return r;
  }
  // P1-F: Post is non-idempotent (e.g. /playhistory/upload). Single attempt only.
  if (IsCancelled(cancelled) || HttpRouteDeadlineExpired()) {
    HttpResult r;
    SetStopReason(r, cancelled);
    r.attemptCount = 0;
    return r;
  }
  const long remaining = BoundToRouteBudget(totalTimeoutMs > 0 ? totalTimeoutMs : 9000);
  InteractiveDeadline deadline(remaining);
  if (!deadline.armed || body.size() > MAXDWORD) {
    HttpResult r;
    r.error = !deadline.armed ? "watchdog_overload" : "request_body_too_large";
    r.attemptCount = 0;
    return r;
  }
  auto res = ExecuteInteractiveRequest(parsed, L"POST", headers, body, true,
      remaining, maxBodyBytes, cancelled, deadline.flag);
  AsyncRequestRegistry::Instance().Reap();
  res.attemptCount = 1;
  res.elapsedMs = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count());
  return res;
}

HttpResult HttpClient::GetInteractive(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers,
    long totalTimeoutMs, std::size_t maxBodyBytes,
    const std::atomic_bool* cancelled) const {
  const auto started = std::chrono::steady_clock::now();
  int attempts = 0;
  auto finish = [&](HttpResult result) {
    AsyncRequestRegistry::Instance().Reap();
    result.attemptCount = attempts;
    result.elapsedMs = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count());
    return result;
  };
  ParsedUrl parsed;
  if (!CrackUrl(ToWide(url), parsed)) {
    HttpResult result;
    result.error = LastErrorText("WinHttpCrackUrl");
    return finish(std::move(result));
  }
  const long budget = BoundToRouteBudget(totalTimeoutMs > 0 ? totalTimeoutMs : 4000);
  InteractiveDeadline deadline(budget);  // one admission entry for all attempts
  HttpResult result;
  if (!deadline.armed) {
    result.error = "watchdog_overload";
    return finish(std::move(result));
  }
  const int maxAttempts = budget > 500 ? 3 : 1;
  constexpr long backoff[] = {100, 200};
  auto remaining = [&] {
    return budget - static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count());
  };
  for (int attempt = 0; attempt < maxAttempts; ++attempt) {
    if (IsCancelled(cancelled) || HttpRouteDeadlineExpired()) {
      SetStopReason(result, cancelled);
      return finish(std::move(result));
    }
    const auto left = remaining();
    if (left <= 0 || deadline.flag->load(std::memory_order_acquire)) {
      result.error = "total_budget_exhausted";
      result.timedOut = true;
      return finish(std::move(result));
    }
    // Avoid mechanical 1/3 budget slicing that prematurely aborts 1.5s responses.
    // Reserve minimal headroom needed for remaining retries while giving current attempt ample budget.
    long reserve = 0;
    for (int next = attempt; next < maxAttempts - 1; ++next) reserve += backoff[next];
    long cap = left;
    if (attempt + 1 < maxAttempts) {
      const long minSubsequentReserve = reserve + 600L * (maxAttempts - 1 - attempt);
      if (left > minSubsequentReserve + 500L) {
        cap = left - minSubsequentReserve;
      } else {
        cap = std::min(left, std::max(100L, (left - reserve) / (maxAttempts - attempt)));
      }
    }
    ++attempts;
    result = ExecuteInteractiveRequest(parsed, L"GET", headers, {}, false, cap,
        maxBodyBytes, cancelled, deadline.flag);
    AsyncRequestRegistry::Instance().Reap();
    if (!InteractiveTransient(result) || attempt + 1 == maxAttempts) return finish(std::move(result));
    if (remaining() <= backoff[attempt] + 100) return finish(std::move(result));
    const auto backoffEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds(backoff[attempt]);
    while (std::chrono::steady_clock::now() < backoffEnd) {
      if (IsCancelled(cancelled) || HttpRouteDeadlineExpired()) {
        SetStopReason(result, cancelled);
        return finish(std::move(result));
      }
      std::this_thread::sleep_until(std::min(backoffEnd,
          std::chrono::steady_clock::now() + std::chrono::milliseconds(20)));
    }
  }
  return finish(std::move(result));
}

HttpResult HttpClient::PostInteractive(
    const std::string& url, const std::string& body,
    const std::unordered_map<std::string, std::string>& headers,
    long totalTimeoutMs, std::size_t maxBodyBytes,
    const std::atomic_bool* cancelled) const {
  const auto started = std::chrono::steady_clock::now();
  HttpResult result;
  ParsedUrl parsed;
  if (!CrackUrl(ToWide(url), parsed)) {
    result.error = LastErrorText("WinHttpCrackUrl");
    result.attemptCount = 0;
    return result;
  }
  if (IsCancelled(cancelled) || HttpRouteDeadlineExpired()) {
    SetStopReason(result, cancelled);
    result.attemptCount = 0;
    return result;
  }
  if (body.size() > MAXDWORD) {
    result.error = "request_body_too_large";
    result.attemptCount = 0;
    return result;
  }
  const long budget = BoundToRouteBudget(totalTimeoutMs > 0 ? totalTimeoutMs : 2000);
  InteractiveDeadline deadline(budget);
  if (!deadline.armed) {
    result.error = "watchdog_overload";
    result.attemptCount = 0;
    return result;
  }
  // POST is single shot, including /v6/priv_url. No service-layer retries.
  result = ExecuteInteractiveRequest(parsed, L"POST", headers, body, true, budget,
      maxBodyBytes, cancelled, deadline.flag);
  AsyncRequestRegistry::Instance().Reap();
  result.elapsedMs = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count());
  return result;
}

}  // namespace echo::core
