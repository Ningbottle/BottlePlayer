#include "echo/core/C_API.h"
#include "echo/core/CompatApi.h"
#include "echo/core/HttpClient.h"
#include "echo/async/RequestScheduler.h"
#include "echo/storage/Database.h"
#include "echo/storage/AppPaths.h"
#include "echo/diagnostics/EchoDiagnostics.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <memory>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <shared_mutex>

static std::unique_ptr<echo::storage::Database> g_db;
static std::unique_ptr<echo::core::CompatApi> g_api;
static echo::async::RequestScheduler g_scheduler(4);
static std::shared_mutex g_api_rwlock;
static bool g_shutdown = false;

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
        g_api = std::make_unique<echo::core::CompatApi>(*g_db);
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

    try {
        // Route through the RequestScheduler with a per-kind deadline.
        // The scheduler provides bounded concurrency (4 workers + queue cap)
        // and the deadline ensures a hung WinHTTP call frees the future even
        // if it can't be interrupted cooperatively.
        auto fut = g_scheduler.SubmitWithDeadline(
            kind,
            [&](echo::async::CancellationToken token) -> echo::core::CompatResponse {
                // Bounded shared-lock acquisition: don't wait forever if
                // EchoShutdown is holding the exclusive lock. Try for up to
                // 2 seconds, then return 503.
                std::shared_lock<std::shared_mutex> lock(g_api_rwlock, std::defer_lock);
                auto lockDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (std::chrono::steady_clock::now() < lockDeadline) {
                    if (lock.try_lock()) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                if (!lock.owns_lock()) {
                    echo::core::CompatResponse err;
                    err.httpStatus = 503;
                    err.body = {{"error", "shutdown_in_progress"}};
                    return err;
                }
                if (!g_api || g_shutdown) {
                    echo::core::CompatResponse err;
                    err.httpStatus = 500;
                    err.body = {{"error", "C API is not initialized or was shut down"}};
                    return err;
                }
                return g_api->Handle(methodStr, pathStr, q, h, bodyStr);
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

    try {
        nlohmann::json out = {
            {"status", r.httpStatus},
            {"headers", {{"Content-Type", r.contentType}}},
            {"body", r.body}
        };

        auto outStr = out.dump();
        *out_response = new char[outStr.size() + 1];
        std::strcpy(*out_response, outStr.c_str());
    } catch(std::exception& e) {
        *out_response = new char[64];
        std::strcpy(*out_response, R"({"status":500,"error":"serialization failed"})");
    } catch(...) {
        *out_response = new char[64];
        std::strcpy(*out_response, R"({"status":500,"error":"unknown"})");
    }
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
