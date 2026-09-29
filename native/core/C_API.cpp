#include "echo/core/C_API.h"
#include "echo/core/CompatApi.h"
#include "echo/core/HttpClient.h"
#include "echo/core/RequestDeadlines.h"
#include "echo/async/RequestScheduler.h"
#include "echo/storage/Database.h"
#include "echo/storage/AppPaths.h"
#include "echo/diagnostics/CrashCapture.h"
#include "echo/diagnostics/EchoDiagnostics.h"
#include "echo/stats/PlayStatsService.h"
#include <nlohmann/json.hpp>
#include <exception>
#include <typeinfo>
#include <type_traits>
#include <cstdlib>
#include <cctype>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif
#include <atomic>
#include <chrono>
#include <limits>
#include <new>
#include <memory>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string_view>

// Process-local state cluster. FFI signatures stay Echo*(...) without an
// EchoContext* handle; internals use Ctx() so globals are not scattered.
// Single-process desktop app: intentionally NOT handle-ized FFI (no multi-tenant
// / multi-backend need). api is shared_ptr so workers hold a strong ref across
// EchoShutdown.
struct EchoContext {
  std::unique_ptr<echo::storage::Database> db;
  std::shared_ptr<echo::core::CompatApi> api;
  echo::async::RequestScheduler scheduler{4};
  std::shared_mutex api_rwlock;
  // atomic: written without api_rwlock (EchoShutdown), read under shared_lock.
  std::atomic<bool> shutdown{false};
  std::unique_ptr<echo::stats::PlayStatsService> stats;
  std::string last_error;
};

static EchoContext& Ctx() {
  static EchoContext ctx;
  return ctx;
}

static const char* _dup_str(const char* s) noexcept {
    if (!s) return nullptr;
    const auto size = std::strlen(s);
    if (size == std::numeric_limits<std::size_t>::max()) return nullptr;
    char* out = new (std::nothrow) char[size + 1];
    if (!out) return nullptr;
    std::memcpy(out, s, size + 1);
    return out;
}

enum class AppDataDirState { Valid, Blank, TooLong };

static AppDataDirState ValidateAppDataDir(const char* path) {
    if (!path) return AppDataDirState::Valid;  // null explicitly selects the default path
    bool hasNonWhitespace = false;
    for (std::size_t i = 0; i <= ECHO_C_API_MAX_APP_DATA_DIR_BYTES; ++i) {
        const auto byte = static_cast<unsigned char>(path[i]);
        if (byte == '\0') {
            return hasNonWhitespace ? AppDataDirState::Valid : AppDataDirState::Blank;
        }
        if (i == ECHO_C_API_MAX_APP_DATA_DIR_BYTES) return AppDataDirState::TooLong;
        if (!std::isspace(byte)) hasNonWhitespace = true;
    }
    return AppDataDirState::TooLong;
}

static bool CStringWithinLimit(const char* value, std::size_t maxBytes) noexcept {
    if (!value) return true;
    for (std::size_t i = 0; i <= maxBytes; ++i) {
        if (value[i] == '\0') return true;
    }
    return false;
}

static char* CopyCStringNoThrow(std::string_view value) noexcept {
    if (value.size() == std::numeric_limits<std::size_t>::max()) return nullptr;
    char* out = new (std::nothrow) char[value.size() + 1];
    if (!out) return nullptr;
    std::memcpy(out, value.data(), value.size());
    out[value.size()] = '\0';
    return out;
}

static void WriteFixedResponse(char** outResponse, std::string_view response) noexcept {
    if (outResponse) *outResponse = CopyCStringNoThrow(response);
}

