#include "echo/diagnostics/CrashCapture.h"

#include <windows.h>
#include <dbghelp.h>
#include <minidumpapiset.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#include "echo/diagnostics/EchoDiagnostics.h"

#pragma comment(lib, "dbghelp.lib")

// Stage 0 (docs/vip-stability-remediation-plan-2026-09-05.md): Release-usable
// crash forensics. Both historical RTC#2 events left only a variable name — no
// dump, no stack, no build identity. This filter is additive-only: healthy code
// paths never observe it, and install failure never fails initialization.

namespace echo::diagnostics {
namespace {

std::atomic<bool> g_installed{false};
std::wstring g_dump_directory;
std::string g_build_hash = ECHO_CRASH_BUILD_HASH;

std::string NarrowPath(const std::filesystem::path& p) {
  return p.string();
}

std::string Hex(uintptr_t value) {
  std::ostringstream s;
  s << std::hex << std::setw(8) << std::setfill('0') << value;
  return s.str();
}

std::string NowStamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_s(&tm, &t);
  std::ostringstream s;
  s << std::put_time(&tm, "%Y%m%d-%H%M%S");
  return s.str();
}

std::string ExceptionCodeName(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case 0xC0000417: return "STATUS_INVALID_PARAMETER (fast-fail)";
    case 0xC0000409: return "STATUS_STACK_BUFFER_OVERRUN (fast-fail/RTC)";
    default: return "EXCEPTION_0x" + Hex(code);
  }
}

void WriteReport(const std::string& reason,
                 EXCEPTION_POINTERS* pointers,
                 const std::string& report_path);

LONG WINAPI UnhandledExceptionFilterImpl(EXCEPTION_POINTERS* pointers) {
  const std::string stamp = NowStamp();
  const std::string base = NarrowPath(std::filesystem::path(g_dump_directory) /
                                      ("crash-" + stamp));

  // 1. Minidump: all thread stacks + indirectly referenced memory. Best
  // effort — a failure here must not block the text report.
  if (HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll")) {
    auto mini_dump = reinterpret_cast<BOOL(WINAPI*)(HANDLE, DWORD, HANDLE,
        MINIDUMP_TYPE, CONST PMINIDUMP_EXCEPTION_INFORMATION,
        CONST PMINIDUMP_USER_STREAM_INFORMATION,
        CONST PMINIDUMP_CALLBACK_INFORMATION)>(
        GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    if (mini_dump) {
      HANDLE process = GetCurrentProcess();
      DWORD pid = GetCurrentProcessId();
      MINIDUMP_EXCEPTION_INFORMATION mei;
      mei.ThreadId = GetCurrentThreadId();
      mei.ExceptionPointers = pointers;
      mei.ClientPointers = FALSE;
      if (HANDLE file = CreateFileA((base + ".dmp").c_str(), GENERIC_WRITE,
                                    0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr)) {
        mini_dump(process, pid, file,
                  static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs |
                                             MiniDumpWithThreadInfo |
                                             MiniDumpWithIndirectlyReferencedMemory),
                  &mei, nullptr, nullptr);
        CloseHandle(file);
      }
    }
    // Do not FreeLibrary: the filter may race process teardown.
  }

  // 2. Text report: build hash + exception + symbolized faulting-thread stack.
  std::ostringstream reason;
  reason << ExceptionCodeName(pointers->ExceptionRecord->ExceptionCode)
         << " at address 0x" << std::hex
         << reinterpret_cast<uintptr_t>(pointers->ExceptionRecord->ExceptionAddress);
  WriteReport(reason.str(), pointers, base + ".txt");
  return EXCEPTION_EXECUTE_HANDLER;
}

