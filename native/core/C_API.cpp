#include "echo/core/C_API.h"
#include "echo/core/CompatApi.h"
#include "echo/storage/Database.h"
#include "echo/storage/AppPaths.h"
#include "echo/diagnostics/EchoDiagnostics.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <shared_mutex>

static std::unique_ptr<echo::storage::Database> g_db;
static std::unique_ptr<echo::core::CompatApi> g_api;
// Shared/exclusive lock guarding g_db/g_api:
//   - EchoHandleRequest holds it SHARED  → many requests run Handle() in parallel;
//   - EchoInitialize*/EchoShutdown hold it EXCLUSIVE → mutate the globals alone,
//     and (for shutdown) wait for all in-flight requests to drain first.
// Concurrency inside Handle() is provided by Database's own mutex and by the
// per-request service objects, so no per-request work is serialized here.
static std::shared_mutex g_api_rwlock;
// Set once EchoShutdown runs; prevents a late request from resurrecting the
// backend after an explicit shutdown (e.g. during window-close teardown).
static bool g_shutdown = false;

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
    EnsureInitializedLocked(app_data_dir);
}

void EchoInitialize() {
    EchoInitializeWithPaths(nullptr);
}

void EchoShutdown() {
    // Exclusive lock: blocks until every in-flight EchoHandleRequest shared-lock
    // holder has finished, so the backend is never torn down mid-request.
    std::unique_lock<std::shared_mutex> lock(g_api_rwlock);
    g_shutdown = true;
    g_api.reset();
    g_db.reset();
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
    try {
        // Shared lock: concurrent requests execute Handle() in parallel. g_api is
        // guaranteed non-null here once EchoInitialize[WithPaths] ran at startup;
        // if init never ran or shutdown already happened, g_api is null → 500.
        std::shared_lock<std::shared_mutex> lock(g_api_rwlock);
        if (!g_api) {
            r.httpStatus = 500;
            r.body = {{"error", "C API is not initialized or was shut down"}};
        } else {
            r = g_api->Handle(method ? method : "GET", path ? path : "/", q, h, body ? body : "");
        }
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
