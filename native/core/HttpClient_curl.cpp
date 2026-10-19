#include "echo/core/HttpClient.h"

#include "echo/async/RequestWatchdog.h"
#include "echo/diagnostics/EchoDiagnostics.h"

#include <curl/curl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace echo::core {

namespace {

std::atomic<long> g_liveRequestHandles{0};
std::atomic<long> g_liveAsyncRequests{0};
std::atomic<bool> g_faultTimeoutSetup{false};
thread_local bool t_transientFaultForTest = false;
thread_local long t_transientFaultCountForTest = 0;
thread_local const std::atomic_bool* t_threadCancelled = nullptr;
thread_local const std::atomic_bool* t_callerCancelled = nullptr;
thread_local auto t_routeDeadline = std::chrono::steady_clock::time_point::max();

struct CurlGlobal {
  CurlGlobal() : initialized(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK) {}
  bool initialized;
};

CurlGlobal& GlobalCurl() {
  static auto* global = new CurlGlobal;
  return *global;
}

std::array<std::mutex, CURL_LOCK_DATA_LAST> g_shareLocks;
std::mutex g_shareMutex;
std::shared_ptr<CURLSH> g_share;

void ShareLock(CURL*, curl_lock_data data, curl_lock_access, void*) {
  const auto index = static_cast<std::size_t>(data);
  if (index < g_shareLocks.size()) g_shareLocks[index].lock();
}

void ShareUnlock(CURL*, curl_lock_data data, void*) {
  const auto index = static_cast<std::size_t>(data);
  if (index < g_shareLocks.size()) g_shareLocks[index].unlock();
}

std::shared_ptr<CURLSH> CreateShare() {
  CURLSH* share = curl_share_init();
  if (!share) return {};
  auto result = std::shared_ptr<CURLSH>(share, [](CURLSH* value) {
    if (value) curl_share_cleanup(value);
  });
  if (curl_share_setopt(share, CURLSHOPT_LOCKFUNC, ShareLock) != CURLSHE_OK ||
      curl_share_setopt(share, CURLSHOPT_UNLOCKFUNC, ShareUnlock) != CURLSHE_OK ||
      curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT) != CURLSHE_OK ||
      curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS) != CURLSHE_OK ||
      curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION) != CURLSHE_OK) {
    return {};
  }
  return result;
}

std::shared_ptr<CURLSH> SharedCurl() {
  if (!GlobalCurl().initialized) return {};
  std::lock_guard lock(g_shareMutex);
  if (!g_share) g_share = CreateShare();
  return g_share;
}

std::mutex g_connectionMutex;
std::unordered_map<std::string, std::uint64_t> g_connections;
std::uint64_t g_connectionSequence = 0;
constexpr std::size_t kMaxAsyncRequests = 128;

std::uint64_t CacheConnection(const std::string& key) {
  std::lock_guard lock(g_connectionMutex);
  const auto generation = ++g_connectionSequence;
  g_connections[key] = generation;
  if (g_connections.size() <= kHttpConnectionPoolCapacity) return generation;
  const auto oldest = std::min_element(g_connections.begin(), g_connections.end(),
      [](const auto& left, const auto& right) { return left.second < right.second; });
  if (oldest != g_connections.end()) g_connections.erase(oldest);
  return generation;
}

void EvictConnection(const std::string& key, std::uint64_t generation) {
  std::lock_guard lock(g_connectionMutex);
  const auto found = g_connections.find(key);
  if (found != g_connections.end() && found->second == generation) {
    g_connections.erase(found);
  }
}

class LiveRequest {
 public:
  static bool TryAcquire() {
    long current = g_liveAsyncRequests.load(std::memory_order_relaxed);
    while (current < static_cast<long>(kMaxAsyncRequests)) {
      if (g_liveAsyncRequests.compare_exchange_weak(
              current, current + 1, std::memory_order_acq_rel)) {
        return true;
      }
    }
    return false;
  }
  LiveRequest() {
    g_liveRequestHandles.fetch_add(1, std::memory_order_relaxed);
  }
  ~LiveRequest() {
    g_liveRequestHandles.fetch_sub(1, std::memory_order_relaxed);
    g_liveAsyncRequests.fetch_sub(1, std::memory_order_release);
  }
};

