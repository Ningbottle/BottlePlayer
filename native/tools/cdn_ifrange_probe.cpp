// R-external (agent B, 2026-09-28): read-only real-CDN probe for If-Range /
// range-request behavior that the B05 resume design depends on.
//
// Safety protocol (per audit handoff):
//   - One GET /song/url resolution (the read-only route the app itself calls
//     before playback) using a hash taken from the ALREADY-COPIED local
//     history capture; then at most THREE CDN range requests, each reading
//     <=100 bytes of body.
//   - The tokenized CDN URL and any credential-like values are never printed
//     or written to the sanitized capture; they stay in the work dir only.
//   - Everything is fail-closed: any upstream/CDN error is recorded and the
//     probe stops (no retries, no write operations).
//
// Usage: cdn_ifrange_probe.exe <work_dir_with_raw-page1.json> <sanitized_out.json>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <windows.h>
#include <winhttp.h>

#include "echo/core/C_API.h"

#pragma comment(lib, "winhttp.lib")

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string ReadCResponse(char* raw) {
  std::string s = raw ? raw : "";
  if (raw) EchoFreeString(raw);
  return s;
}

void FindUrlStrings(const json& node, const std::string& prefix,
                    std::vector<std::pair<std::string, std::string>>& out) {
  if (node.is_object()) {
    for (auto it = node.begin(); it != node.end(); ++it) {
      FindUrlStrings(it.value(), it.key(), out);
    }
  } else if (node.is_array()) {
    for (std::size_t i = 0; i < node.size() && i < 4; ++i) {
      FindUrlStrings(node[i], prefix, out);
    }
  } else if (node.is_string()) {
    std::string key = prefix;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    if (key.find("url") != std::string::npos) {
      out.emplace_back(prefix, node.get<std::string>());
    }
  }
}

std::string Lower(std::string v) {
  std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return v;
}

// One range GET. Returns status + selected headers; body is read (up to 100
// bytes) then discarded — the sanitized record never includes body content.
struct RangeResult {
  long status = 0;
  std::string contentRange;
  std::string acceptRanges;
  std::string etag;
  std::string lastModified;
  std::string contentType;
  long long contentLength = -1;
  std::size_t bodyBytesRead = 0;
  std::string error;
};

RangeResult RangeGet(const std::wstring& host, INTERNET_PORT port,
                     const std::wstring& path, bool isHttps,
                     const std::string& rangeHeader,
                     const std::string& ifRangeHeader) {
  RangeResult r;
  HINTERNET session = WinHttpOpen(L"BottleMusic-IfRangeProbe/1.0",
                                  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    r.error = "WinHttpOpen failed";
    return r;
  }
  auto closeSession = [&] { WinHttpCloseHandle(session); };
  HINTERNET connect = WinHttpConnect(session, host.c_str(), port, 0);
  if (!connect) {
    r.error = "WinHttpConnect failed";
    closeSession();
    return r;
  }
  HINTERNET request = WinHttpOpenRequest(
      connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, isHttps ? WINHTTP_FLAG_SECURE : 0);
  if (!request) {
    r.error = "WinHttpOpenRequest failed";
    WinHttpCloseHandle(connect);
    closeSession();
    return r;
  }
  std::wstring headers;
  if (!rangeHeader.empty()) {
    headers += L"Range: " + std::wstring(rangeHeader.begin(),
                                         rangeHeader.end()) + L"\r\n";
  }
  if (!ifRangeHeader.empty()) {
    headers += L"If-Range: " + std::wstring(ifRangeHeader.begin(),
                                            ifRangeHeader.end()) + L"\r\n";
  }
  if (!headers.empty()) {
    if (!WinHttpAddRequestHeaders(request, headers.c_str(),
                                  static_cast<DWORD>(-1L),
                                  WINHTTP_ADDREQ_FLAG_ADD)) {
      r.error = "WinHttpAddRequestHeaders failed";
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      closeSession();
      return r;
    }
  }
  if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request, nullptr)) {
    r.error = "send/receive failed";
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    closeSession();
    return r;
  }
  DWORD status = 0, size = sizeof(status);
  WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                      WINHTTP_NO_HEADER_INDEX);
  r.status = static_cast<long>(status);

  // Named header queries: WINHTTP_QUERY_CUSTOM requires the header NAME as
  // lpHeaderName (WINHTTP_HEADER_NAME_BY_INDEX only applies to index queries
  // — the first draft passed 0 here and read nothing).
  struct HeaderQuery {
    const wchar_t* name;
    std::string* out;
  } queries[] = {
      {L"Content-Range", &r.contentRange},
      {L"Accept-Ranges", &r.acceptRanges},
      {L"ETag", &r.etag},
      {L"Last-Modified", &r.lastModified},
      {L"Content-Type", &r.contentType},
  };
  for (auto& q : queries) {
    wchar_t wbuf[1024] = {};
    DWORD bufSize = sizeof(wbuf) - sizeof(wchar_t);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, q.name, wbuf,
                            &bufSize, WINHTTP_NO_HEADER_INDEX) &&
        bufSize > 0) {
      *q.out = std::string(wbuf, wbuf + bufSize / sizeof(wchar_t));
    }
  }
  // Content-Length via number query (string query can fail on some CDNs).
  {
    DWORD len = 0, lenSize = sizeof(len);
    if (WinHttpQueryHeaders(request,
                            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &len, &lenSize,
                            WINHTTP_NO_HEADER_INDEX)) {
      r.contentLength = static_cast<long long>(len);
    }
  }
  // Read at most 100 body bytes, discard content.
  char body[100];
  DWORD total = 0;
  for (;;) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
    DWORD toRead = std::min<DWORD>(available, sizeof(body) - total);
    if (toRead == 0) break;
    DWORD read = 0;
    if (!WinHttpReadData(request, body + total, toRead, &read) || read == 0) break;
    total += read;
  }
  r.bodyBytesRead = total;
  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connect);
  closeSession();
  return r;
}

