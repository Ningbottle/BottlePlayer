#include "echo/core/C_API.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

class IsolatedCurrentDirectory {
 public:
  IsolatedCurrentDirectory() : original_(std::filesystem::current_path()) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("bottlemusic-capi-boundary-" + std::to_string(stamp));
    std::filesystem::create_directories(path_);
    std::filesystem::current_path(path_);
  }

  ~IsolatedCurrentDirectory() {
    std::error_code ignored;
    std::filesystem::current_path(original_, ignored);
    std::filesystem::remove_all(path_, ignored);
  }

  const std::filesystem::path& path() const { return path_; }

  bool isEmpty() const {
    std::error_code error;
    return std::filesystem::is_empty(path_, error) && !error;
  }

 private:
  std::filesystem::path original_;
  std::filesystem::path path_;
};

bool RejectedPathWithoutDatabase(
    const char* path,
    const IsolatedCurrentDirectory& isolated) {
  const int status = EchoInitializeWithPathsV2(path);
  if (status == 0) {
    (void)EchoShutdown();
    std::cerr << "RED: empty/whitespace app_data_dir was accepted" << std::endl;
    return false;
  }
  char* message = EchoGetLastError();
  const bool hasError = message != nullptr && std::strlen(message) > 0;
  EchoFreeString(message);
  if (!hasError) {
    std::cerr << "initialization failure did not expose an error" << std::endl;
    return false;
  }
  if (!isolated.isEmpty()) {
    std::cerr << "rejected initialization created an artifact under the temporary CWD"
              << std::endl;
    return false;
  }
  if (EchoShutdown() != 0) return false;
  return true;
}

bool ExpectOversizedRequestField(
    const char* expectedField,
    const char* method,
    const char* path,
    const char* query,
    const char* headers,
    const char* body) {
  char* response = nullptr;
  try {
    EchoHandleRequest(method, path, query, headers, body, &response);
  } catch (...) {
    std::cerr << "RED: request limit exception escaped C ABI for " << expectedField
              << std::endl;
    return false;
  }
  const std::string text = response ? response : "";
  EchoFreeString(response);
  return text.find("\"status\":413") != std::string::npos &&
         text.find(std::string("\"field\":\"") + expectedField + "\"") !=
             std::string::npos;
}

}  // namespace

int main() {
  IsolatedCurrentDirectory isolated;
  std::cout << "[CapiBoundary] Testing invalid UTF-8 initialization path..." << std::endl;

  // C callers may pass arbitrary byte strings. On Windows the path constructor
  // converts UTF-8 to UTF-16 and rejects this malformed sequence.
  const char invalidUtf8Path[] = {static_cast<char>(0xFF), '\0'};
  int status = -1;
  try {
    status = EchoInitializeWithPathsV2(invalidUtf8Path);
  } catch (const std::exception& error) {
    std::cerr << "RED: C++ exception escaped EchoInitializeWithPathsV2: "
              << error.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "RED: unknown exception escaped EchoInitializeWithPathsV2"
              << std::endl;
    return 1;
  }

  assert(status != 0);
  char* message = EchoGetLastError();
  assert(message != nullptr);
  assert(std::strlen(message) > 0);
  EchoFreeString(message);

  // A rejected initialization must still leave the process C API able to
  // complete shutdown before the next isolated path case.
  assert(EchoShutdown() == 0);
  std::cout << "  invalid UTF-8 rejected without unwinding across C ABI" << std::endl;

  // An explicit empty path must not silently become a relative database path
  // in the caller's working directory. The test changes CWD only to a fresh,
  // test-owned temporary directory and inspects only that directory.
  std::cout << "[CapiBoundary] Testing empty and whitespace paths..." << std::endl;
  assert(RejectedPathWithoutDatabase("", isolated));
  assert(RejectedPathWithoutDatabase(" \t\r\n", isolated));
  const std::string tooLongPath(ECHO_C_API_MAX_APP_DATA_DIR_BYTES + 1, 'x');
  assert(RejectedPathWithoutDatabase(tooLongPath.c_str(), isolated));
  std::cout << "  empty and whitespace paths fail closed" << std::endl;

  std::cout << "[CapiBoundary] Testing bounded request strings..." << std::endl;
  const std::string oversizedMethod(ECHO_C_API_MAX_METHOD_BYTES + 1, 'M');
  const std::string oversizedPath(ECHO_C_API_MAX_PATH_BYTES + 1, 'p');
  const std::string oversizedQuery(ECHO_C_API_MAX_QUERY_JSON_BYTES + 1, 'q');
  const std::string oversizedHeaders(ECHO_C_API_MAX_HEADERS_JSON_BYTES + 1, 'h');
  const std::string oversizedBody(ECHO_C_API_MAX_BODY_BYTES + 1, 'b');
  assert(ExpectOversizedRequestField(
      "method", oversizedMethod.c_str(), "/", nullptr, nullptr, nullptr));
  assert(ExpectOversizedRequestField(
      "path", "GET", oversizedPath.c_str(), nullptr, nullptr, nullptr));
  assert(ExpectOversizedRequestField(
      "query_json", "GET", "/", oversizedQuery.c_str(), nullptr, nullptr));
  assert(ExpectOversizedRequestField(
      "headers_json", "GET", "/", nullptr, oversizedHeaders.c_str(), nullptr));
  assert(ExpectOversizedRequestField(
      "body", "POST", "/", nullptr, nullptr, oversizedBody.c_str()));
  std::cout << "  oversized strings return structured 413 responses" << std::endl;

  const auto explicitDataDir = isolated.path() / "explicit-data";
  const auto explicitDataDirUtf8 = explicitDataDir.string();
  assert(EchoInitializeWithPathsV2(explicitDataDirUtf8.c_str()) == 0);
  assert(std::filesystem::exists(explicitDataDir / "bottlemusic.db"));
  assert(EchoStatsRecordPlay(oversizedBody.c_str()) == kEchoStatsBadJson);
  const std::string oversizedRange(ECHO_C_API_MAX_PATH_BYTES + 1, 'r');
  const char* stats = EchoStatsGetSummary(oversizedRange.c_str());
  assert(stats != nullptr);
  assert(std::strstr(stats, "stats_input_too_large") != nullptr);
  EchoFreeString(const_cast<char*>(stats));
  assert(EchoShutdown() == 0);
  std::cout << "  valid explicit path reinitializes the context" << std::endl;
  return 0;
}
