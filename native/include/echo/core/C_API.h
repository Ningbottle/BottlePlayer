#pragma once

#ifdef _WIN32
#define ECHO_C_API __declspec(dllexport)
#else
#define ECHO_C_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

ECHO_C_API void EchoInitialize();
ECHO_C_API void EchoInitializeWithPaths(const char* app_data_dir);
// Returns abandoned worker count. Non-zero ⇒ do not unload the DLL
// (detached C++ workers may still execute inside it). See P0-B.
ECHO_C_API int EchoShutdown();
ECHO_C_API void EchoHandleRequest(const char* method, const char* path, const char* query_json, const char* headers_json, const char* body, char** out_response);
ECHO_C_API void EchoFreeString(char* str);

// FFI log callback: level (0=debug, 1=info, 2=warn, 3=error), tag, message, user_data.
typedef void (*EchoLogCallback)(int level, const char* tag, const char* msg, void* user_data);
ECHO_C_API void EchoSetLogCallback(EchoLogCallback cb, void* user_data);

// ─── Stats C API ─────────────────────────────────────────────────────────────
ECHO_C_API void EchoStatsRecordPlay(const char* json_record);
ECHO_C_API const char* EchoStatsGetSummary(const char* range);
ECHO_C_API const char* EchoStatsGetTop(const char* dim, const char* range, int limit);
ECHO_C_API const char* EchoStatsGetTimeline(const char* range);
ECHO_C_API const char* EchoStatsGetRecent(int limit, int offset);
ECHO_C_API const char* EchoStatsGetRecommendations(int limit);

#ifdef __cplusplus
}
#endif