static void WriteInputTooLargeResponse(const char* field, char** outResponse) noexcept {
    if (std::strcmp(field, "method") == 0) {
        WriteFixedResponse(outResponse,
            R"({"status":413,"headers":{"Content-Type":"application/json; charset=utf-8"},"body":{"error":"request_too_large","field":"method"}})");
    } else if (std::strcmp(field, "path") == 0) {
        WriteFixedResponse(outResponse,
            R"({"status":413,"headers":{"Content-Type":"application/json; charset=utf-8"},"body":{"error":"request_too_large","field":"path"}})");
    } else if (std::strcmp(field, "query_json") == 0) {
        WriteFixedResponse(outResponse,
            R"({"status":413,"headers":{"Content-Type":"application/json; charset=utf-8"},"body":{"error":"request_too_large","field":"query_json"}})");
    } else if (std::strcmp(field, "headers_json") == 0) {
        WriteFixedResponse(outResponse,
            R"({"status":413,"headers":{"Content-Type":"application/json; charset=utf-8"},"body":{"error":"request_too_large","field":"headers_json"}})");
    } else {
        WriteFixedResponse(outResponse,
            R"({"status":413,"headers":{"Content-Type":"application/json; charset=utf-8"},"body":{"error":"request_too_large","field":"body"}})");
    }
}

// Map a request path to a RequestKind for per-kind deadlines.
static echo::async::RequestKind KindForPath(const std::string& path) {
    if (path.rfind("/song/url", 0) == 0) return echo::async::RequestKind::SongUrl;
    if (path.rfind("/search", 0) == 0) return echo::async::RequestKind::Search;
    if (path.rfind("/images/", 0) == 0) return echo::async::RequestKind::Image;
    if (path.rfind("/login/qr/", 0) == 0) return echo::async::RequestKind::LoginPoll;
    if (path.rfind("/playlist", 0) == 0 || path.rfind("/rank", 0) == 0 ||
        path.rfind("/top/", 0) == 0 || path.rfind("/album", 0) == 0 ||
        path.rfind("/artist", 0) == 0) return echo::async::RequestKind::Playlist;
    return echo::async::RequestKind::Generic;
}

static long DeadlineMsForKind(echo::async::RequestKind kind) {
    switch (kind) {
        case echo::async::RequestKind::SongUrl:   return echo::core::kDeadlineSongUrlMs;
        case echo::async::RequestKind::Image:     return echo::core::kDeadlineImageMs;
        case echo::async::RequestKind::LoginPoll: return echo::core::kDeadlineLoginPollMs;
        case echo::async::RequestKind::Search:    return echo::core::kDeadlineSearchMs;
        case echo::async::RequestKind::Playlist:  return echo::core::kDeadlinePlaylistMs;
        case echo::async::RequestKind::Generic:   return echo::core::kDeadlineGenericMs;
    }
    return echo::core::kDeadlineGenericMs;
}

// Initialize Ctx().db/Ctx().api if needed. PRECONDITION: caller holds Ctx().api_rwlock
// EXCLUSIVELY (unique_lock). Mutation of the globals only ever happens under the
// exclusive lock; requests read them under a shared lock.
static void EnsureInitializedLocked(const char* app_data_dir) {
    if (Ctx().shutdown.load(std::memory_order_acquire)) return;
    if(!Ctx().scheduler.Restart()) {
        Ctx().shutdown.store(true, std::memory_order_release);
        throw std::runtime_error("request scheduler restart failed");
    }
    if(!Ctx().db) {
        Ctx().db = std::make_unique<echo::storage::Database>();
#ifdef _WIN32
        std::filesystem::path dbPath = app_data_dir
            ? std::filesystem::path(reinterpret_cast<const char8_t*>(app_data_dir)) / "bottlemusic.db"
            : echo::storage::GetDefaultDatabasePath();
#else
        std::filesystem::path dbPath = app_data_dir
            ? std::filesystem::path(app_data_dir) / "bottlemusic.db"
            : echo::storage::GetDefaultDatabasePath();
#endif
        Ctx().db->Open(dbPath);
        Ctx().db->Initialize();
        Ctx().api = std::make_shared<echo::core::CompatApi>(*Ctx().db);
        Ctx().stats = std::make_unique<echo::stats::PlayStatsService>(*Ctx().db);
    }
}