bool IsCancelled(const std::atomic_bool* cancelled) {
  if (execution::currentOperation.Reason() == execution::StopReason::Cancelled) return true;
  if (cancelled && cancelled->load(std::memory_order_acquire)) return true;
  if (t_threadCancelled && t_threadCancelled->load(std::memory_order_acquire)) return true;
  return t_callerCancelled && t_callerCancelled->load(std::memory_order_acquire);
}

bool StoppedForDeadline() {
  return HttpRouteDeadlineExpired();
}

void SetStopReason(HttpResult& result, const std::atomic_bool* cancelled) {
  if (StoppedForDeadline()) {
    result.error = "total_budget_exhausted";
    result.timedOut = true;
  } else if (IsCancelled(cancelled)) {
    result.error = "cancelled";
    result.timedOut = true;
  }
}

long BoundToRouteBudget(long requested) {
  const auto deadline = std::min(t_routeDeadline, execution::currentOperation.deadline);
  if (deadline == std::chrono::steady_clock::time_point::max()) return requested;
  const auto left = static_cast<long>(std::chrono::ceil<std::chrono::milliseconds>(
      deadline - std::chrono::steady_clock::now()).count());
  return std::min(requested, std::max(1L, left));
}

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

struct ParsedUrl {
  std::string value;
  std::string host;
  std::string scheme;
  std::string port;
  std::string connectionKey;
};

