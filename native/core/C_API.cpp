#include "echo/core/C_API.h"
#include "echo/core/CompatApi.h"
#include "echo/core/HttpClient.h"
#include "echo/core/RequestDeadlines.h"
#include "echo/async/RequestScheduler.h"
#include "echo/storage/Database.h"
#include "echo/storage/AppPaths.h"
#include "echo/diagnostics/EchoDiagnostics.h"
#include "echo/stats/PlayStatsService.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <sstream>

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
};

static EchoContext& Ctx() {
  static EchoContext ctx;
  return ctx;
}

static const char* _dup_str(const char* s) {
    char* out = new char[std::strlen(s) + 1];
    std::strcpy(out, s);
    return out;
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
        return;
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

void EchoInitializeWithPaths(const char* app_data_dir) {
    std::unique_lock<std::shared_mutex> lock(Ctx().api_rwlock);
    Ctx().shutdown.store(false, std::memory_order_release);  // allow re-init after shutdown
    try {
        EnsureInitializedLocked(app_data_dir);
    } catch (const std::exception& e) {
        // Never let C++ exceptions cross the extern "C" FFI boundary.
        // Log and leave Ctx().api null — subsequent requests will get 500.
        Ctx().api.reset();
        Ctx().db.reset();
    } catch (...) {
        Ctx().api.reset();
        Ctx().db.reset();
    }
}

void EchoInitialize() {
    EchoInitializeWithPaths(nullptr);
}

// Returns the number of abandoned (detached) scheduler workers.
// Non-zero ⇒ DLL must NOT be unloaded (Rust should forget the Library).
// See P0-B: drop(_lib) after abandoned workers → use-after-unload.
int EchoShutdown() {
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
            return 0;
        }
        Ctx().api.reset();
        Ctx().stats.reset();
        Ctx().db.reset();
    }
    echo::core::CloseHttpConnectionPool();
    return 0;
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

// ─── Stats C API ─────────────────────────────────────────────────────────────

ECHO_C_API void EchoStatsRecordPlay(const char* json_record) {
    if (!json_record) return;
    std::shared_lock<std::shared_mutex> lock(Ctx().api_rwlock);
    if (!Ctx().stats) return;
    try {
        auto j = nlohmann::json::parse(json_record);
        echo::stats::PlayRecord r;
        r.songHash = j.value("song_hash", "");
        r.songName = j.value("song_name", "");
        r.singerName = j.value("singer_name", "");
        r.albumId = j.value("album_id", "");
        r.albumName = j.value("album_name", "");
        r.coverUrl = j.value("cover_url", "");
        r.durationSeconds = j.value("duration_seconds", 0.0);
        r.completed = j.value("completed", false);
        r.listenedSeconds = j.value("listened_seconds", 0.0);
        r.quality = j.value("quality", "");
        r.playedAtMs = j.value("played_at", 0LL);
        Ctx().stats->RecordPlay(r);
    } catch (...) {}
}

ECHO_C_API const char* EchoStatsGetSummary(const char* range) {
    std::shared_lock<std::shared_mutex> lock(Ctx().api_rwlock);
    try {
        if (!Ctx().stats) return _dup_str(R"({"total_plays":0,"total_listened_seconds":0,"unique_songs":0,"unique_artists":0,"completion_rate":0,"range":"all"})");
        return _dup_str(Ctx().stats->GetSummary(range ? range : "all").c_str());
    } catch (...) {
        return _dup_str(R"({"total_plays":0,"total_listened_seconds":0,"unique_songs":0,"unique_artists":0,"completion_rate":0,"range":"all"})");
    }
}

ECHO_C_API const char* EchoStatsGetTop(const char* dim, const char* range, int limit) {
    std::shared_lock<std::shared_mutex> lock(Ctx().api_rwlock);
    try {
        if (!Ctx().stats || !dim || !range) return _dup_str(R"({"items":[]})");
        return _dup_str(Ctx().stats->GetTop(dim, range, limit).c_str());
    } catch (...) {
        return _dup_str(R"({"items":[]})");
    }
}

ECHO_C_API const char* EchoStatsGetTimeline(const char* range) {
    std::shared_lock<std::shared_mutex> lock(Ctx().api_rwlock);
    try {
        if (!Ctx().stats || !range) return _dup_str(R"({"items":[]})");
        return _dup_str(Ctx().stats->GetTimeline(range).c_str());
    } catch (...) {
        return _dup_str(R"({"items":[]})");
    }
}

ECHO_C_API const char* EchoStatsGetRecent(int limit, int offset) {
    std::shared_lock<std::shared_mutex> lock(Ctx().api_rwlock);
    try {
        if (!Ctx().stats) return _dup_str(R"({"items":[]})");
        return _dup_str(Ctx().stats->GetRecent(limit, offset).c_str());
    } catch (...) {
        return _dup_str(R"({"items":[]})");
    }
}

ECHO_C_API const char* EchoStatsGetRecommendations(int limit) {
    std::shared_lock<std::shared_mutex> lock(Ctx().api_rwlock);
    try {
        if (!Ctx().stats) return _dup_str(R"({"items":[]})");
        return _dup_str(Ctx().stats->GetRecommendations(limit).c_str());
    } catch (...) {
        return _dup_str(R"({"items":[]})");
    }
}