int EchoInitializeWithPathsV2(const char* app_data_dir) {
    try {
        std::unique_lock<std::shared_mutex> lock(Ctx().api_rwlock);
        Ctx().shutdown.store(false, std::memory_order_release);  // allow re-init after shutdown
        Ctx().last_error.clear();
        try {
            switch (ValidateAppDataDir(app_data_dir)) {
                case AppDataDirState::Blank:
                    throw std::invalid_argument("app_data_dir must not be empty or whitespace");
                case AppDataDirState::TooLong:
                    throw std::length_error("app_data_dir exceeds the platform path limit");
                case AppDataDirState::Valid:
                    break;
            }
            // Stage 0: crash forensics. Must never block/fail initialization; dumps
            // land under <app_data>/crash (or temp fallback) so Release faults carry
            // a minidump + build hash + stack (audit G3). Keep path conversion inside
            // the C ABI exception boundary: Windows rejects malformed UTF-8 here.
            {
                echo::diagnostics::CrashCaptureConfig crash_config;
                if (app_data_dir) {
                    crash_config.dump_directory =
                        std::filesystem::path(reinterpret_cast<const char8_t*>(app_data_dir)) /
                        "crash";
                }
                echo::diagnostics::InstallCrashCapture(crash_config);
            }
            EnsureInitializedLocked(app_data_dir);
            if (!Ctx().api || !Ctx().db || !Ctx().stats) {
                throw std::runtime_error("backend context is incomplete");
            }
            return 0;
        } catch (const std::exception& e) {
            // Never let C++ exceptions cross the extern "C" FFI boundary.
            Ctx().last_error = std::string("initialize failed: ") + e.what();
            Ctx().api.reset();
            Ctx().stats.reset();
            Ctx().db.reset();
            Ctx().shutdown.store(true, std::memory_order_release);
            Ctx().scheduler.Shutdown(std::chrono::milliseconds(3000));
            return 1;
        } catch (...) {
            Ctx().last_error = "initialize failed: unknown error";
            Ctx().api.reset();
            Ctx().stats.reset();
            Ctx().db.reset();
            Ctx().shutdown.store(true, std::memory_order_release);
            Ctx().scheduler.Shutdown(std::chrono::milliseconds(3000));
            return 1;
        }
    } catch (...) {
        // Even failures while acquiring the context lock must not unwind
        // through extern "C". The state may be unavailable after a static
        // initialization failure, so this fallback deliberately does not
        // allocate or attempt further teardown.
        try {
            Ctx().shutdown.store(true, std::memory_order_release);
        } catch (...) {
        }
        return 1;
    }
}

int EchoInitializeV2() {
    return EchoInitializeWithPathsV2(nullptr);
}

void EchoInitializeWithPaths(const char* app_data_dir) {
    (void)EchoInitializeWithPathsV2(app_data_dir);
}

void EchoInitialize() {
    (void)EchoInitializeV2();
}

char* EchoGetLastError() {
    try {
        std::shared_lock<std::shared_mutex> lock(Ctx().api_rwlock);
        return const_cast<char*>(_dup_str(Ctx().last_error.c_str()));
    } catch (...) {
        return nullptr;
    }
}

// Returns zero only when the DLL is safe to unload. Non-zero means detached
// workers or lock holders may still execute inside it.
// See P0-B: drop(_lib) after abandoned workers → use-after-unload.
static int EchoShutdownImpl() {
    // Phase 1: stop accepting new jobs and drain the scheduler with a hard
    // 3s deadline. This MUST happen before acquiring the exclusive lock,
    // because workers executing in-flight jobs try to acquire the shared
    // lock inside their lambda. If we held the exclusive lock and then
    // called Shutdown()->join(), we'd deadlock. If we used the unbounded
    // Shutdown() and a worker was stuck in a 60s uninterruptible job,
    // EchoShutdown would block for 60s+ — violating the "close within
    // 3-5s" contract. Bounded Shutdown detaches hung workers (safe since
    // the process is exiting).
    Ctx().shutdown.store(true, std::memory_order_release);
    const auto abandoned = Ctx().scheduler.Shutdown(std::chrono::milliseconds(3000));

    // If the bounded shutdown had to abandon a worker, that detached thread may
    // still be running apiShared->Handle(...). The captured apiShared keeps the
    // CompatApi object alive, but CompatApi holds the Database BY REFERENCE
    // (storage::Database&), so resetting Ctx().db would free the storage out from
    // under the live worker — a use-after-free. The process is exiting anyway,
    // so the safe choice is to leak: skip the teardown and the HTTP pool close
    // entirely and let the OS reclaim everything.
    // Also tell the Rust loader not to FreeLibrary the DLL (P0-B).
    if (abandoned > 0) {
        return static_cast<int>(abandoned);
    }

    // Phase 2: no worker was abandoned, so every job has finished. Acquire the
    // exclusive lock (bounded) to safely tear down Ctx().api/Ctx().stats/Ctx().db. If the 3s
    // acquisition times out we return WITHOUT resetting them — Ctx().shutdown still
    // blocks new requests and the process is exiting, so the leak is acceptable.
    {
        std::unique_lock<std::shared_mutex> lock(Ctx().api_rwlock, std::defer_lock);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline) {
            if (lock.try_lock()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!lock.owns_lock()) {
            // A direct C API caller still holds the shared lock. Signal the
            // loader to retain the DLL mapping just as for an abandoned worker.
            return 1;
        }
        Ctx().api.reset();
        Ctx().stats.reset();
        Ctx().db.reset();
    }
    echo::core::CloseHttpConnectionPool();
    return 0;
}

