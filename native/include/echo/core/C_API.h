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

// ABI placeholder for future C++ → Rust async event channel (playback state, download progress).
// Not yet wired; reserved so ABI surface is stable.
typedef void (*EchoEventCallback)(const char* event_json, void* user_data);
ECHO_C_API void EchoSetEventCallback(EchoEventCallback cb, void* user_data);

#ifdef __cplusplus
}
#endif
