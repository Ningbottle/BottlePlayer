#include "echo/core/C_API.h"
#include "echo/core/CompatApi.h"
#include "echo/storage/Database.h"
#include "echo/storage/AppPaths.h"
#include <nlohmann/json.hpp>
#include <memory>

static std::unique_ptr<echo::storage::Database> g_db;
static std::unique_ptr<echo::core::CompatApi> g_api;

void EchoInitialize() {
    if(!g_db) {
        g_db = std::make_unique<echo::storage::Database>();
        g_db->Open(echo::storage::GetDefaultDatabasePath());
        g_db->Initialize();
        g_api = std::make_unique<echo::core::CompatApi>(*g_db);
    }
}

void EchoShutdown() {
    g_api.reset();
    g_db.reset();
}

void EchoHandleRequest(const char* method, const char* path, const char* query_json, const char* headers_json, const char* body, char** out_response) {
    if(!g_api) EchoInitialize();
    
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
        r = g_api->Handle(method ? method : "GET", path ? path : "/", q, h, body ? body : "");
    } catch(std::exception& e) {
        r.httpStatus = 500;
        r.body = {{"error", e.what()}};
    } catch(...) {
        r.httpStatus = 500;
        r.body = {{"error", "Unknown"}};
    }
    
    nlohmann::json out = {
        {"status", r.httpStatus},
        {"headers", {{"Content-Type", r.contentType}}},
        {"body", r.body}
    };
    
    auto outStr = out.dump();
    *out_response = new char[outStr.size() + 1];
    strcpy(*out_response, outStr.c_str());
}

void EchoFreeString(char* str) {
    delete[] str;
}