int EchoShutdown() {
    try {
        return EchoShutdownImpl();
    } catch (...) {
        // The caller must retain the DLL when teardown cannot prove that all
        // native work has stopped. Never unwind a C++ exception through C ABI.
        try {
            Ctx().shutdown.store(true, std::memory_order_release);
        } catch (...) {
        }
        return 1;
    }
}

// Serialize a CompatResponse to a heap-allocated JSON string. Used by
// EchoHandleRequest's multiple early-exit paths. Out-of-line so the
// caller doesn't have to wrap each path in its own try/catch.
static void SerializeResponse(const echo::core::CompatResponse& r, char** out_response) noexcept {
    if (!out_response) return;
    *out_response = nullptr;
    try {
        nlohmann::json out = {
            {"status", r.httpStatus},
            {"headers", {{"Content-Type", r.contentType}}},
            {"body", r.body}
        };
        auto outStr = out.dump();
        if (outStr.size() > ECHO_C_API_MAX_RESPONSE_BYTES) {
            WriteFixedResponse(out_response,
                R"({"status":502,"headers":{"Content-Type":"application/json; charset=utf-8"},"body":{"error":"response_too_large"}})");
            return;
        }
        *out_response = CopyCStringNoThrow(outStr);
    } catch (...) {
        WriteFixedResponse(out_response,
            R"({"status":500,"headers":{"Content-Type":"application/json; charset=utf-8"},"body":{"error":"serialization_failed"}})");
    }
}