void WriteReport(const std::string& reason,
                 EXCEPTION_POINTERS* pointers,
                 const std::string& report_path) {
  std::ostringstream out;
  out << "BottleMusic crash report\n"
      << "reason: " << reason << "\n"
      << "build_hash: " << CrashCaptureBuildHash() << "\n"
      << "pid: " << GetCurrentProcessId() << "\n"
      << "tid: " << GetCurrentThreadId() << "\n";

  CONTEXT* ctx = pointers ? pointers->ContextRecord : nullptr;
  if (pointers && pointers->ExceptionRecord) {
    out << "exception_code: 0x" << std::hex << pointers->ExceptionRecord->ExceptionCode
        << " (" << ExceptionCodeName(pointers->ExceptionRecord->ExceptionCode) << ")\n"
        << "exception_address: 0x"
        << reinterpret_cast<uintptr_t>(pointers->ExceptionRecord->ExceptionAddress)
        << "\n";
  }
  if (ctx) {
    out << "rip: 0x" << std::hex << ctx->Rip << " rsp: 0x" << ctx->Rsp << "\n";
  }

  // Symbolized stack walk of the faulting thread. Requires the matching PDB
  // next to the binary or in _NT_SYMBOL_PATH (CMake now archives PDBs).
  if (ctx && SymInitialize(GetCurrentProcess(), nullptr, TRUE)) {
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS |
                  SYMOPT_LOAD_ANYTHING);
    HANDLE thread = GetCurrentThread();
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = ctx->Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    out << "stack:\n";
    for (int i = 0; i < 64; ++i) {
      if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), thread,
                       &frame, ctx, nullptr, SymFunctionTableAccess64,
                       SymGetModuleBase64, nullptr)) {
        break;
      }
      if (frame.AddrPC.Offset == 0) break;
      out << "  #" << std::dec << i << " 0x" << std::hex << frame.AddrPC.Offset;
      char symbol_buffer[sizeof(SYMBOL_INFO) + 256]{};
      auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_buffer);
      symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
      symbol->MaxNameLen = 255;
      DWORD64 displacement = 0;
      if (SymFromAddr(GetCurrentProcess(), frame.AddrPC.Offset, &displacement,
                      symbol)) {
        out << "  " << symbol->Name << " +0x" << std::hex << displacement;
      }
      IMAGEHLP_MODULE64 module{};
      module.SizeOfStruct = sizeof(module);
      if (SymGetModuleInfo64(GetCurrentProcess(), frame.AddrPC.Offset, &module)) {
        out << "  [" << module.ImageName << "]";
      }
      out << "\n";
    }
    SymCleanup(GetCurrentProcess());
  } else {
    out << "stack: <symbols unavailable — attach the archived PDB for this "
           "build_hash>\n";
  }

  std::ofstream file(report_path);
  if (file) {
    file << out.str();
  }
  ECHO_LOG("CRASH", "crash report written: " + report_path + " (" + reason + ")");
}

}  // namespace

bool InstallCrashCapture(const CrashCaptureConfig& config) {
  std::filesystem::path dir = config.dump_directory.empty()
      ? std::filesystem::temp_directory_path() / "EchoMusicNative" / "crash"
      : std::filesystem::path(config.dump_directory);
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) return false;

  g_dump_directory = dir.wstring();
  g_build_hash = config.build_hash.empty() ? std::string(ECHO_CRASH_BUILD_HASH)
                                           : config.build_hash;
  g_installed.store(true, std::memory_order_release);
  SetUnhandledExceptionFilter(UnhandledExceptionFilterImpl);
  ECHO_LOG("CRASH", "crash capture installed: dir=" + NarrowPath(dir) +
                        " build_hash=" + g_build_hash);
  return true;
}

std::wstring CrashCaptureDirectory() {
  return g_dump_directory;
}

std::string CrashCaptureBuildHash() {
  return g_build_hash;
}

std::string WriteCrashReportForTest(const std::string& reason) {
  if (!g_installed.load(std::memory_order_acquire)) return {};
  const std::string base = NarrowPath(std::filesystem::path(g_dump_directory) /
                                      ("crash-" + NowStamp() + "-test"));
  WriteReport(reason, nullptr, base + ".txt");
  std::ifstream probe(base + ".txt");
  return probe.good() ? base + ".txt" : std::string{};
}

}  // namespace echo::diagnostics
