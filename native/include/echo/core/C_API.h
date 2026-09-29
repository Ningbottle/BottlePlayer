#pragma once

#ifdef _WIN32
#define ECHO_C_API __declspec(dllexport)
#else
#define ECHO_C_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// EchoHandleRequest receives NUL-terminated UTF-8 strings with no separate
// length argument. These generous limits bound JSON parsing, scheduler copies,
// and WinHTTP's DWORD-sized path/header/body lengths. The body budget matches
// HttpClient's default 10 MiB response budget used by native routes; metadata
// limits are larger than current compatibility route names and cursors need.
#define ECHO_C_API_MAX_METHOD_BYTES (32u)
#define ECHO_C_API_MAX_PATH_BYTES (16u * 1024u)
#define ECHO_C_API_MAX_QUERY_JSON_BYTES (1u * 1024u * 1024u)
#define ECHO_C_API_MAX_HEADERS_JSON_BYTES (256u * 1024u)
#define ECHO_C_API_MAX_BODY_BYTES (10u * 1024u * 1024u)
#define ECHO_C_API_MAX_RESPONSE_BYTES (64u * 1024u * 1024u)
#define ECHO_C_API_MAX_APP_DATA_DIR_BYTES (32767u)
#define ECHO_C_API_MAX_STATS_JSON_BYTES (256u * 1024u)
#define ECHO_C_API_MAX_STATS_ROWS (100)

// Legacy wrappers keep the original ABI for existing consumers.
ECHO_C_API void EchoInitialize();
ECHO_C_API void EchoInitializeWithPaths(const char* app_data_dir);
// Versioned initialization returns 0 on success. On failure,
// EchoGetLastError returns an owned message released with EchoFreeString.
ECHO_C_API int EchoInitializeV2();
ECHO_C_API int EchoInitializeWithPathsV2(const char* app_data_dir);
ECHO_C_API char* EchoGetLastError();
// Returns 0 only when teardown completed and the DLL is safe to unload.
// Non-zero means workers or lock holders may still execute inside it.
ECHO_C_API int EchoShutdown();
// Inputs above the ECHO_C_API_MAX_* request limits receive HTTP 413 before
// route dispatch. out_response is either EchoFreeString-owned or null if even
// the small emergency response cannot be allocated.
ECHO_C_API void EchoHandleRequest(const char* method, const char* path, const char* query_json, const char* headers_json, const char* body, char** out_response);
ECHO_C_API void EchoFreeString(char* str);

// FFI log callback: level (0=debug, 1=info, 2=warn, 3=error), tag, message, user_data.
typedef void (*EchoLogCallback)(int level, const char* tag, const char* msg, void* user_data);
ECHO_C_API void EchoSetLogCallback(EchoLogCallback cb, void* user_data);

// ─── Stats C API ─────────────────────────────────────────────────────────────
// B06: EchoStatsRecordPlay reports its outcome instead of silently ignoring
// failures. The int result is one of the EchoStatsRecordStatus codes below.
// Note: a shorter-than-threshold listen is a NORMAL outcome (kEchoStatsRecordBelowThreshold),
// not an error — playback must never be blocked by statistics.
enum EchoStatsRecordStatus {
    kEchoStatsRecorded = 0,
    kEchoStatsRecordBelowThreshold = 1,
    kEchoStatsInvalidRecord = 2,
    kEchoStatsBadJson = 3,
    kEchoStatsNotInitialized = 4,
    kEchoStatsStorageError = 5,
};
ECHO_C_API int EchoStatsRecordPlay(const char* json_record);
// The EchoStatsGet* readers append "degraded":true plus an "error" code to
// their (structurally unchanged) result when the read could not be served —
// an empty-looking stats payload is otherwise indistinguishable from a real
// empty database (B06).
ECHO_C_API const char* EchoStatsGetSummary(const char* range);
ECHO_C_API const char* EchoStatsGetTop(const char* dim, const char* range, int limit);
ECHO_C_API const char* EchoStatsGetTimeline(const char* range);
ECHO_C_API const char* EchoStatsGetRecent(int limit, int offset);
ECHO_C_API const char* EchoStatsGetRecommendations(int limit);

#ifdef __cplusplus
}
#endif