void EchoHandleRequest(const char* method, const char* path, const char* query_json, const char* headers_json, const char* body, char** out_response) {
    if(!out_response) return;
    *out_response = nullptr;

    try {
        const char* oversizedField = nullptr;
        if (!CStringWithinLimit(method, ECHO_C_API_MAX_METHOD_BYTES)) oversizedField = "method";
        else if (!CStringWithinLimit(path, ECHO_C_API_MAX_PATH_BYTES)) oversizedField = "path";
        else if (!CStringWithinLimit(query_json, ECHO_C_API_MAX_QUERY_JSON_BYTES)) oversizedField = "query_json";
        else if (!CStringWithinLimit(headers_json, ECHO_C_API_MAX_HEADERS_JSON_BYTES)) oversizedField = "headers_json";
        else if (!CStringWithinLimit(body, ECHO_C_API_MAX_BODY_BYTES)) oversizedField = "body";
        if (oversizedField) {
            WriteInputTooLargeResponse(oversizedField, out_response);
            return;
        }

        echo::core::QueryMap q;
        echo::core::HeaderMap h;

        if(query_json) {
            try {
                auto j = nlohmann::json::parse(query_json);
                if(j.is_object()) {
                    for(auto& el : j.items()) {
                        q[el.key()] = el.value().is_string() ? el.value().get<std::string>() : el.value().dump();
                    }
                }
            } catch(...) {}
        }

        if(headers_json) {
            try {
                auto j = nlohmann::json::parse(headers_json);
                if(j.is_object()) {
                    for(auto& el : j.items()) {
                        h[el.key()] = el.value().is_string() ? el.value().get<std::string>() : el.value().dump();
                    }
                }
            } catch(...) {}
        }

        echo::core::CompatResponse r;
        std::string methodStr = method ? method : "GET";
        std::string pathStr = path ? path : "/";
        std::string bodyStr = body ? body : "";

        auto kind = KindForPath(pathStr);
        long deadlineMs = DeadlineMsForKind(kind);

        // Acquire a strong reference to Ctx().api under the rwlock so the object
        // stays alive for the entire scheduled call — even if EchoShutdown
        // runs concurrently and resets the global Ctx().api pointer. The
        // rwlock gates the pointer swap; the object's lifetime is now
        // ref-counted via shared_ptr.
        std::shared_ptr<echo::core::CompatApi> apiShared;
        {
            std::shared_lock<std::shared_mutex> lock(Ctx().api_rwlock, std::defer_lock);
            auto lockDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (std::chrono::steady_clock::now() < lockDeadline) {
                if (lock.try_lock()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!lock.owns_lock()) {
                r.httpStatus = 503;
                r.body = {{"error", "shutdown_in_progress"}};
                SerializeResponse(r, out_response);
                return;
            }
            if (!Ctx().api || Ctx().shutdown.load(std::memory_order_acquire)) {
                r.httpStatus = 500;
                r.body = {{"error", "C API is not initialized or was shut down"}};
                SerializeResponse(r, out_response);
                return;
            }
            apiShared = Ctx().api;
        }  // release shared_lock

        try {
            // Route through the RequestScheduler with a per-kind deadline.
            // The scheduler provides bounded concurrency (4 workers + queue cap)
            // and the deadline ensures a hung WinHTTP call frees the future even
            // if it can't be interrupted cooperatively. The captured apiShared
            // keeps Ctx().api alive even if EchoShutdown runs while we wait.
            // Pin the production response's move properties in production builds,
            // including MSVC's move-assignment delivery path. The scheduler also
            // preserves delivery exceptions for generic throwing return types.
            static_assert(std::is_nothrow_move_constructible_v<echo::core::CompatResponse>,
                          "SubmitWithDeadline production return type must be nothrow-move");
            static_assert(std::is_nothrow_move_assignable_v<echo::core::CompatResponse>,
                          "SubmitWithDeadline production return type must be nothrow-move-assignable");
            auto fut = Ctx().scheduler.SubmitWithDeadline(
                kind,
                [apiShared, methodStr, pathStr, q, h, bodyStr](echo::async::CancellationToken token) -> echo::core::CompatResponse {
                    // P1-C: expose scheduler cancel to nested HttpClient calls.
                    echo::core::HttpClientCancellationScope cancelScope(token.Flag());
                    return apiShared->Handle(methodStr, pathStr, q, h, bodyStr);
                },
                deadlineMs);
            r = fut.get();
        } catch(const std::runtime_error& e) {
            // Deadline or queue-full
            r.httpStatus = 504;
            r.body = {{"error", e.what()}};
        } catch(std::exception& e) {
            r.httpStatus = 500;
            r.body = {{"error", e.what()}};
        } catch(...) {
            r.httpStatus = 500;
            r.body = {{"error", "Unknown"}};
        }
        SerializeResponse(r, out_response);
    } catch (...) {
        WriteFixedResponse(out_response,
            R"({"status":500,"headers":{"Content-Type":"application/json; charset=utf-8"},"body":{"error":"native_request_failed"}})");
    }
}

void EchoFreeString(char* str) {
    delete[] str;
}

void EchoSetLogCallback(EchoLogCallback cb, void* user_data) {
  try {
    // EchoLogCallback and echo::diagnostics::LogCallback are the same signature,
    // so assign directly. If either ever drifts this stops compiling (intended)
    // instead of silently becoming UB behind a reinterpret_cast.
    echo::diagnostics::SetLogCallback(cb, user_data);

    // 未捕获异常 -> terminate 时记录异常类型与 what()，再交给默认终止流程。
    // 排查后台线程静默 exit(3)（terminate 不落日志）的最后一公里。
    std::set_terminate([] {
        std::string what = "unknown";
        if (auto ep = std::current_exception()) {
            try {
                std::rethrow_exception(ep);
            } catch (const std::exception& e) {
                what = std::string(typeid(e).name()) + ": " + e.what();
            } catch (...) {
                what = "non-std::exception";
            }
        } else {
            what = "terminate called without active exception";
        }
        ECHO_LOG("CRASH", std::string("terminate: ") + what);
        std::abort();
    });

#if defined(_MSC_VER)
    // CRT 错误报告（含 RTC 栈损坏 Run-Time Check Failure）默认弹模态对话框，
    // 无人值守时表现为静默 exit(3)。挂钩后改落日志。Release 也必须挂钩：
    // RTC#2（F20）不走 SEH/UnhandledExceptionFilter，CRT 报告是唯一观测点，
    // 并在此生成取证报告（minidump 不适用于 fast-fail，写文本报告 + 栈）。
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL,
        [](int reportType, char* message, int* returnValue) -> int {
            if (reportType == _CRT_ERROR || reportType == _CRT_ASSERT) {
                const std::string msg = message ? message : "<empty>";
                ECHO_LOG("CRASH", std::string("crt report: ") + msg);
                // RTC 栈损坏类故障不会进入 SEH 过滤器（__fastfail 直接终止），
                // 这里是唯一能留下取证记录的时机。
                echo::diagnostics::WriteCrashReportForTest(
                    "crt-report: " + msg);
            }
            *returnValue = 0; // 继续默认流程（仍可能弹窗/终止）
            return 0;
        });
#endif
  } catch (...) {
    // Logging is diagnostic-only. A callback/setup failure must not unwind
    // into Rust or another C caller.
  }
}

