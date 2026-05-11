#include "echo/core/CompatApi.h"
#include "echo/diagnostics/MemorySnapshot.h"
#include "echo/storage/AppPaths.h"
#include "echo/storage/Database.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <cstdlib>

namespace echo::compat_server {
namespace {

struct WsaLifetime {
  WsaLifetime() {
    WSADATA data{};
    ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }
  ~WsaLifetime() {
    if (ok) WSACleanup();
  }
  bool ok = false;
};

std::string UrlDecode(std::string_view value) {
  std::string decoded;
  decoded.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      const auto hex = value.substr(i + 1, 2);
      char* end = nullptr;
      const auto code = static_cast<char>(std::strtol(std::string(hex).c_str(), &end, 16));
      decoded.push_back(code);
      i += 2;
    } else if (value[i] == '+') {
      decoded.push_back(' ');
    } else {
      decoded.push_back(value[i]);
    }
  }
  return decoded;
}

void ParseQuery(std::string_view queryString, echo::core::QueryMap& query) {
  while (!queryString.empty()) {
    const auto amp = queryString.find('&');
    const auto part = queryString.substr(0, amp);
    const auto eq = part.find('=');
    if (eq != std::string_view::npos) {
      query[UrlDecode(part.substr(0, eq))] = UrlDecode(part.substr(eq + 1));
    } else if (!part.empty()) {
      query[UrlDecode(part)] = "";
    }
    if (amp == std::string_view::npos) break;
    queryString.remove_prefix(amp + 1);
  }
}

std::string Trim(std::string value) {
  while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) {
    value.pop_back();
  }
  while (!value.empty() && value.front() == ' ') {
    value.erase(value.begin());
  }
  return value;
}

bool ParseRequest(
    const std::string& raw,
    std::string& method,
    std::string& path,
    echo::core::QueryMap& query,
    echo::core::HeaderMap& headers) {
  std::istringstream stream(raw);
  std::string requestLine;
  if (!std::getline(stream, requestLine)) return false;

  std::istringstream requestLineStream(requestLine);
  std::string target;
  requestLineStream >> method >> target;
  if (method.empty() || target.empty()) return false;

  const auto queryStart = target.find('?');
  path = queryStart == std::string::npos ? target : target.substr(0, queryStart);
  if (queryStart != std::string::npos) {
    ParseQuery(std::string_view(target).substr(queryStart + 1), query);
  }

  std::string line;
  while (std::getline(stream, line)) {
    if (line == "\r" || line.empty()) break;
    const auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    headers[Trim(line.substr(0, colon))] = Trim(line.substr(colon + 1));
  }

  return true;
}

void SendResponse(SOCKET client, const echo::core::CompatResponse& response) {
  const auto body = response.body.dump();
  const char* reason = response.httpStatus >= 500 ? "Not Implemented"
      : response.httpStatus >= 400                  ? "Bad Request"
      : response.httpStatus == 204                  ? "No Content"
                                                   : "OK";
  std::ostringstream stream;
  stream << "HTTP/1.1 " << response.httpStatus << ' ' << reason << "\r\n"
         << "Content-Type: " << response.contentType << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Connection: close\r\n"
         << "Access-Control-Allow-Origin: *\r\n"
         << "Access-Control-Allow-Headers: Authorization, Content-Type\r\n"
         << "\r\n"
         << body;
  const auto payload = stream.str();
  send(client, payload.data(), static_cast<int>(payload.size()), 0);
}

echo::core::CompatResponse DiagnosticsMemoryResponse() {
  echo::diagnostics::MemorySnapshotProvider provider;
  const auto snapshot = provider.Capture(0, 0, "Idle");
  return echo::core::CompatResponse{
      200,
      "application/json; charset=utf-8",
      {
          {"status", 1},
          {"data",
           {
               {"working_set_bytes", snapshot.workingSetBytes},
               {"private_bytes", snapshot.privateBytes},
               {"image_cache_bytes", snapshot.imageCacheBytes},
               {"pending_task_count", snapshot.pendingTaskCount},
               {"playback_state", snapshot.playbackState},
               {"text", echo::diagnostics::FormatMemorySnapshot(snapshot)},
           }},
      }};
}

}  // namespace

int RunCompatServer(const char* host, int port) {
  WsaLifetime wsa;
  if (!wsa.ok) {
    std::cerr << "WSAStartup failed\n";
    return 1;
  }

  std::cout << "Opening native storage\n" << std::flush;
  echo::storage::Database database;
  const auto databasePath = echo::storage::GetDefaultDatabasePath();
  std::cout << "Native storage path: " << databasePath.string() << '\n' << std::flush;
  database.Open(databasePath);
  std::cout << "Native storage opened\n" << std::flush;
  database.Initialize();
  std::cout << "Native storage initialized\n" << std::flush;
  echo::core::CompatApi api(database);
  std::cout << "Native storage ready\n" << std::flush;
  std::cout << echo::diagnostics::FormatMemorySnapshot(
                   echo::diagnostics::MemorySnapshotProvider{}.Capture(0, 0, "Idle"))
            << '\n'
            << std::flush;

  SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (server == INVALID_SOCKET) {
    std::cerr << "socket failed\n";
    return 1;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<unsigned short>(port));
  inet_pton(AF_INET, host, &address.sin_addr);

  BOOL reuse = TRUE;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

  if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
      listen(server, SOMAXCONN) == SOCKET_ERROR) {
    std::cerr << "bind/listen failed on " << host << ':' << port << '\n';
    closesocket(server);
    return 1;
  }

  std::cout << "EchoCompatServer listening at http://" << host << ':' << port << '\n' << std::flush;

  for (;;) {
    SOCKET client = accept(server, nullptr, nullptr);
    if (client == INVALID_SOCKET) continue;

    std::string raw;
    DWORD timeoutMs = 2000;
    setsockopt(
        client,
        SOL_SOCKET,
        SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeoutMs),
        sizeof(timeoutMs));

    char buffer[4096]{};
    for (;;) {
      const int received = recv(client, buffer, sizeof(buffer), 0);
      if (received <= 0) break;
      raw.append(buffer, buffer + received);
      if (raw.find("\r\n\r\n") != std::string::npos || raw.size() > 64 * 1024) break;
    }

    std::string method;
    std::string path;
    echo::core::QueryMap query;
    echo::core::HeaderMap headers;

    if (ParseRequest(raw, method, path, query, headers)) {
      if (method == "OPTIONS") {
        SendResponse(
            client,
            echo::core::CompatResponse{204, "application/json; charset=utf-8", nlohmann::json::object()});
      } else {
        SendResponse(client, path == "/diagnostics/memory"
                                 ? DiagnosticsMemoryResponse()
                                 : api.Handle(method, path, query, headers));
      }
    } else {
      SendResponse(
          client,
          echo::core::CompatResponse{
              400,
              "application/json; charset=utf-8",
              {{"status", 0}, {"error_code", 400}, {"error", "Bad request"}}});
    }

    shutdown(client, SD_BOTH);
    closesocket(client);
  }
}

}  // namespace echo::compat_server
