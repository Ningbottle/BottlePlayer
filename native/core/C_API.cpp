#include "echo/core/C_API.h"
#include "echo/core/CompatApi.h"
#include "echo/core/HttpClient.h"
#include "echo/async/RequestScheduler.h"
#include "echo/storage/Database.h"
#include "echo/storage/AppPaths.h"
#include "echo/diagnostics/EchoDiagnostics.h"
#include "echo/playback/PlaybackController.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <memory>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <sstream>

static std::unique_ptr<echo::storage::Database> g_db;
// g_api is a shared_ptr (not unique_ptr) so worker threads executing
// in-flight requests can capture a strong reference to the object.
// EchoShutdown can reset our global ref while in-flight calls continue
// using their captured shared_ptr; the object is destroyed only when
// the last worker releases its ref. This prevents the use-after-free
// that the bounded Shutdown path would otherwise expose when a worker
// is abandoned mid-call.
static std::shared_ptr<echo::core::CompatApi> g_api;
static echo::async::RequestScheduler g_scheduler(4);
static std::shared_mutex g_api_rwlock;
static bool g_shutdown = false;

static std::shared_ptr<echo::playback::PlaybackController> g_playback;
static std::mutex g_playback_mutex;

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
        case echo::async::RequestKind::SongUrl:   return 10000;
        case echo::async::RequestKind::Image:     return 8000;
        case echo::async::RequestKind::LoginPoll: return 6000;
        case echo::async::RequestKind::Search:
        case echo::async::RequestKind::Playlist:
        case echo::async::RequestKind::Generic:   return 12000;
    }
    return 12000;
}

// Initialize g_db/g_api if needed. PRECONDITION: caller holds g_api_rwlock
// EXCLUSIVELY (unique_lock). Mutation of the globals only ever happens under the
// exclusive lock; requests read them under a shared lock.
static void EnsureInitializedLocked(const char* app_data_dir) {
    if(g_shutdown) return;
    if(!g_db) {
        g_db = std::make_unique<echo::storage::Database>();
#ifdef _WIN32
        std::filesystem::path dbPath = app_data_dir
            ? std::filesystem::path(reinterpret_cast<const char8_t*>(app_data_dir)) / "bottlemusic.db"
            : echo::storage::GetDefaultDatabasePath();
#else
        std::filesystem::path dbPath = app_data_dir
            ? std::filesystem::path(app_data_dir) / "bottlemusic.db"
            : echo::storage::GetDefaultDatabasePath();
#endif
        g_db->Open(dbPath);
        g_db->Initialize();
        g_api = std::make_shared<echo::core::CompatApi>(*g_db);
    }
}

void EchoInitializeWithPaths(const char* app_data_dir) {
    std::unique_lock<std::shared_mutex> lock(g_api_rwlock);
    g_shutdown = false;  // Explicit reset: allow re-init after shutdown (defensive)
    try {
        EnsureInitializedLocked(app_data_dir);
    } catch (const std::exception& e) {
        // Never let C++ exceptions cross the extern "C" FFI boundary.
        // Log and leave g_api null — subsequent requests will get 500.
        g_api.reset();
        g_db.reset();
    } catch (...) {
        g_api.reset();
        g_db.reset();
    }
}

void EchoInitialize() {
    EchoInitializeWithPaths(nullptr);
}

