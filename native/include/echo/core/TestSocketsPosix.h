#pragma once

#include <cerrno>
#include <cstdint>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>

using SOCKET = int;
using DWORD = unsigned int;
using WSADATA = int;
constexpr SOCKET INVALID_SOCKET = -1;
constexpr int SOCKET_ERROR = -1;
constexpr int SD_BOTH = SHUT_RDWR;
constexpr int WSAEWOULDBLOCK = EWOULDBLOCK;

inline constexpr unsigned short MAKEWORD(unsigned char low, unsigned char high) {
  return static_cast<unsigned short>(low | (static_cast<unsigned short>(high) << 8));
}

inline int WSAStartup(unsigned short, WSADATA*) { return 0; }
inline int WSACleanup() { return 0; }
inline int WSAGetLastError() { return errno; }
inline int closesocket(SOCKET socket) { return ::close(socket); }
inline int ioctlsocket(SOCKET socket, long command, unsigned long* value) {
  int nonblocking = value && *value ? 1 : 0;
  return ioctl(socket, command, &nonblocking);
}

inline int EchoTestSelect(int, fd_set* readSet, fd_set* writeSet, fd_set* errorSet,
                          timeval* timeout) {
  int maxDescriptor = -1;
  for (const fd_set* set : {readSet, writeSet, errorSet}) {
    if (!set) continue;
    for (int descriptor = 0; descriptor < FD_SETSIZE; ++descriptor) {
      if (FD_ISSET(descriptor, set)) maxDescriptor = std::max(maxDescriptor, descriptor);
    }
  }
  return ::select(maxDescriptor + 1, readSet, writeSet, errorSet, timeout);
}

inline int EchoTestGetSockName(SOCKET socket, sockaddr* address, int* length) {
  socklen_t nativeLength = length ? static_cast<socklen_t>(*length) : 0;
  const int result = ::getsockname(socket, address, length ? &nativeLength : nullptr);
  if (length) *length = static_cast<int>(nativeLength);
  return result;
}

inline int EchoTestGetSockName(SOCKET socket, sockaddr* address, socklen_t* length) {
  return ::getsockname(socket, address, length);
}

inline int EchoTestGetSockName(SOCKET socket, sockaddr* address, std::nullptr_t) {
  return ::getsockname(socket, address, nullptr);
}

inline SOCKET EchoTestAccept(SOCKET socket, sockaddr* address, int* length) {
  socklen_t nativeLength = length ? static_cast<socklen_t>(*length) : 0;
  const SOCKET result = ::accept(socket, address, length ? &nativeLength : nullptr);
  if (length) *length = static_cast<int>(nativeLength);
  return result;
}

inline SOCKET EchoTestAccept(SOCKET socket, sockaddr* address, socklen_t* length) {
  return ::accept(socket, address, length);
}

inline SOCKET EchoTestAccept(SOCKET socket, sockaddr* address, std::nullptr_t) {
  return ::accept(socket, address, nullptr);
}

inline int EchoTestSetSocketOption(SOCKET socket, int level, int option,
                                  const char* value, int length) {
  if (level == SOL_SOCKET && option == SO_RCVTIMEO &&
      length == static_cast<int>(sizeof(DWORD))) {
    const DWORD milliseconds = *reinterpret_cast<const DWORD*>(value);
    timeval timeout{};
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec = static_cast<suseconds_t>(milliseconds % 1000) * 1000;
    return ::setsockopt(socket, level, option, &timeout, sizeof(timeout));
  }
  return ::setsockopt(socket, level, option, value, static_cast<socklen_t>(length));
}

inline int EchoTestSend(SOCKET socket, const char* data, int length, int flags) {
  return static_cast<int>(::send(socket, data, static_cast<std::size_t>(length),
                                 flags | MSG_NOSIGNAL));
}

inline int EchoTestRecv(SOCKET socket, char* data, int length, int flags) {
  return static_cast<int>(::recv(socket, data, static_cast<std::size_t>(length), flags));
}

#define select EchoTestSelect
#define getsockname EchoTestGetSockName
#define accept EchoTestAccept
#define setsockopt EchoTestSetSocketOption
#define send EchoTestSend
#define recv EchoTestRecv
