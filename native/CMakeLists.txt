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
target_link_libraries(EchoCAPI PUBLIC EchoCore)

add_library(EchoPlayback STATIC playback/PlaybackController.cpp)
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

add_library(EchoWin32Layout STATIC
  win32_app/GlassPanel.cpp
  win32_app/ImageSlot.cpp
  win32_app/Layout.cpp
  win32_app/LyricViewModel.cpp
  win32_app/Navigation.cpp
  win32_app/PaperTexture.cpp
  win32_app/Painter.cpp
  win32_app/PlaybackQueue.cpp
  win32_app/PlaybackViewModel.cpp
  win32_app/RenderPipeline.cpp
  win32_app/SearchInput.cpp
  win32_app/SearchViewModel.cpp
  win32_app/Theme.cpp
)
# d2d1/d3d11/dxgi/dxguid 通过 EchoWin32Layout 公开链接，使 EchoWin32 与 EchoNativeSmokeTests
# 共享同一套 D2D/D3D11 互操作设置。dxguid 提供 IID_IDXGIDevice / IID_IDXGIFactory2 等接口 GUID。
# d2d1/d3d11/dxgi/dxguid/dwrite 通过 EchoWin32Layout 公开链接，使 EchoWin32 与 EchoNativeSmokeTests
# 共享同一套 D2D/D3D11/DWrite 互操作设置。dxguid 提供 IID 接口 GUID；dwrite 是 Painter 字体排版基础。
target_link_libraries(EchoWin32Layout PUBLIC EchoCore nlohmann_json::nlohmann_json d2d1 d3d11 dxgi dxguid dwrite)
target_include_directories(EchoWin32Layout PUBLIC include)

if(ECHO_NATIVE_BUILD_WIN32_APP)
  add_executable(EchoWin32 WIN32
    win32_app/MainWindow.cpp
    win32_app/main.cpp
  )
  target_include_directories(EchoWin32 PRIVATE include)
  target_link_libraries(EchoWin32
      PRIVATE EchoCore EchoPlayback EchoAsync EchoImage EchoDiagnostics EchoWin32Layout d2d1 dwrite windowscodecs comctl32 shell32 dwmapi
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
