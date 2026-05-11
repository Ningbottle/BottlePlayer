cmake_minimum_required(VERSION 3.24)

project(BottleMusicNative VERSION 0.1.0 LANGUAGES CXX)

option(ECHO_NATIVE_BUILD_WIN32_APP "Build the pure Win32 shell" ON)
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

add_library(EchoCore STATIC
  core/Authorization.cpp
  core/BackendFacade.cpp
  core/CatalogService.cpp
  core/CompatApi.cpp
  core/DeviceService.cpp
  core/HttpClient.cpp
  core/JsonHelpers.cpp
  core/LyricParser.cpp
  core/LyricService.cpp
  core/PlaylistService.cpp
  core/PrivilegeService.cpp
  core/RankService.cpp
  core/SearchService.cpp
  core/SongUrlService.cpp
)
target_include_directories(EchoCore PUBLIC include)
target_link_libraries(EchoCore
  PUBLIC EchoStorage nlohmann_json::nlohmann_json
  PRIVATE winhttp
)

add_library(EchoPlayback STATIC playback/PlaybackController.cpp)
target_include_directories(EchoPlayback PUBLIC include)
target_link_libraries(EchoPlayback
  PUBLIC nlohmann_json::nlohmann_json
  PRIVATE mf mfplat mfplay mfreadwrite mfuuid
)

add_library(EchoAsync STATIC
  async/EventQueue.cpp
  async/TaskScheduler.cpp
)
target_include_directories(EchoAsync PUBLIC include)

add_library(EchoImage STATIC
  image/ImageCache.cpp
  image/ImageLoader.cpp
)
target_include_directories(EchoImage PUBLIC include)
target_link_libraries(EchoImage PUBLIC EchoAsync PRIVATE windowscodecs ole32)

add_library(EchoDiagnostics STATIC diagnostics/MemorySnapshot.cpp)
target_include_directories(EchoDiagnostics PUBLIC include)
target_link_libraries(EchoDiagnostics PRIVATE psapi)

add_library(EchoWin32Layout STATIC
  win32_app/ImageSlot.cpp
  win32_app/Layout.cpp
  win32_app/LyricViewModel.cpp
  win32_app/Navigation.cpp
  win32_app/PlaybackQueue.cpp
  win32_app/PlaybackViewModel.cpp
  win32_app/SearchInput.cpp
  win32_app/SearchViewModel.cpp
)
target_link_libraries(EchoWin32Layout PUBLIC EchoCore nlohmann_json::nlohmann_json)
target_include_directories(EchoWin32Layout PUBLIC include)

add_executable(EchoCompatServer
  compat_server/CompatServer.cpp
  compat_server/main.cpp
)
target_include_directories(EchoCompatServer PRIVATE include)
target_link_libraries(EchoCompatServer PRIVATE EchoCore EchoDiagnostics ws2_32)

if(ECHO_NATIVE_BUILD_WIN32_APP)
  add_executable(EchoWin32 WIN32
    win32_app/MainWindow.cpp
    win32_app/main.cpp
  )
  target_include_directories(EchoWin32 PRIVATE include)
  target_link_libraries(EchoWin32
      PRIVATE EchoCore EchoPlayback EchoAsync EchoImage EchoDiagnostics EchoWin32Layout d2d1 dwrite windowscodecs comctl32 shell32
  )
  if(MSVC)
    add_custom_command(TARGET EchoWin32 POST_BUILD
      COMMAND mt.exe -nologo
        -manifest "${CMAKE_CURRENT_SOURCE_DIR}/win32_app/EchoWin32.manifest"
        "-outputresource:$<TARGET_FILE:EchoWin32>;#1"
      COMMENT "Embedding EchoWin32 DPI manifest"
    )
  endif()
endif()

if(ECHO_NATIVE_BUILD_TESTS)
  enable_testing()
  add_executable(EchoNativeSmokeTests tests/basic_contract_tests.cpp)
  target_include_directories(EchoNativeSmokeTests PRIVATE include)
  target_link_libraries(EchoNativeSmokeTests PRIVATE EchoCore EchoPlayback EchoAsync EchoImage EchoDiagnostics EchoWin32Layout)
  add_test(NAME EchoNativeSmokeTests COMMAND EchoNativeSmokeTests)
endif()
