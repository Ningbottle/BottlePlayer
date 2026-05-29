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
ECHO_C_API void EchoShutdown();
ECHO_C_API void EchoHandleRequest(const char* method, const char* path, const char* query_json, const char* headers_json, const char* body, char** out_response);
ECHO_C_API void EchoFreeString(char* str);

#ifdef __cplusplus
}
#endif
