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
ECHO_C_API void EchoShutdown();
ECHO_C_API void EchoHandleRequest(const char* method, const char* path, const char* query_json, const char* headers_json, const char* body, char** out_response);
ECHO_C_API void EchoFreeString(char* str);

// FFI log callback: level (0=debug, 1=info, 2=warn, 3=error), tag, message, user_data.
typedef void (*EchoLogCallback)(int level, const char* tag, const char* msg, void* user_data);
ECHO_C_API void EchoSetLogCallback(EchoLogCallback cb, void* user_data);

// ABI placeholder for future C++ 鈫?Rust async event channel (playback state, download progress).
// Not yet wired; reserved so ABI surface is stable.
typedef void (*EchoEventCallback)(const char* event_json, void* user_data);
ECHO_C_API void EchoSetEventCallback(EchoEventCallback cb, void* user_data);

// 鈹€鈹€ Playback C API 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

typedef enum EchoPlaybackBackend {
  ECHO_PLAYBACK_MFP = 0,
  ECHO_PLAYBACK_MFS = 1,
} EchoPlaybackBackend;

ECHO_C_API bool EchoPlaybackInitialize(EchoPlaybackBackend backend);
ECHO_C_API bool EchoPlaybackPlayUrl(const char* url);
ECHO_C_API void EchoPlaybackPause(void);
ECHO_C_API void EchoPlaybackResume(void);
ECHO_C_API void EchoPlaybackStop(void);
ECHO_C_API void EchoPlaybackSeek(double seconds);
ECHO_C_API void EchoPlaybackSetVolume(double volume);
ECHO_C_API void EchoPlaybackSetRate(double rate);
ECHO_C_API const char* EchoPlaybackGetState(void);
ECHO_C_API void EchoPlaybackShutdown(void);

ECHO_C_API void EchoPlaybackSetEqEnabled(int enabled);
ECHO_C_API void EchoPlaybackSetEqBand(int bandIndex, double gainDb);
ECHO_C_API void EchoPlaybackSetEqBands(const double gainsDb[5]);
ECHO_C_API void EchoPlaybackGetEqBands(double outGainsDb[5]);

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