json RangeToReport(const RangeResult& r) {
  json j;
  j["status"] = r.status;
  j["content_range"] = r.contentRange.empty() ? json(nullptr) : json(r.contentRange);
  j["accept_ranges"] = r.acceptRanges.empty() ? json(nullptr) : json(r.acceptRanges);
  j["has_etag"] = !r.etag.empty();
  j["has_last_modified"] = !r.lastModified.empty();
  j["content_length"] = r.contentLength;
  j["content_type"] = r.contentType.empty() ? json(nullptr) : json(r.contentType);
  j["body_bytes_read"] = r.bodyBytesRead;
  if (!r.error.empty()) j["error"] = r.error;
  return j;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: cdn_ifrange_probe.exe <work_dir_with_raw-page1.json> <sanitized_out.json> [explicit_hash_hex]\n";
    return 2;
  }
  const fs::path workDir = argv[1];
  const fs::path sanitizedOut = argv[2];
  std::string explicitHash = argc > 3 ? argv[3] : "";

  std::string hash = explicitHash;
  if (hash.empty()) {
    std::ifstream rawIn(workDir / "raw-page1.json");
    if (!rawIn) {
      std::cerr << "raw-page1.json not found in work dir\n";
      return 2;
    }
    json page1 = json::parse(rawIn, nullptr, false);
    if (page1.is_object() && page1.contains("body")) {
      const json& data = page1["body"]["data"];
      if (data.contains("songs") && !data["songs"].empty() &&
          data["songs"][0].contains("info")) {
        hash = data["songs"][0]["info"].value("hash", "");
      }
    }
  }
  if (hash.empty()) {
    std::cerr << "no hash found in history capture\n";
    return 2;
  }

  json report;
  report["probe"] = "cdn_ifrange_probe";
  report["readonly"] = true;
  report["hash_redacted"] = "<redacted>";

  // Own process: initialize against the ALREADY-COPIED database in the work
  // dir (never the original app-data location).
  if (EchoInitializeWithPathsV2(workDir.string().c_str()) != 0) {
    report["init"] = "failed";
    std::ofstream out(sanitizedOut);
    out << report.dump(2);
    return 1;
  }

  // Step 1: resolve a play URL through the production read-only route.
  const std::string query = "{\"hash\":\"" + hash + "\"}";
  char* rawResp = nullptr;
  EchoHandleRequest("GET", "/song/url", query.c_str(), "{}", "", &rawResp);
  const std::string resp = ReadCResponse(rawResp);
  {
    std::ofstream raw(workDir / "raw-songurl.json");
    raw << resp;
  }
  json r = json::parse(resp, nullptr, false);
  json songUrlReport;
  songUrlReport["http_status"] = r.value("status", -1);
  std::string playUrl;
  if (r.is_object() && r.contains("body") && r["body"].is_object()) {
    const json& body = r["body"];
    songUrlReport["upstream_status"] = body.value("status", -1);
    if (body.contains("error_code")) songUrlReport["upstream_error_code"] = body["error_code"];
    std::vector<std::pair<std::string, std::string>> urls;
    FindUrlStrings(body, "", urls);
    json urlKeys = json::array();
    for (auto& [key, val] : urls) {
      urlKeys.push_back(key);
      if (playUrl.empty() && val.rfind("http", 0) == 0) playUrl = val;
    }
    songUrlReport["url_field_names"] = urlKeys;  // names only, values redacted
    songUrlReport["url_chosen_field"] = playUrl.empty() ? "(none)" : "(first http url)";
  }
  report["song_url_resolution"] = songUrlReport;

  if (playUrl.empty()) {
    report["cdn"] = "stopped: no play url resolved (fail-closed)";
    (void)EchoShutdown();
    std::ofstream out(sanitizedOut);
    out << report.dump(2);
    std::cout << "[probe] no play url resolved; sanitized capture written\n";
    return 1;
  }

  // Keep the tokenized URL in the work dir only.
  {
    std::ofstream urlFile(workDir / "play-url.txt");
    urlFile << playUrl;
  }

  // Parse URL crudely: scheme://host[:port]/path?query
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  uc.dwSchemeLength = (DWORD)-1;
  uc.dwHostNameLength = (DWORD)-1;
  uc.dwUrlPathLength = (DWORD)-1;
  uc.dwExtraInfoLength = (DWORD)-1;
  const std::wstring wideUrl(playUrl.begin(), playUrl.end());
  if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &uc)) {
    report["cdn"] = "stopped: url parse failed";
    std::ofstream out(sanitizedOut);
    out << report.dump(2);
    return 1;
  }
  std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
  std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
  if (uc.dwExtraInfoLength > 0) {
    path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
  }
  const bool isHttps = uc.nScheme == INTERNET_SCHEME_HTTPS;

  // Request A: plain range request.
  RangeResult a = RangeGet(host, uc.nPort, path, isHttps, "bytes=0-99", "");
  report["range_plain"] = RangeToReport(a);

  // Request B: If-Range with the validator from A (the B05 retry contract).
  const std::string validator = !a.etag.empty() ? a.etag : a.lastModified;
  report["validator_kind"] = !a.etag.empty() ? "etag" : (!a.lastModified.empty() ? "last_modified" : "none");
  if (!validator.empty()) {
    RangeResult b = RangeGet(host, uc.nPort, path, isHttps, "bytes=0-99", validator);
    report["range_ifrange_valid"] = RangeToReport(b);

    // Request C: If-Range with a deliberately stale validator — an
    // If-Range-honoring CDN must answer 200 (full) instead of 206.
    RangeResult c = RangeGet(host, uc.nPort, path, isHttps, "bytes=0-99",
                             "\"stale-validator-probe-000\"");
    report["range_ifrange_stale"] = RangeToReport(c);
  } else {
    report["range_ifrange_valid"] = "skipped: CDN provides no ETag/Last-Modified validator";
    report["range_ifrange_stale"] = "skipped: no validator to test against";
  }

  report["range_ifrange_valid"] =
      report.value("range_ifrange_valid", json(nullptr));

  (void)EchoShutdown();
  std::ofstream out(sanitizedOut);
  out << report.dump(2);
  std::cout << "[probe] sanitized capture written to " << sanitizedOut.string()
            << "\n[probe] raw songurl + url retained in " << workDir.string()
            << "\n";
  return 0;
}