bool CrackUrl(const std::string& url, ParsedUrl& parsed) {
  CURLU* raw = curl_url();
  if (!raw) return false;
  const auto cleanup = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>(raw, curl_url_cleanup);
  if (curl_url_set(raw, CURLUPART_URL, url.c_str(), 0) != CURLUE_OK) return false;
  char* scheme = nullptr;
  char* host = nullptr;
  char* port = nullptr;
  const auto freePart = [](char* value) { if (value) curl_free(value); };
  if (curl_url_get(raw, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK ||
      curl_url_get(raw, CURLUPART_HOST, &host, 0) != CURLUE_OK) {
    freePart(scheme);
    freePart(host);
    return false;
  }
  curl_url_get(raw, CURLUPART_PORT, &port, CURLU_DEFAULT_PORT);
  parsed.scheme = Lower(scheme);
  parsed.host = Lower(host);
  parsed.value = url;
  parsed.port = port ? port : (parsed.scheme == "https" ? "443" : "80");
  parsed.connectionKey = parsed.scheme + "://" + parsed.host + ":" + parsed.port;
  freePart(scheme);
  freePart(host);
  freePart(port);
  return parsed.scheme == "http" || parsed.scheme == "https";
}

struct RequestState {
  std::size_t maxBodyBytes = 0;
  std::string body;
  std::string location;
  bool bodyTooLarge = false;
  bool cancelled = false;
  bool deadlineExpired = false;
  bool outerDeadlineExpired = false;
  const std::atomic_bool* callerCancelled = nullptr;
  const std::shared_ptr<std::atomic_bool>* outerDeadline = nullptr;
  std::chrono::steady_clock::time_point deadline;
};

size_t ReceiveBody(char* data, size_t size, size_t count, void* opaque) {
  auto& state = *static_cast<RequestState*>(opaque);
  if (count != 0 && size > SIZE_MAX / count) return 0;
  const std::size_t bytes = size * count;
  if (bytes > state.maxBodyBytes || state.body.size() > state.maxBodyBytes - bytes) {
    state.bodyTooLarge = true;
    return 0;
  }
  state.body.append(data, bytes);
  return bytes;
}

size_t ReceiveHeader(char* data, size_t size, size_t count, void* opaque) {
  auto& state = *static_cast<RequestState*>(opaque);
  if (count != 0 && size > SIZE_MAX / count) return 0;
  const std::size_t bytes = size * count;
  const std::string_view line(data, bytes);
  constexpr std::string_view prefix = "location:";
  if (line.size() >= prefix.size() &&
      Lower(std::string(line.substr(0, prefix.size()))) == prefix) {
    auto start = line.find_first_not_of(" \t", prefix.size());
    if (start != std::string_view::npos) {
      auto end = line.find_last_not_of("\r\n \t");
      state.location.assign(line.substr(start, end - start + 1));
    }
  }
  constexpr std::string_view contentLength = "content-length:";
  if (line.size() >= contentLength.size() &&
      Lower(std::string(line.substr(0, contentLength.size()))) == contentLength) {
    auto start = line.find_first_not_of(" \t", contentLength.size());
    if (start != std::string_view::npos) {
      try {
        const auto length = std::stoull(std::string(line.substr(start)));
        if (length > state.maxBodyBytes) {
          state.bodyTooLarge = true;
          return 0;
        }
      } catch (...) {
      }
    }
  }
  return bytes;
}

int TransferProgress(void* opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  auto& state = *static_cast<RequestState*>(opaque);
  if (HttpRouteDeadlineExpired()) {
    state.deadlineExpired = true;
    return 1;
  }
  if (IsCancelled(state.callerCancelled)) {
    state.cancelled = true;
    return 1;
  }
  if (state.outerDeadline && (*state.outerDeadline)->load(std::memory_order_acquire)) {
    state.outerDeadlineExpired = true;
    return 1;
  }
  if (std::chrono::steady_clock::now() >= state.deadline) {
    state.outerDeadlineExpired = true;
    return 1;
  }
  return 0;
}

struct SlistDeleter {
  void operator()(curl_slist* value) const { curl_slist_free_all(value); }
};

void LogRedirect(const std::string& location, const ParsedUrl& source, long status) {
  std::string targetHost = "unknown";
  std::string targetScheme = source.scheme;
  std::string targetPort = source.port;
  const bool safeLocation = !location.empty() &&
      std::all_of(location.begin(), location.end(), [](unsigned char c) {
        return c >= 0x20 && c != 0x7f && c != '\\';
      });
  bool supportedScheme = true;
  const auto colon = location.find(':');
  const auto separator = location.find_first_of("/?#");
  if (colon != std::string::npos &&
      (separator == std::string::npos || colon < separator)) {
    const auto scheme = Lower(location.substr(0, colon));
    supportedScheme = scheme == "http" || scheme == "https";
  }
  if (safeLocation && supportedScheme) {
    CURLU* raw = curl_url();
    if (raw) {
      const auto cleanup =
          std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>(raw, curl_url_cleanup);
      if (curl_url_set(raw, CURLUPART_URL, (source.scheme + "://" + source.host + ":" +
              source.port + "/").c_str(), 0) == CURLUE_OK &&
          curl_url_set(raw, CURLUPART_URL, location.c_str(), 0) == CURLUE_OK) {
        char* scheme = nullptr;
        char* host = nullptr;
        char* port = nullptr;
        const CURLUcode schemeResult = curl_url_get(raw, CURLUPART_SCHEME, &scheme, 0);
        const CURLUcode hostResult = curl_url_get(raw, CURLUPART_HOST, &host, 0);
        curl_url_get(raw, CURLUPART_PORT, &port, CURLU_DEFAULT_PORT);
        if (schemeResult == CURLUE_OK && hostResult == CURLUE_OK && scheme && host) {
          const std::string parsedScheme = Lower(scheme);
          const std::string parsedHost = Lower(host);
          const auto safeHost = [](const std::string& value) {
            return !value.empty() &&
                std::all_of(value.begin(), value.end(), [](unsigned char c) {
                  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ':' ||
                      c == '[' || c == ']';
                });
          };
          if ((parsedScheme == "http" || parsedScheme == "https") &&
              safeHost(parsedHost)) {
            targetScheme = parsedScheme;
            targetHost = parsedHost;
            targetPort = port ? port : (parsedScheme == "https" ? "443" : "80");
          }
        }
        if (scheme) curl_free(scheme);
        if (host) curl_free(host);
        if (port) curl_free(port);
      }
    }
  }
  std::ostringstream diagnostic;
  diagnostic << "http_redirect status=" << status
             << " host=" << source.host
             << " location_host=" << targetHost
             << " scheme_change=" << (targetScheme != source.scheme ? 1 : 0)
             << " port_change=" << (targetPort != source.port ? 1 : 0);
  echo::diagnostics::LogDebug("HttpClient", diagnostic.str());
}

HttpResult ExecuteInteractiveRequest(
    const ParsedUrl& url, const std::string& method,
    const std::unordered_map<std::string, std::string>& headers,
    const std::string& body, bool ensureJsonContentType, long budgetMs,
    std::size_t maxBodyBytes, const std::atomic_bool* cancelled,
    const std::shared_ptr<std::atomic_bool>& outerDeadline) {
  HttpResult result;
  if (t_transientFaultForTest) {
    ++t_transientFaultCountForTest;
    result.error = "test_transient_failure";
    return result;
  }
  if (!GlobalCurl().initialized || !SharedCurl()) {
    result.error = "curl_initialization_failed";
    return result;
  }
  if (!LiveRequest::TryAcquire()) {
    result.error = "http_callback_overload";
    return result;
  }
  LiveRequest active;
  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy(curl_easy_init(), curl_easy_cleanup);
  if (!easy) {
    result.error = "curl_easy_init_failed";
    return result;
  }
  auto share = SharedCurl();
  if (!share) {
    result.error = "curl_share_init_failed";
    return result;
  }
  if (g_faultTimeoutSetup.load(std::memory_order_acquire)) {
    result.error = "timeout_setup_failed";
    return result;
  }

  std::unique_ptr<curl_slist, SlistDeleter> headerList(nullptr);
  bool hasContentType = false;
  for (const auto& [key, value] : headers) {
    const std::string line = key + ": " + value;
    auto* updated = curl_slist_append(headerList.get(), line.c_str());
    if (!updated) {
      result.error = "curl_header_setup_failed";
      return result;
    }
    headerList.release();
    headerList.reset(updated);
    hasContentType |= Lower(key) == "content-type";
  }
  if (ensureJsonContentType && !hasContentType) {
    auto* updated = curl_slist_append(headerList.get(), "Content-Type: application/json");
    if (!updated) {
      result.error = "curl_header_setup_failed";
      return result;
    }
    headerList.release();
    headerList.reset(updated);
  }

  RequestState state;
  state.maxBodyBytes = maxBodyBytes;
  state.callerCancelled = cancelled;
  state.outerDeadline = &outerDeadline;
  state.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
  const long timeout = std::max(1L, budgetMs);
  const auto timeouts = DeriveRequestTimeouts(timeout);
  CURLcode setup = CURLE_OK;
  setup = curl_easy_setopt(easy.get(), CURLOPT_URL, url.value.c_str());
  if (setup == CURLE_OK) setup = curl_easy_setopt(easy.get(), CURLOPT_SHARE, share.get());
  if (setup == CURLE_OK) setup = curl_easy_setopt(easy.get(), CURLOPT_NOSIGNAL, 1L);
  if (setup == CURLE_OK) setup = curl_easy_setopt(
      easy.get(), CURLOPT_MAXCONNECTS, static_cast<long>(kHttpConnectionPoolCapacity));
  if (setup == CURLE_OK) setup = curl_easy_setopt(easy.get(), CURLOPT_TIMEOUT_MS, timeout);
  if (setup == CURLE_OK) setup = curl_easy_setopt(
      easy.get(), CURLOPT_CONNECTTIMEOUT_MS, std::min(timeout, std::max(1L, timeouts.connect)));
  if (setup == CURLE_OK) setup = curl_easy_setopt(easy.get(), CURLOPT_SSL_VERIFYPEER, 1L);
  if (setup == CURLE_OK) setup = curl_easy_setopt(easy.get(), CURLOPT_SSL_VERIFYHOST, 2L);
  if (setup == CURLE_OK) setup = curl_easy_setopt(easy.get(), CURLOPT_FOLLOWLOCATION, 0L);
  if (setup == CURLE_OK) setup = curl_easy_setopt(easy.get(), CURLOPT_PROTOCOLS,
      static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
  if (setup == CURLE_OK) setup = curl_easy_setopt(easy.get(), CURLOPT_WRITEFUNCTION, ReceiveBody);
  if (setup == CURLE_OK) setup = curl_easy_setopt(easy.get(), CURLOPT_WRITEDATA, &state);
  if (setup == CURLE_OK) setup = curl_easy_setopt(easy.get(), CURLOPT_HEADERFUNCTION, ReceiveHeader);
  if (setup == CURLE_OK) setup = curl_easy_setopt(easy.get(), CURLOPT_HEADERDATA, &state);
  if (setup == CURLE_OK) setup = curl_easy_setopt(easy.get(), CURLOPT_XFERINFOFUNCTION, TransferProgress);
  if (setup == CURLE_OK) setup = curl_easy_setopt(easy.get(), CURLOPT_XFERINFODATA, &state);
  if (setup == CURLE_OK) setup = curl_easy_setopt(easy.get(), CURLOPT_NOPROGRESS, 0L);
  if (setup == CURLE_OK && headerList)
    setup = curl_easy_setopt(easy.get(), CURLOPT_HTTPHEADER, headerList.get());
  if (setup == CURLE_OK && method == "POST") {
    setup = curl_easy_setopt(easy.get(), CURLOPT_POST, 1L);
    if (setup == CURLE_OK) setup = curl_easy_setopt(
        easy.get(), CURLOPT_POSTFIELDS, body.empty() ? "" : body.data());
    if (setup == CURLE_OK) setup = curl_easy_setopt(
        easy.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
  } else if (setup == CURLE_OK) {
    setup = curl_easy_setopt(easy.get(), CURLOPT_HTTPGET, 1L);
  }
  if (setup != CURLE_OK) {
    result.error = "timeout_setup_failed";
    return result;
  }

  const auto connectionGeneration = CacheConnection(url.connectionKey);
  const CURLcode transfer = curl_easy_perform(easy.get());
  curl_easy_getinfo(easy.get(), CURLINFO_RESPONSE_CODE, &result.statusCode);
  result.body = std::move(state.body);
  result.winHttpErrorCode = static_cast<unsigned long>(transfer);
  if (result.statusCode == 301 || result.statusCode == 302 || result.statusCode == 303 ||
      result.statusCode == 307 || result.statusCode == 308) {
    LogRedirect(state.location, url, result.statusCode);
  }
  if (state.bodyTooLarge) {
    result.error = "max_body_exceeded";
  } else if (state.deadlineExpired || HttpRouteDeadlineExpired()) {
    result.error = "total_budget_exhausted";
    result.timedOut = true;
  } else if (state.cancelled || IsCancelled(cancelled)) {
    result.error = "cancelled";
    result.timedOut = true;
  } else if (state.outerDeadlineExpired) {
    result.error = "total_request_timeout";
    result.winHttpErrorCode = static_cast<unsigned long>(CURLE_OPERATION_TIMEDOUT);
    result.timedOut = true;
  } else if (transfer == CURLE_OK) {
    result.error.clear();
  } else if (transfer == CURLE_OPERATION_TIMEDOUT) {
    result.error = "total_request_timeout";
    result.timedOut = true;
  } else if (transfer == CURLE_PARTIAL_FILE) {
    result.error = "incomplete_body";
  } else if (transfer == CURLE_GOT_NOTHING || transfer == CURLE_RECV_ERROR) {
    result.error = "connection_closed_early";
  } else {
    result.error = std::string("curl request failed: ") + curl_easy_strerror(transfer);
  }
  if (!result.error.empty() && result.error != "max_body_exceeded") {
    EvictConnection(url.connectionKey, connectionGeneration);
  }
  return result;
}

bool InteractiveTransient(const HttpResult& result) {
  if (result.error == "cancelled") return false;
  if (result.timedOut || result.error == "incomplete_body" ||
      result.error == "connection_closed_early" || result.error == "test_transient_failure") {
    return true;
  }
  switch (static_cast<CURLcode>(result.winHttpErrorCode)) {
    case CURLE_COULDNT_CONNECT:
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_RESOLVE_PROXY:
    case CURLE_RECV_ERROR:
    case CURLE_SEND_ERROR:
    case CURLE_GOT_NOTHING:
    case CURLE_PARTIAL_FILE:
    case CURLE_OPERATION_TIMEDOUT: return true;
    default: return false;
  }
}

struct InteractiveDeadline {
  std::shared_ptr<std::atomic_bool> flag = std::make_shared<std::atomic_bool>(false);
  bool armed = false;
  explicit InteractiveDeadline(long budget)
      : armed(budget > 0 && echo::async::RequestWatchdog::Instance().Arm(
            budget, flag, [deadline = flag] {
              deadline->store(true, std::memory_order_release);
            })) {}
  ~InteractiveDeadline() {
    if (armed) {
      try { echo::async::RequestWatchdog::Instance().Cancel(flag); } catch (...) {}
    }
  }
};

}  // namespace

HttpClientCancellationScope::HttpClientCancellationScope(
    const std::atomic_bool* cancelled, long remainingBudgetMs,
    const std::atomic_bool* callerCancelled)
    : previous_(t_threadCancelled), previousDeadline_(t_routeDeadline),
      previousCaller_(t_callerCancelled), previousOperation_(execution::CurrentOperation()) {
  t_threadCancelled = cancelled;
  t_callerCancelled = callerCancelled;
  if (remainingBudgetMs > 0) {
    t_routeDeadline = std::min(t_routeDeadline,
        std::chrono::steady_clock::now() + std::chrono::milliseconds(remainingBudgetMs));
  }
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

RequestTimeouts DeriveRequestTimeouts(long totalTimeoutMs) {
  RequestTimeouts timeouts;
  if (totalTimeoutMs <= 0) return timeouts;
  const long quarter = std::max<long>(totalTimeoutMs / 4, 100);
  timeouts.resolve = std::min<long>(quarter, 5000);
  timeouts.connect = std::min<long>(quarter, 6000);
  timeouts.send = std::min<long>(quarter, 10000);
  timeouts.receive = std::min<long>(std::max<long>(totalTimeoutMs / 3, 100), 10000);
  return timeouts;
}

void CloseHttpConnectionPool() {
  {
    std::lock_guard lock(g_connectionMutex);
    g_connections.clear();
  }
  {
    std::lock_guard lock(g_shareMutex);
    g_share.reset();
  }
  const auto grace = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
  while (g_liveAsyncRequests.load(std::memory_order_acquire) &&
         std::chrono::steady_clock::now() < grace) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

std::size_t HttpClientCachedConnectionCount() {
  std::lock_guard lock(g_connectionMutex);
  return g_connections.size();
}

long HttpClientLiveRequestHandleCount() {
  return g_liveRequestHandles.load(std::memory_order_relaxed);
}

long HttpClientLiveAsyncRequestCount() {
  return g_liveAsyncRequests.load(std::memory_order_acquire);
}

namespace {
constexpr long kReservedGetAttemptMs = 1500;
constexpr long kMinimumGetAttemptBudgetMs = 1000;
}

long SingleAttemptCap(long left, int attempt, int maxAttempts, const long* backoffMs,
                      long backoffCount) {
  if (left <= 0) return 0;
  if (attempt + 1 >= maxAttempts) return left;
  long reserve = 0;
  for (int next = attempt; next < maxAttempts - 1; ++next) {
    reserve += next < backoffCount ? backoffMs[next] : backoffMs[backoffCount - 1];
  }
  const long attemptsLeft = maxAttempts - attempt;
  const long reserveForRest = reserve + kReservedGetAttemptMs * (attemptsLeft - 1);
  return left <= reserveForRest ? left : left - reserveForRest;
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

HttpResult HttpClient::Get(const std::string& url,
                           const std::unordered_map<std::string, std::string>& headers,
                           long totalTimeoutMs, std::size_t maxBodyBytes,
                           const std::atomic_bool* cancelled, bool retryAllowed) const {
  const auto started = std::chrono::steady_clock::now();
  ParsedUrl parsed;
  if (!CrackUrl(url, parsed)) {
    HttpResult result;
    result.error = "invalid_url";
    result.attemptCount = 0;
    return result;
  }
  totalTimeoutMs = BoundToRouteBudget(
      totalTimeoutMs > 0 ? totalTimeoutMs : kHttpGetDefaultBudgetMs);
  InteractiveDeadline deadline(totalTimeoutMs);
  if (!deadline.armed) {
    HttpResult result;
    result.error = "watchdog_overload";
    result.attemptCount = 0;
    return result;
  }
  static constexpr long backoffMs[] = {500, 2000};
  const int maxAttempts = retryAllowed ? 3 : 1;
  int attempts = 0;
  for (int attempt = 0; attempt < maxAttempts; ++attempt) {
    if (IsCancelled(cancelled) || HttpRouteDeadlineExpired()) {
      HttpResult result;
      SetStopReason(result, cancelled);
      result.attemptCount = attempts;
      result.elapsedMs = static_cast<long>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - started).count());
      return result;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    const long remaining = totalTimeoutMs - static_cast<long>(elapsed);
    const long attemptBudget = SingleAttemptCap(remaining, attempt, maxAttempts, backoffMs, 2);
    if (attempt > 0 && attemptBudget < kMinimumGetAttemptBudgetMs) {
      HttpResult result;
      result.timedOut = true;
      result.error = "total_budget_exhausted";
      result.attemptCount = attempts;
      result.elapsedMs = static_cast<long>(elapsed);
      return result;
    }
    if (remaining <= 0) {
      HttpResult result;
      result.error = "total_budget_exhausted";
      result.timedOut = true;
      result.attemptCount = attempts;
      result.elapsedMs = static_cast<long>(elapsed);
      return result;
    }
    ++attempts;
    auto result = ExecuteInteractiveRequest(parsed, "GET", headers, {}, false,
        attemptBudget, maxBodyBytes, cancelled, deadline.flag);
    result.attemptCount = attempts;
    result.elapsedMs = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
    if (IsCancelled(cancelled) || HttpRouteDeadlineExpired()) {
      SetStopReason(result, cancelled);
      return result;
    }
    const bool admissionFailure = result.error == "watchdog_overload" ||
        result.error == "timeout_setup_failed";
    const bool transient = !admissionFailure && InteractiveTransient(result);
    if (!transient || attempt + 1 == maxAttempts) return result;
    const long backoff = backoffMs[attempt];
    const auto elapsedAfterAttempt = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    const long remainingAfterAttempt = totalTimeoutMs - static_cast<long>(elapsedAfterAttempt);
    const long nextAttemptBudget = SingleAttemptCap(
        remainingAfterAttempt - backoff, attempt + 1, maxAttempts, backoffMs, 2);
    if (nextAttemptBudget < kMinimumGetAttemptBudgetMs) return result;
    const auto backoffEnd = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(backoff);
    while (!IsCancelled(cancelled) && std::chrono::steady_clock::now() < backoffEnd) {
      std::this_thread::sleep_until(std::min(backoffEnd, std::chrono::steady_clock::now() +
          std::chrono::milliseconds(20)));
    }
  }
  HttpResult result;
  result.error = "retry_exhausted";
  result.attemptCount = attempts;
  result.elapsedMs = static_cast<long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started).count());
  return result;
}

HttpResult HttpClient::Post(const std::string& url, const std::string& body,
                            const std::unordered_map<std::string, std::string>& headers,
                            long totalTimeoutMs, std::size_t maxBodyBytes,
                            const std::atomic_bool* cancelled) const {
  const auto started = std::chrono::steady_clock::now();
  ParsedUrl parsed;
  if (!CrackUrl(url, parsed)) {
    HttpResult result;
    result.error = "invalid_url";
    result.attemptCount = 0;
    return result;
  }
  if (body.size() > static_cast<std::size_t>(ULONG_MAX)) {
    HttpResult result;
    result.error = "request_body_too_large";
    result.attemptCount = 0;
    return result;
  }
  if (IsCancelled(cancelled) || HttpRouteDeadlineExpired()) {
    HttpResult result;
    SetStopReason(result, cancelled);
    result.attemptCount = 0;
    return result;
  }
  const long budget = BoundToRouteBudget(totalTimeoutMs > 0 ? totalTimeoutMs : 9000);
  InteractiveDeadline deadline(budget);
  if (!deadline.armed) {
    HttpResult result;
    result.error = "watchdog_overload";
    result.attemptCount = 0;
    return result;
  }
  auto result = ExecuteInteractiveRequest(parsed, "POST", headers, body, true,
      budget, maxBodyBytes, cancelled, deadline.flag);
  result.attemptCount = 1;
  result.elapsedMs = static_cast<long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started).count());
  return result;
}

