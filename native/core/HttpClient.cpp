#include "echo/core/HttpClient.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
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
namespace {

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

  // CDN 30x 跳转必须显式跟随，否则封面/签名媒体 URL 会静默退化为占位/播放失败。
  DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

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
    result.error = LastErrorText("WinHttpSendRequest/WinHttpReceiveResponse");
    WinHttpCloseHandle(request);
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

  WinHttpCloseHandle(request);  // 仅关 request；connect/session 由池管理
  return result;
}

}  // namespace

void CloseHttpConnectionPool() {
  HttpConnectionPool::Instance().CloseAll();
}

HttpResult HttpClient::Get(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers,
    long totalTimeoutMs,
    std::size_t maxBodyBytes) const {
  ParsedUrl parsed;
  if (!CrackUrl(ToWide(url), parsed)) {
    HttpResult r;
    r.error = LastErrorText("WinHttpCrackUrl");
    return r;
  }
  // Bounded retry: up to 2 retries on transient failures (timeout/connection
  // reset) with exponential backoff 500ms / 2s. No retry on 4xx.
  static const long backoffMs[] = {500, 2000};
  for (int attempt = 0; attempt <= 2; ++attempt) {
    auto res = ExecuteRequest(parsed, L"GET", headers, nullptr, 0,
                              /*ensureJsonContentType=*/false,
                              totalTimeoutMs, maxBodyBytes);
    bool transient = res.timedOut ||
                     (!res.error.empty() && res.statusCode == 0);
    if (!transient || attempt == 2) return res;
    std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs[attempt]));
  }
  return ExecuteRequest(parsed, L"GET", headers, nullptr, 0,
                        /*ensureJsonContentType=*/false,
                        totalTimeoutMs, maxBodyBytes);
}

HttpResult HttpClient::Post(
    const std::string& url,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& headers,
    long totalTimeoutMs,
    std::size_t maxBodyBytes) const {
  ParsedUrl parsed;
  if (!CrackUrl(ToWide(url), parsed)) {
    HttpResult r;
    r.error = LastErrorText("WinHttpCrackUrl");
    return r;
  }
  static const long backoffMs[] = {500, 2000};
  for (int attempt = 0; attempt <= 2; ++attempt) {
    auto res = ExecuteRequest(
        parsed, L"POST", headers, body.data(), static_cast<DWORD>(body.size()),
        /*ensureJsonContentType=*/true,
        totalTimeoutMs, maxBodyBytes);
    bool transient = res.timedOut ||
                     (!res.error.empty() && res.statusCode == 0);
    if (!transient || attempt == 2) return res;
    std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs[attempt]));
  }
  return ExecuteRequest(
      parsed, L"POST", headers, body.data(), static_cast<DWORD>(body.size()),
      /*ensureJsonContentType=*/true,
      totalTimeoutMs, maxBodyBytes);
}

}  // namespace echo::core
