#include "echo/core/HttpClient.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <sstream>
#include <string>
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

class HttpConnectionPool {
 public:
  // 返回进程级共享 session（首次调用惰性创建）。connect 池依附于该 session。
  HINTERNET Session() {
    std::call_once(session_once_, [this] {
      session_ = WinHttpOpen(
          L"EchoMusicNative/0.1",
          WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
          WINHTTP_NO_PROXY_NAME,
          WINHTTP_NO_PROXY_BYPASS,
          0);
      if (session_) {
        // 进程级默认超时：解析 5s / 连接 5s / 发送 10s / 接收 10s。
        WinHttpSetTimeouts(session_, 5000, 5000, 10000, 10000);
      }
    });
    return session_;
  }

  // 取（或创建并缓存）指定 host:port 的 connect 句柄。失败返回 nullptr。
  HINTERNET Connect(const std::wstring& host, INTERNET_PORT port) {
    if (!Session()) return nullptr;
    const std::wstring key = host + L":" + std::to_wstring(port);
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = connects_.find(key);
    if (it != connects_.end() && it->second) return it->second;

    HINTERNET connect = WinHttpConnect(Session(), host.c_str(), port, 0);
    if (!connect) return nullptr;
    connects_[key] = connect;  // 缓存；后续同 host 请求复用，WinHTTP 自动 keep-alive
    return connect;
  }

  ~HttpConnectionPool() {
    // 析构顺序：先 connect 后 session（WinHTTP 要求子句柄先于父句柄关闭）。
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [_, h] : connects_) {
      if (h) WinHttpCloseHandle(h);
    }
    connects_.clear();
    if (session_) {
      WinHttpCloseHandle(session_);
      session_ = nullptr;
    }
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

  std::once_flag session_once_;
  HINTERNET session_ = nullptr;
  std::unordered_map<std::wstring, HINTERNET> connects_;
  std::mutex mutex_;  // 保护 connects_（session_ 由 call_once 保护）
};

// 一次请求的公共执行逻辑：Get/Post 共用。
// method 为 L"GET"/L"POST"；postBody/postLen 为空表示 GET。
HttpResult ExecuteRequest(
    const ParsedUrl& url,
    const wchar_t* method,
    const std::unordered_map<std::string, std::string>& headers,
    const void* postBody,
    DWORD postLen,
    bool ensureJsonContentType) {
  HttpResult result;
  auto& pool = HttpConnectionPool::Instance();

  HINTERNET connect = pool.Connect(url.host, url.port);
  if (!connect) {
    result.error = LastErrorText("WinHttpConnect");
    return result;
  }

  const DWORD flags = url.scheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET request = WinHttpOpenRequest(
      connect, method, url.path.c_str(), nullptr,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!request) {
    result.error = LastErrorText("WinHttpOpenRequest");
    return result;  // connect/session 由池持有，不在此关闭
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

HttpResult HttpClient::Get(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers) const {
  HttpResult result;
  ParsedUrl parsed;
  if (!CrackUrl(ToWide(url), parsed)) {
    result.error = LastErrorText("WinHttpCrackUrl");
    return result;
  }
  return ExecuteRequest(parsed, L"GET", headers, nullptr, 0, /*ensureJsonContentType=*/false);
}

HttpResult HttpClient::Post(
    const std::string& url,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& headers) const {
  HttpResult result;
  ParsedUrl parsed;
  if (!CrackUrl(ToWide(url), parsed)) {
    result.error = LastErrorText("WinHttpCrackUrl");
    return result;
  }
  return ExecuteRequest(
      parsed, L"POST", headers, body.data(), static_cast<DWORD>(body.size()),
      /*ensureJsonContentType=*/true);
}

}  // namespace echo::core