// ─── Stats C API ─────────────────────────────────────────────────────────────

namespace {
// B06: failure reads must not be indistinguishable from an empty database.
// The degraded marker is additive — existing consumers keep parsing the same
// fields, and diagnostics get a truthful signal.
const char* DegradedStatsPayload(const char* shape) noexcept {
    try {
        ECHO_LOG("Stats", std::string("stats read degraded: ") + shape);
    } catch (...) {
    }
    return _dup_str(shape);
}
}  // namespace

ECHO_C_API int EchoStatsRecordPlay(const char* json_record) {
    using echo::stats::RecordStatus;
    try {
        if (!json_record) return kEchoStatsInvalidRecord;
        if (!CStringWithinLimit(json_record, ECHO_C_API_MAX_STATS_JSON_BYTES)) {
            return kEchoStatsBadJson;
        }
        std::shared_lock<std::shared_mutex> lock(Ctx().api_rwlock);
        if (!Ctx().stats) return kEchoStatsNotInitialized;
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(json_record);
        } catch (...) {
            return kEchoStatsBadJson;
        }
        echo::stats::PlayRecord r;
        try {
            // Malformed field types (a null from a non-finite float, a string
            // where a number belongs) are a malformed payload, not a storage
            // failure: surface them as kEchoStatsBadJson.
            const auto getNumber = [&](const char* key) -> std::optional<double> {
                if (!j.contains(key)) return 0.0;
                const auto& v = j[key];
                if (!v.is_number()) return std::nullopt;
                return v.get<double>();
            };
            const auto duration = getNumber("duration_seconds");
            const auto listened = getNumber("listened_seconds");
            if (!duration || !listened) return kEchoStatsBadJson;
            r.songHash = j.value("song_hash", "");
            r.songName = j.value("song_name", "");
            r.singerName = j.value("singer_name", "");
            r.albumId = j.value("album_id", "");
            r.albumName = j.value("album_name", "");
            r.coverUrl = j.value("cover_url", "");
            r.durationSeconds = *duration;
            r.completed = j.value("completed", false);
            r.listenedSeconds = *listened;
            r.quality = j.value("quality", "");
            r.playedAtMs = j.value("played_at", 0LL);
        } catch (...) {
            return kEchoStatsBadJson;
        }
        const auto status = Ctx().stats->RecordPlay(r);
        switch (status) {
            case RecordStatus::Recorded: return kEchoStatsRecorded;
            case RecordStatus::BelowThreshold: return kEchoStatsRecordBelowThreshold;
            case RecordStatus::InvalidRecord:
                ECHO_LOG("Stats", "record play rejected: invalid record fields");
                return kEchoStatsInvalidRecord;
            case RecordStatus::StorageError:
                ECHO_LOG("Stats", "record play failed: storage error");
                return kEchoStatsStorageError;
        }
        return kEchoStatsStorageError;
    } catch (...) {
        return kEchoStatsStorageError;
    }
}

