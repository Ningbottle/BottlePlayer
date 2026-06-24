cmake_minimum_required(VERSION 3.24)

project(BottleMusicNative VERSION 1.0.0 LANGUAGES CXX)

option(ECHO_NATIVE_BUILD_TESTS "Build native smoke tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
  add_compile_options(/utf-8)
endif()

set(ECHO_NATIVE_VCPKG_INSTALLED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows")
if(EXISTS "${ECHO_NATIVE_VCPKG_INSTALLED_DIR}")
  list(PREPEND CMAKE_PREFIX_PATH "${ECHO_NATIVE_VCPKG_INSTALLED_DIR}")
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG QUIET)
find_package(SQLite3 QUIET)
find_package(httplib CONFIG QUIET)
find_package(spdlog CONFIG QUIET)
find_package(wil CONFIG QUIET)

add_library(EchoStorage STATIC
  storage/AppPaths.cpp
  storage/ApiCache.cpp
  storage/Database.cpp
  storage/DeviceRepository.cpp
  storage/SessionRepository.cpp
  storage/SettingsRepository.cpp
)
target_include_directories(EchoStorage PUBLIC include)
target_link_libraries(EchoStorage PUBLIC nlohmann_json::nlohmann_json)
if(unofficial-sqlite3_FOUND)
  target_compile_definitions(EchoStorage PUBLIC ECHO_NATIVE_HAS_SQLITE=1)
  target_link_libraries(EchoStorage PUBLIC unofficial::sqlite3::sqlite3)
elseif(SQLite3_FOUND)
  target_compile_definitions(EchoStorage PUBLIC ECHO_NATIVE_HAS_SQLITE=1)
  target_link_libraries(EchoStorage PUBLIC SQLite::SQLite3)
else()
  message(WARNING "SQLite3 development files were not found; EchoStorage will use the JSON-file fallback.")
endif()

add_library(EchoDiagnostics STATIC
  diagnostics/EchoDiagnostics.cpp
  diagnostics/MemorySnapshot.cpp
  diagnostics/ScopedTimer.cpp
  diagnostics/Redaction.cpp
)
target_include_directories(EchoDiagnostics PUBLIC include)
target_link_libraries(EchoDiagnostics PRIVATE psapi)

add_library(EchoCore STATIC
  core/Authorization.cpp
  core/BackendFacade.cpp
  core/CatalogService.cpp
  core/CompatApi.cpp
  core/CompatRequestContext.cpp
  core/compat_routes/DiagnosticsRoutes.cpp
  core/compat_routes/LoginRoutes.cpp
  core/compat_routes/UserRoutes.cpp
  core/compat_routes/PlaylistRoutes.cpp
  core/compat_routes/MediaRoutes.cpp
  core/compat_routes/YouthVipRoutes.cpp
  core/compat_routes/RegisterRoutes.cpp
  core/DeviceService.cpp
  core/DeviceRegisterService.cpp
  core/HomeService.cpp
  core/HttpClient.cpp
  core/HttpUtils.cpp
  core/Crypto.cpp
  core/KuGouAndroidRequest.cpp
  core/KuGouProfile.cpp
  core/LoginService.cpp
  core/JsonHelpers.cpp
  core/LyricParser.cpp
  core/LyricService.cpp
  core/PlaylistService.cpp
  core/PrivilegeService.cpp
  core/RankService.cpp
  core/SearchService.cpp
  core/SongUrlService.cpp
  core/StringUtils.cpp
  core/UserService.cpp
  core/SongService.cpp
  core/PlayHistoryService.cpp
  core/UserCloudService.cpp
  core/C_API.cpp
)
target_include_directories(EchoCore PUBLIC include)
target_link_libraries(EchoCore
  PUBLIC EchoStorage EchoDiagnostics EchoAsync nlohmann_json::nlohmann_json
  PRIVATE winhttp bcrypt crypt32
)

add_library(EchoCAPI SHARED core/C_API.cpp)
target_include_directories(EchoCAPI PUBLIC include)
target_link_libraries(EchoCAPI PUBLIC EchoCore EchoPlayback)

add_library(EchoPlayback STATIC
  playback/PlaybackController.cpp
  playback/PlaybackControllerMFP.cpp
  playback/PlaybackControllerMFS.cpp
  playback/BiquadFilter.cpp
  playback/EqualizerMFT.cpp
)
target_include_directories(EchoPlayback PUBLIC include)
target_link_libraries(EchoPlayback
  PUBLIC nlohmann_json::nlohmann_json
  PRIVATE mf mfplat mfplay mfreadwrite mfuuid
)

add_library(EchoAsync STATIC
  async/EventQueue.cpp
  async/TaskScheduler.cpp
  async/RequestScheduler.cpp
)
target_include_directories(EchoAsync PUBLIC include)

add_library(EchoImage STATIC
  image/ImageCache.cpp
  image/ImageLoader.cpp
)
target_include_directories(EchoImage PUBLIC include)
target_link_libraries(EchoImage PUBLIC EchoAsync PRIVATE windowscodecs ole32)

if(ECHO_NATIVE_BUILD_TESTS)
  enable_testing()

  # Full smoke test (covers route/crypto/service contracts; the former
  # win32_app layout/view-model sections were removed with EchoWin32).
  add_executable(EchoNativeSmokeTests tests/basic_contract_tests.cpp)
  target_include_directories(EchoNativeSmokeTests PRIVATE include)
  target_link_libraries(EchoNativeSmokeTests PRIVATE EchoCore EchoPlayback EchoAsync EchoImage EchoDiagnostics)
  add_test(NAME EchoNativeSmokeTests COMMAND EchoNativeSmokeTests)

  # PlaybackControllerMFS contract test (S4 Phase 4.1a checkpoint).
  add_executable(EchoPlaybackMfsTest tests/playback_controller_mfs_test.cpp)
  target_include_directories(EchoPlaybackMfsTest PRIVATE include)
  target_link_libraries(EchoPlaybackMfsTest PRIVATE EchoCore EchoPlayback)
  add_test(NAME EchoPlaybackMfsTest COMMAND EchoPlaybackMfsTest)

  # Independent contract test targets (P1: split from smoke for faster feedback)
  add_executable(EchoRouteContractTest tests/route_contract_test.cpp)
  target_include_directories(EchoRouteContractTest PRIVATE include)
  target_link_libraries(EchoRouteContractTest PRIVATE EchoCore EchoStorage)
  add_test(NAME EchoRouteContractTest COMMAND EchoRouteContractTest)

  add_executable(EchoSongUrlContractTest tests/songurl_contract_test.cpp)
  target_include_directories(EchoSongUrlContractTest PRIVATE include)
  target_link_libraries(EchoSongUrlContractTest PRIVATE EchoCore)
  add_test(NAME EchoSongUrlContractTest COMMAND EchoSongUrlContractTest)

  add_executable(EchoPlaylistContractTest tests/playlist_contract_test.cpp)
  target_include_directories(EchoPlaylistContractTest PRIVATE include)
  target_link_libraries(EchoPlaylistContractTest PRIVATE EchoCore)
  add_test(NAME EchoPlaylistContractTest COMMAND EchoPlaylistContractTest)

  add_executable(EchoProfileSignatureContractTest tests/profile_signature_contract_test.cpp)
  target_include_directories(EchoProfileSignatureContractTest PRIVATE include)
  target_link_libraries(EchoProfileSignatureContractTest PRIVATE EchoCore)
  add_test(NAME EchoProfileSignatureContractTest COMMAND EchoProfileSignatureContractTest)

  add_executable(EchoHomeContractTest tests/home_contract_test.cpp)
  target_include_directories(EchoHomeContractTest PRIVATE include)
  target_link_libraries(EchoHomeContractTest PRIVATE EchoCore)
  add_test(NAME EchoHomeContractTest COMMAND EchoHomeContractTest)

  add_executable(EchoHttpClientResilienceTest tests/http_client_resilience_test.cpp)
  target_include_directories(EchoHttpClientResilienceTest PRIVATE include)
  target_link_libraries(EchoHttpClientResilienceTest PRIVATE EchoCore ws2_32)
  add_test(NAME EchoHttpClientResilienceTest COMMAND EchoHttpClientResilienceTest)

  add_executable(EchoRequestSchedulerResilienceTest tests/request_scheduler_resilience_test.cpp)
  target_include_directories(EchoRequestSchedulerResilienceTest PRIVATE include)
  target_link_libraries(EchoRequestSchedulerResilienceTest PRIVATE EchoCore EchoAsync)
  add_test(NAME EchoRequestSchedulerResilienceTest COMMAND EchoRequestSchedulerResilienceTest)

  add_executable(EchoBiquadFilterTest tests/biquad_filter_test.cpp)
  target_include_directories(EchoBiquadFilterTest PRIVATE include)
  target_link_libraries(EchoBiquadFilterTest PRIVATE EchoCore EchoPlayback)
  add_test(NAME EchoBiquadFilterTest COMMAND EchoBiquadFilterTest)

  set(ECHO_NATIVE_TESTS
    EchoNativeSmokeTests
    EchoRouteContractTest
    EchoSongUrlContractTest
    EchoPlaylistContractTest
    EchoProfileSignatureContractTest
    EchoHomeContractTest
    EchoHttpClientResilienceTest
    EchoRequestSchedulerResilienceTest
  )
  # 测试进程也需要找到 vcpkg 的动态库；把 bin 目录临时挂到 PATH，
  # 避免普通 PowerShell/CTest 启动时报 0xc0000135。
  set_tests_properties(${ECHO_NATIVE_TESTS} PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${ECHO_NATIVE_VCPKG_INSTALLED_DIR}/bin")
endif()
