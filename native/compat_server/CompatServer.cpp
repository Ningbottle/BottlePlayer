#include "echo/async/RequestScheduler.h"
#include "echo/core/CompatApi.h"
#include "echo/core/HttpUtils.h"
#include "echo/diagnostics/MemorySnapshot.h"
#include "echo/storage/AppPaths.h"
#include "echo/storage/Database.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

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

struct SocketGuard {
  explicit SocketGuard(SOCKET s = INVALID_SOCKET) : socket_(s) {}
  ~SocketGuard() {
    if (socket_ != INVALID_SOCKET) {
      closesocket(socket_);
    }
  }
  SOCKET socket_ = INVALID_SOCKET;
};

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

  SocketGuard serverGuard(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  SOCKET server = serverGuard.socket_;
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
    return 1;
  }

  std::cout << "EchoCompatServer listening at http://" << host << ':' << port << '\n' << std::flush;

  echo::async::RequestScheduler scheduler(4);

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
    std::string body;

    if (!echo::core::ParseHttpRequest(raw, method, path, query, headers)) {
      SendResponse(
          client,
          echo::core::CompatResponse{
              400,
              "application/json; charset=utf-8",
              {{"status", 0}, {"error_code", 400}, {"error", "Bad request"}}});
      shutdown(client, SD_BOTH);
      closesocket(client);
      continue;
    }

    size_t contentLength = 0;
    for (const auto& kv : headers) {
      if (kv.first == "content-length") {
        try {
          contentLength = std::stoull(kv.second);
        } catch (...) {
          contentLength = 0;
        }
      }
    }
    if (contentLength > 0) {
      size_t headerEnd = raw.find("\r\n\r\n");
      if (headerEnd != std::string::npos) {
        headerEnd += 4;
        if (raw.size() > headerEnd) {
          body = raw.substr(headerEnd);
        }
        while (body.size() < contentLength) {
          const int received = recv(client, buffer, sizeof(buffer), 0);
          if (received <= 0) break;
          body.append(buffer, buffer + received);
        }
      }
    }

    if (method == "OPTIONS") {
      SendResponse(
          client,
          echo::core::CompatResponse{204, "application/json; charset=utf-8", nlohmann::json::object()});
      shutdown(client, SD_BOTH);
      closesocket(client);
      continue;
    }

    // Fast routes: synchronous so they never block on slow workers.
    if (path == "/health" || path == "/server/now" || path == "/diagnostics/memory") {
      auto response = path == "/diagnostics/memory"
                          ? DiagnosticsMemoryResponse()
                          : api.Handle(method, path, query, headers, body);
      SendResponse(client, response);
      shutdown(client, SD_BOTH);
      closesocket(client);
      continue;
    }

    // Slow routes: offloaded to RequestScheduler so accept loop stays free.
    if (path == "/song/url") {
      scheduler.SubmitLatestDetached(
          echo::async::RequestKind::SongUrl,
          [client, method, path, query, headers, body, &api](echo::async::CancellationToken) {
            auto response = api.Handle(method, path, query, headers, body);
            SendResponse(client, response);
            shutdown(client, SD_BOTH);
            closesocket(client);
          });
    } else {
      scheduler.SubmitDetached(
          echo::async::RequestKind::Generic,
          [client, method, path, query, headers, body, &api](echo::async::CancellationToken) {
            auto response = api.Handle(method, path, query, headers, body);
            SendResponse(client, response);
            shutdown(client, SD_BOTH);
            closesocket(client);
          });
    }
  }
}

}  // namespace echo::compat_server
