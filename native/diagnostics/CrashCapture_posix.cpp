#include "echo/diagnostics/CrashCapture.h"

#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <codecvt>
#include <cstddef>
#include <chrono>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <locale>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace echo::diagnostics {
namespace {

std::atomic<bool> g_installed{false};
std::filesystem::path g_dump_directory;
std::string g_build_hash = ECHO_CRASH_BUILD_HASH;
std::array<char, 4096> g_signal_report_path{};
alignas(std::max_align_t) std::array<char, 64 * 1024> g_signal_stack{};

const char* SignalReason(int signalNumber) {
  switch (signalNumber) {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGBUS: return "SIGBUS";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    default: return "signal";
  }
}

std::size_t SignalReasonLength(int signalNumber) {
  switch (signalNumber) {
    case SIGSEGV: return sizeof("SIGSEGV") - 1;
    case SIGABRT: return sizeof("SIGABRT") - 1;
    case SIGBUS: return sizeof("SIGBUS") - 1;
    case SIGFPE: return sizeof("SIGFPE") - 1;
    case SIGILL: return sizeof("SIGILL") - 1;
    default: return sizeof("signal") - 1;
  }
}

void WriteAll(int fd, const char* bytes, std::size_t size) {
  while (size > 0) {
    const auto written = write(fd, bytes, size);
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) return;
    bytes += written;
    size -= static_cast<std::size_t>(written);
  }
}

void AppendSignalReport(int signalNumber) {
  const int fd = open(g_signal_report_path.data(), O_CREAT | O_WRONLY | O_APPEND, 0600);
  if (fd < 0) return;
  static constexpr char kHeader[] = "BottleMusic crash report\nreason: ";
  static constexpr char kBuildHash[] = "\nbuild_hash: ";
  static constexpr char kStack[] = "\nstack:\n";
  const char* reason = SignalReason(signalNumber);
  WriteAll(fd, kHeader, sizeof(kHeader) - 1);
  WriteAll(fd, reason, SignalReasonLength(signalNumber));
  WriteAll(fd, kBuildHash, sizeof(kBuildHash) - 1);
  WriteAll(fd, g_build_hash.data(), g_build_hash.size());
  WriteAll(fd, kStack, sizeof(kStack) - 1);
  void* frames[64];
  const int count = backtrace(frames, 64);
  backtrace_symbols_fd(frames, count, fd);
  close(fd);
}

void CrashSignalHandler(int signalNumber, siginfo_t*, void*) {
  AppendSignalReport(signalNumber);
  sigset_t signalSet;
  sigemptyset(&signalSet);
  sigaddset(&signalSet, signalNumber);
  sigprocmask(SIG_UNBLOCK, &signalSet, nullptr);
  raise(signalNumber);
  _exit(128 + signalNumber);
}

std::string NewReportPath() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return (g_dump_directory /
          ("crash-" + std::to_string(getpid()) + "-" + std::to_string(stamp) + ".txt"))
      .string();
}

std::string NarrowPath(const std::wstring& value) {
  return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.to_bytes(value);
}

std::wstring WidenPath(const std::filesystem::path& value) {
  return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.from_bytes(value.string());
}

}  // namespace

bool InstallCrashCapture(const CrashCaptureConfig& config) {
  auto directory = config.dump_directory.empty()
      ? std::filesystem::temp_directory_path() / "EchoMusicNative" / "crash"
      : std::filesystem::path(NarrowPath(config.dump_directory));
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) return false;
  g_dump_directory = std::move(directory);
  if (!config.build_hash.empty()) g_build_hash = config.build_hash;

  const auto signalPath = (g_dump_directory /
      ("crash-" + std::to_string(getpid()) + ".txt")).string();
  if (signalPath.size() >= g_signal_report_path.size()) return false;
  std::memcpy(g_signal_report_path.data(), signalPath.c_str(), signalPath.size() + 1);

  stack_t alternate{};
  alternate.ss_sp = g_signal_stack.data();
  alternate.ss_size = g_signal_stack.size();
  if (sigaltstack(&alternate, nullptr) != 0) return false;
  struct sigaction action {};
  action.sa_sigaction = CrashSignalHandler;
  action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
  sigemptyset(&action.sa_mask);
  for (const int signalNumber : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) {
    if (sigaction(signalNumber, &action, nullptr) != 0) return false;
  }
  g_installed.store(true, std::memory_order_release);
  return true;
}

std::wstring CrashCaptureDirectory() {
  if (!g_installed.load(std::memory_order_acquire)) return {};
  return WidenPath(g_dump_directory);
}

std::string CrashCaptureBuildHash() {
  return g_build_hash;
}

std::string WriteCrashReportForTest(const std::string& reason) {
  if (!g_installed.load(std::memory_order_acquire)) return {};
  const auto reportPath = NewReportPath();
  const int fd = open(reportPath.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
  if (fd < 0) return {};
  const std::string header = "BottleMusic crash report\nreason: " + reason +
      "\nbuild_hash: " + CrashCaptureBuildHash() + "\npid: " +
      std::to_string(getpid()) + "\nstack:\n";
  const auto written = write(fd, header.data(), header.size());
  if (written != static_cast<ssize_t>(header.size())) {
    close(fd);
    return {};
  }
  void* frames[64];
  const int count = backtrace(frames, 64);
  backtrace_symbols_fd(frames, count, fd);
  close(fd);
  return reportPath;
}

}  // namespace echo::diagnostics