HttpResult HttpClient::GetInteractive(const std::string& url,
                                      const std::unordered_map<std::string, std::string>& headers,
                                      long totalTimeoutMs, std::size_t maxBodyBytes,
                                      const std::atomic_bool* cancelled) const {
  const auto started = std::chrono::steady_clock::now();
  int attempts = 0;
  auto finish = [&](HttpResult result) {
    result.attemptCount = attempts;
    result.elapsedMs = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
    return result;
  };
  ParsedUrl parsed;
  if (!CrackUrl(url, parsed)) {
    HttpResult result;
    result.error = "invalid_url";
    return finish(std::move(result));
  }
  const long budget = BoundToRouteBudget(totalTimeoutMs > 0 ? totalTimeoutMs : 4000);
  InteractiveDeadline deadline(budget);
  if (!deadline.armed) {
    HttpResult result;
    result.error = "watchdog_overload";
    return finish(std::move(result));
  }
  const int maxAttempts = budget > 500 ? 3 : 1;
  constexpr long backoff[] = {100, 200};
  auto remaining = [&] {
    return budget - static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
  };
  HttpResult result;
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
    result = ExecuteInteractiveRequest(parsed, "GET", headers, {}, false, cap,
        maxBodyBytes, cancelled, deadline.flag);
    if (!InteractiveTransient(result) || attempt + 1 == maxAttempts)
      return finish(std::move(result));
    if (remaining() <= backoff[attempt] + 100) return finish(std::move(result));
    const auto backoffEnd = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(backoff[attempt]);
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
  ParsedUrl parsed;
  if (!CrackUrl(url, parsed)) {
    HttpResult result;
    result.error = "invalid_url";
    result.attemptCount = 0;
    return result;
  }
  if (IsCancelled(cancelled) || HttpRouteDeadlineExpired()) {
    HttpResult result;
    SetStopReason(result, cancelled);
    result.attemptCount = 0;
    return result;
  }
  if (body.size() > static_cast<std::size_t>(ULONG_MAX)) {
    HttpResult result;
    result.error = "request_body_too_large";
    result.attemptCount = 0;
    return result;
  }
  const long budget = BoundToRouteBudget(totalTimeoutMs > 0 ? totalTimeoutMs : 2000);
  InteractiveDeadline deadline(budget);
  if (!deadline.armed) {
    HttpResult result;
    result.error = "watchdog_overload";
    result.attemptCount = 0;
    return result;
  }
  auto result = ExecuteInteractiveRequest(parsed, "POST", headers, body, true,
      budget, maxBodyBytes, cancelled, deadline.flag);
  result.attemptCount = 1;
  result.elapsedMs = static_cast<long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started).count());
  return result;
}

}  // namespace echo::core
