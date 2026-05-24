#include "echo/core/HttpClient.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <sstream>
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

}  // namespace

HttpResult HttpClient::Get(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& headers) const {
  HttpResult result;
  const auto wideUrl = ToWide(url);

  URL_COMPONENTS components{};
  components.dwStructSize = sizeof(components);
  components.dwSchemeLength = static_cast<DWORD>(-1);
  components.dwHostNameLength = static_cast<DWORD>(-1);
  components.dwUrlPathLength = static_cast<DWORD>(-1);
  components.dwExtraInfoLength = static_cast<DWORD>(-1);

  if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components)) {
    result.error = LastErrorText("WinHttpCrackUrl");
    return result;
  }

  std::wstring host(components.lpszHostName, components.dwHostNameLength);
  std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
  if (components.dwExtraInfoLength > 0) {
    path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
  }

  HINTERNET session = WinHttpOpen(
      L"EchoMusicNative/0.1",
      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
      WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS,
      0);
  if (!session) {
    result.error = LastErrorText("WinHttpOpen");
    return result;
  }
  WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000);

  HINTERNET connect = WinHttpConnect(session, host.c_str(), components.nPort, 0);
  if (!connect) {
    result.error = LastErrorText("WinHttpConnect");
    WinHttpCloseHandle(session);
    return result;
  }

  const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET request = WinHttpOpenRequest(
      connect,
      L"GET",
      path.c_str(),
      nullptr,
      WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES,
      flags);
  if (!request) {
    result.error = LastErrorText("WinHttpOpenRequest");
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
  }

  // Kugou image/audio CDNs frequently bounce through short-lived 30x URLs.
  // Make redirect handling explicit so remote covers and signed media URLs do
  // not silently degrade to placeholders or playback errors.
  DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(
      request,
      WINHTTP_OPTION_REDIRECT_POLICY,
      &redirectPolicy,
      sizeof(redirectPolicy));

  std::wstring headerBlock;
  for (const auto& [key, value] : headers) {
    headerBlock += ToWide(key);
    headerBlock += L": ";
    headerBlock += ToWide(value);
    headerBlock += L"\r\n";
  }

  const wchar_t* headerPtr =
      headerBlock.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerBlock.c_str();
  const DWORD headerLength = headerBlock.empty() ? 0 : static_cast<DWORD>(headerBlock.size());

  if (!WinHttpSendRequest(
          request, headerPtr, headerLength, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request, nullptr)) {
    result.error = LastErrorText("WinHttpSendRequest/WinHttpReceiveResponse");
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
  }

  DWORD statusCode = 0;
  DWORD statusSize = sizeof(statusCode);
  if (WinHttpQueryHeaders(
          request,
          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX,
          &statusCode,
          &statusSize,
          WINHTTP_NO_HEADER_INDEX)) {
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

  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
  return result;
}

HttpResult HttpClient::Post(
    const std::string& url,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& headers) const {
  HttpResult result;
  const auto wideUrl = ToWide(url);

  URL_COMPONENTS components{};
  components.dwStructSize = sizeof(components);
  components.dwSchemeLength = static_cast<DWORD>(-1);
  components.dwHostNameLength = static_cast<DWORD>(-1);
  components.dwUrlPathLength = static_cast<DWORD>(-1);
  components.dwExtraInfoLength = static_cast<DWORD>(-1);

  if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components)) {
    result.error = LastErrorText("WinHttpCrackUrl");
    return result;
  }

  std::wstring host(components.lpszHostName, components.dwHostNameLength);
  std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
  if (components.dwExtraInfoLength > 0) {
    path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
  }

  HINTERNET session = WinHttpOpen(
      L"EchoMusicNative/0.1",
      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
      WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS,
      0);
  if (!session) {
    result.error = LastErrorText("WinHttpOpen");
    return result;
  }
  WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000);

  HINTERNET connect = WinHttpConnect(session, host.c_str(), components.nPort, 0);
  if (!connect) {
    result.error = LastErrorText("WinHttpConnect");
    WinHttpCloseHandle(session);
    return result;
  }

  const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET request = WinHttpOpenRequest(
      connect,
      L"POST",
      path.c_str(),
      nullptr,
      WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES,
      flags);
  if (!request) {
    result.error = LastErrorText("WinHttpOpenRequest");
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
  }

  DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(
      request,
      WINHTTP_OPTION_REDIRECT_POLICY,
      &redirectPolicy,
      sizeof(redirectPolicy));

  std::wstring headerBlock;
  bool hasContentType = false;
  for (const auto& [key, value] : headers) {
    headerBlock += ToWide(key);
    headerBlock += L": ";
    headerBlock += ToWide(value);
    headerBlock += L"\r\n";
    if (Lower(key) == "content-type") {
      hasContentType = true;
    }
  }
  if (!hasContentType) {
    headerBlock += L"Content-Type: application/json\r\n";
  }

  const wchar_t* headerPtr = headerBlock.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerBlock.c_str();
  const DWORD headerLength = headerBlock.empty() ? 0 : static_cast<DWORD>(headerBlock.size());

  void* postData = const_cast<char*>(body.data());
  DWORD postDataLength = static_cast<DWORD>(body.size());

  if (!WinHttpSendRequest(
          request, headerPtr, headerLength, postData, postDataLength, postDataLength, 0) ||
      !WinHttpReceiveResponse(request, nullptr)) {
    result.error = LastErrorText("WinHttpSendRequest/WinHttpReceiveResponse");
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
  }

  DWORD statusCode = 0;
  DWORD statusSize = sizeof(statusCode);
  if (WinHttpQueryHeaders(
          request,
          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX,
          &statusCode,
          &statusSize,
          WINHTTP_NO_HEADER_INDEX)) {
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

  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
  return result;
}

}  // namespace echo::core