void EchoShutdown() {
    // Phase 1: stop accepting new jobs and drain the scheduler with a hard
    // 3s deadline. This MUST happen before acquiring the exclusive lock,
    // because workers executing in-flight jobs try to acquire the shared
    // lock inside their lambda. If we held the exclusive lock and then
    // called Shutdown()->join(), we'd deadlock. If we used the unbounded
    // Shutdown() and a worker was stuck in a 60s uninterruptible job,
    // EchoShutdown would block for 60s+ — violating the "close within
    // 3-5s" contract. Bounded Shutdown detaches hung workers (safe since
    // the process is exiting).
    g_shutdown = true;
    g_scheduler.Shutdown(std::chrono::milliseconds(3000));

    // Phase 2: acquire exclusive lock (bounded) to safely tear down g_api/g_db.
    // In-flight jobs that haven't finished yet will see g_api == null (or
    // g_shutdown == true) inside their lambda and return 500 without
    // touching the globals. The scheduler is already shut down so no new
    // jobs will start.
    {
        std::unique_lock<std::shared_mutex> lock(g_api_rwlock, std::defer_lock);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline) {
            if (lock.try_lock()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        // Whether we got the lock or timed out: force-tear-down.
        // If we didn't get the lock, in-flight jobs are still running but
        // g_shutdown is true so they'll return 500 and release their shared
        // lock soon. We set g_api=nullptr atomically; jobs that read it after
        // this point get null and return 500. This is safe because unique_ptr
        // reset is a single pointer swap.
    }
    g_api.reset();
    g_db.reset();
    echo::core::CloseHttpConnectionPool();
}

// Serialize a CompatResponse to a heap-allocated JSON string. Used by
// EchoHandleRequest's multiple early-exit paths. Out-of-line so the
// caller doesn't have to wrap each path in its own try/catch.
static void SerializeResponse(const echo::core::CompatResponse& r, char** out_response) {
    try {
        nlohmann::json out = {
            {"status", r.httpStatus},
            {"headers", {{"Content-Type", r.contentType}}},
            {"body", r.body}
        };
        auto outStr = out.dump();
        *out_response = new char[outStr.size() + 1];
        std::strcpy(*out_response, outStr.c_str());
    } catch(std::exception&) {
        *out_response = new char[64];
        std::strcpy(*out_response, R"({"status":500,"error":"serialization failed"})");
    } catch(...) {
        *out_response = new char[64];
        std::strcpy(*out_response, R"({"status":500,"error":"unknown"})");
    }
}

void EchoHandleRequest(const char* method, const char* path, const char* query_json, const char* headers_json, const char* body, char** out_response) {
    if(!out_response) return;

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

    // Acquire a strong reference to g_api under the rwlock so the object
    // stays alive for the entire scheduled call — even if EchoShutdown
    // runs concurrently and resets the global g_api pointer. The
    // rwlock gates the pointer swap; the object's lifetime is now
    // ref-counted via shared_ptr.
    std::shared_ptr<echo::core::CompatApi> apiShared;
    {
        std::shared_lock<std::shared_mutex> lock(g_api_rwlock, std::defer_lock);
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
        if (!g_api || g_shutdown) {
            r.httpStatus = 500;
            r.body = {{"error", "C API is not initialized or was shut down"}};
            SerializeResponse(r, out_response);
            return;
        }
        apiShared = g_api;
    }  // release shared_lock

    try {
        // Route through the RequestScheduler with a per-kind deadline.
        // The scheduler provides bounded concurrency (4 workers + queue cap)
        // and the deadline ensures a hung WinHTTP call frees the future even
        // if it can't be interrupted cooperatively. The captured apiShared
        // keeps g_api alive even if EchoShutdown runs while we wait.
        auto fut = g_scheduler.SubmitWithDeadline(
            kind,
            [apiShared, methodStr, pathStr, q, h, bodyStr](echo::async::CancellationToken token) -> echo::core::CompatResponse {
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
}

void EchoFreeString(char* str) {
    delete[] str;
}

void EchoSetLogCallback(EchoLogCallback cb, void* user_data) {
    // EchoLogCallback and echo::diagnostics::LogCallback are the same signature,
    // so assign directly. If either ever drifts this stops compiling (intended)
    // instead of silently becoming UB behind a reinterpret_cast.
    echo::diagnostics::SetLogCallback(cb, user_data);
}

void EchoSetEventCallback(EchoEventCallback cb, void* user_data) {
    // ABI placeholder — not yet wired. Reserved for playback / download events.
    (void)cb;
    (void)user_data;
}

// ── Playback C API ──────────────────────────────────────────────────────────

static const char* PlaybackStateKindToString(echo::core::PlaybackStateKind kind) {
    switch (kind) {
        case echo::core::PlaybackStateKind::Idle:      return "idle";
        case echo::core::PlaybackStateKind::Opening:    return "opening";
        case echo::core::PlaybackStateKind::Playing:    return "playing";
        case echo::core::PlaybackStateKind::Paused:     return "paused";
        case echo::core::PlaybackStateKind::Buffering:  return "buffering";
        case echo::core::PlaybackStateKind::Stopped:    return "stopped";
        case echo::core::PlaybackStateKind::Failed:     return "failed";
    }
    return "unknown";
}

bool EchoPlaybackInitialize(EchoPlaybackBackend backend) {
    std::lock_guard lock(g_playback_mutex);
    if (g_playback) return true;  // already initialized
    auto pc = std::make_shared<echo::playback::PlaybackController>();
    bool ok = pc->Initialize(static_cast<echo::playback::PlaybackController::Backend>(backend));
    if (!ok && backend == ECHO_PLAYBACK_MFS) {
        // Auto-fallback: MFS failed, try MFP
        ok = pc->Initialize(echo::playback::PlaybackController::Backend::MFP);
    }
    if (!ok) return false;
    g_playback = pc;
    return true;
}

bool EchoPlaybackPlayUrl(const char* url) {
    std::lock_guard lock(g_playback_mutex);
    if (!g_playback) return false;
    return g_playback->PlayUrl(url ? url : "");
}

void EchoPlaybackPause(void) {
    std::lock_guard lock(g_playback_mutex);
    if (g_playback) g_playback->Pause();
}

void EchoPlaybackResume(void) {
    std::lock_guard lock(g_playback_mutex);
    if (g_playback) g_playback->Resume();
}

void EchoPlaybackStop(void) {
    std::lock_guard lock(g_playback_mutex);
    if (g_playback) g_playback->Stop();
}

void EchoPlaybackSeek(double seconds) {
    std::lock_guard lock(g_playback_mutex);
    if (g_playback) g_playback->Seek(seconds);
}

void EchoPlaybackSetVolume(double volume) {
    std::lock_guard lock(g_playback_mutex);
    if (g_playback) g_playback->SetVolume(volume);
}

void EchoPlaybackSetRate(double rate) {
    std::lock_guard lock(g_playback_mutex);
    if (g_playback) g_playback->SetRate(rate);
}

const char* EchoPlaybackGetState(void) {
    std::lock_guard lock(g_playback_mutex);
    if (!g_playback) {
        // Caller must free via EchoFreeString
        char* out = new char[64];
        std::strcpy(out, R"({"state":"uninitialized","position":0,"duration":0})");
        return out;
    }
    auto state = g_playback->GetState();
    std::ostringstream os;
    os << R"({"state":")" << PlaybackStateKindToString(state.kind) << R"(",)"
       << R"("position":)" << state.currentSeconds
       << R"(,"duration":)" << state.durationSeconds
       << R"(,"volume":)" << state.volume
       << R"(,"rate":)" << state.rate;
    if (!state.error.empty()) {
        os << R"(,"error":")" << state.error << R"(")";
    }
    os << "}";
    std::string s = os.str();
    char* out = new char[s.size() + 1];
    std::strcpy(out, s.c_str());
    return out;
}

void EchoPlaybackShutdown(void) {
    std::lock_guard lock(g_playback_mutex);
    g_playback.reset();
}

void EchoPlaybackSetEqEnabled(bool enabled) {
    std::lock_guard lock(g_playback_mutex);
    if (g_playback) g_playback->SetEqEnabled(enabled);
}

void EchoPlaybackSetEqBand(int bandIndex, double gainDb) {
    std::lock_guard lock(g_playback_mutex);
    if (g_playback) g_playback->SetEqBand(bandIndex, gainDb);
}

void EchoPlaybackSetEqBands(const double gainsDb[5]) {
    std::lock_guard lock(g_playback_mutex);
    if (g_playback) g_playback->SetEqBands(gainsDb);
}

void EchoPlaybackGetEqBands(double outGainsDb[5]) {
    std::lock_guard lock(g_playback_mutex);
    if (g_playback) {
        g_playback->GetEqBands(outGainsDb);
    } else {
        for (int i = 0; i < 5; ++i) outGainsDb[i] = 0.0;
    }
}