ECHO_C_API const char* EchoStatsGetSummary(const char* range) {
    try {
        if (!CStringWithinLimit(range, ECHO_C_API_MAX_PATH_BYTES)) {
            return DegradedStatsPayload(
                R"({"total_plays":0,"total_listened_seconds":0,"unique_songs":0,"unique_artists":0,"completion_rate":0,"range":"all","degraded":true,"error":"stats_input_too_large"})");
        }
        std::shared_lock<std::shared_mutex> lock(Ctx().api_rwlock);
        if (!Ctx().stats) return DegradedStatsPayload(
            R"({"total_plays":0,"total_listened_seconds":0,"unique_songs":0,"unique_artists":0,"completion_rate":0,"range":"all","degraded":true,"error":"stats_not_initialized"})");
        return _dup_str(Ctx().stats->GetSummary(range ? range : "all").c_str());
    } catch (...) {
        return DegradedStatsPayload(
            R"({"total_plays":0,"total_listened_seconds":0,"unique_songs":0,"unique_artists":0,"completion_rate":0,"range":"all","degraded":true,"error":"stats_read_failed"})");
    }
}

ECHO_C_API const char* EchoStatsGetTop(const char* dim, const char* range, int limit) {
    try {
        if (!CStringWithinLimit(dim, ECHO_C_API_MAX_METHOD_BYTES) ||
            !CStringWithinLimit(range, ECHO_C_API_MAX_PATH_BYTES)) {
            return DegradedStatsPayload(R"({"items":[],"degraded":true,"error":"stats_input_too_large"})");
        }
        std::shared_lock<std::shared_mutex> lock(Ctx().api_rwlock);
        if (!Ctx().stats || !dim || !range)
            return DegradedStatsPayload(R"({"items":[],"degraded":true,"error":"stats_not_initialized"})");
        return _dup_str(Ctx().stats->GetTop(dim, range, limit).c_str());
    } catch (...) {
        return DegradedStatsPayload(R"({"items":[],"degraded":true,"error":"stats_read_failed"})");
    }
}

ECHO_C_API const char* EchoStatsGetTimeline(const char* range) {
    try {
        if (!CStringWithinLimit(range, ECHO_C_API_MAX_PATH_BYTES)) {
            return DegradedStatsPayload(R"({"items":[],"degraded":true,"error":"stats_input_too_large"})");
        }
        std::shared_lock<std::shared_mutex> lock(Ctx().api_rwlock);
        if (!Ctx().stats || !range)
            return DegradedStatsPayload(R"({"items":[],"degraded":true,"error":"stats_not_initialized"})");
        return _dup_str(Ctx().stats->GetTimeline(range).c_str());
    } catch (...) {
        return DegradedStatsPayload(R"({"items":[],"degraded":true,"error":"stats_read_failed"})");
    }
}

ECHO_C_API const char* EchoStatsGetRecent(int limit, int offset) {
    try {
        std::shared_lock<std::shared_mutex> lock(Ctx().api_rwlock);
        if (!Ctx().stats)
            return DegradedStatsPayload(R"({"items":[],"degraded":true,"error":"stats_not_initialized"})");
        return _dup_str(Ctx().stats->GetRecent(limit, offset).c_str());
    } catch (...) {
        return DegradedStatsPayload(R"({"items":[],"degraded":true,"error":"stats_read_failed"})");
    }
}

ECHO_C_API const char* EchoStatsGetRecommendations(int limit) {
    try {
        std::shared_lock<std::shared_mutex> lock(Ctx().api_rwlock);
        if (!Ctx().stats)
            return DegradedStatsPayload(R"({"items":[],"degraded":true,"error":"stats_not_initialized"})");
        return _dup_str(Ctx().stats->GetRecommendations(limit).c_str());
    } catch (...) {
        return DegradedStatsPayload(R"({"items":[],"degraded":true,"error":"stats_read_failed"})");
    }
}
