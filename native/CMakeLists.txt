cmake_minimum_required(VERSION 3.24)

project(BottleMusicNative VERSION 1.0.0 LANGUAGES CXX)

option(ECHO_NATIVE_BUILD_TESTS "Build native smoke tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Stage 0 (vip-stability remediation plan): stamp a hash of the native sources
# into the diagnostics library so every crash report names the exact tree it
# was built from. History must map faults to builds (audit G3: no build hash,
# no dump, no stack → RTC#2 unattributable).
file(GLOB_RECURSE ECHO_NATIVE_ALL_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
list(FILTER ECHO_NATIVE_ALL_SOURCES EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/out/")
list(SORT ECHO_NATIVE_ALL_SOURCES)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" ECHO_NATIVE_BUILD_HASH)
foreach(_echo_src IN LISTS ECHO_NATIVE_ALL_SOURCES)
  file(SHA256 "${_echo_src}" _echo_src_hash)
  string(APPEND ECHO_NATIVE_BUILD_HASH "${_echo_src_hash}")
endforeach()
string(SHA256 ECHO_NATIVE_BUILD_HASH "${ECHO_NATIVE_BUILD_HASH}")

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

set(ECHO_NATIVE_SQLITE_AVAILABLE FALSE)

add_library(EchoStorage STATIC
  storage/AppPaths.cpp
  storage/ApiCache.cpp
  storage/Database.cpp
  storage/DeviceRepository.cpp
  storage/SessionRepository.cpp
  storage/SettingsRepository.cpp
)
target_include_directories(EchoStorage PUBLIC include)
target_link_libraries(EchoStorage
  PUBLIC nlohmann_json::nlohmann_json
  PRIVATE crypt32
)
if(unofficial-sqlite3_FOUND)
  set(ECHO_NATIVE_SQLITE_AVAILABLE TRUE)
  target_compile_definitions(EchoStorage PUBLIC ECHO_NATIVE_HAS_SQLITE=1)
  target_link_libraries(EchoStorage PUBLIC unofficial::sqlite3::sqlite3)
elseif(SQLite3_FOUND)
  set(ECHO_NATIVE_SQLITE_AVAILABLE TRUE)
  target_compile_definitions(EchoStorage PUBLIC ECHO_NATIVE_HAS_SQLITE=1)
  target_link_libraries(EchoStorage PUBLIC SQLite::SQLite3)
else()
  # P6: Packaging must not ship silent JSON-file fallback. Release (single-config
  # CMAKE_BUILD_TYPE, multi-config trees that include Release, or preset
  # bottlemusic-release) fails configure without SQLite. Debug-only builds keep
  # WARNING + fallback for local convenience.
  set(_echo_sqlite_missing_fatal FALSE)
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(_echo_sqlite_missing_fatal TRUE)
  endif()
  if(CMAKE_CONFIGURATION_TYPES)
    list(FIND CMAKE_CONFIGURATION_TYPES "Release" _echo_release_cfg_idx)
    if(NOT _echo_release_cfg_idx EQUAL -1)
      set(_echo_sqlite_missing_fatal TRUE)
    endif()
    unset(_echo_release_cfg_idx)
  endif()
  if(_echo_sqlite_missing_fatal)
    message(FATAL_ERROR
      "SQLite3 development files were not found. "
      "Release/packaging builds require SQLite (ECHO_NATIVE_HAS_SQLITE); "
      "silent JSON-file fallback is not allowed. "
      "Install via vcpkg (unofficial-sqlite3) or provide the SQLite3 CMake package.")
  else()
    message(WARNING "SQLite3 development files were not found; EchoStorage will use the JSON-file fallback.")
  endif()
  unset(_echo_sqlite_missing_fatal)
endif()

add_library(EchoDiagnostics STATIC
  diagnostics/EchoDiagnostics.cpp
  diagnostics/MemorySnapshot.cpp
  diagnostics/ScopedTimer.cpp
  diagnostics/Redaction.cpp
  diagnostics/CrashCapture.cpp
)
target_include_directories(EchoDiagnostics PUBLIC include)
target_link_libraries(EchoDiagnostics PRIVATE psapi)
# dbghelp powers the crash forensics (MiniDumpWriteDump + symbol walk).
target_link_libraries(EchoDiagnostics PRIVATE dbghelp)
target_compile_definitions(EchoDiagnostics PRIVATE
  ECHO_CRASH_BUILD_HASH="${ECHO_NATIVE_BUILD_HASH}")

# Release/RelWithDebInfo must ship PDBs (audit G3: no symbols → no
# attribution). NMake single-config generators honor CMAKE_BUILD_TYPE;
# multi-config generators (VS) honor the config check below.
set(CMAKE_PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/pdb/Release")
set(CMAKE_PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/pdb/RelWithDebInfo")
if(MSVC)
  foreach(_echo_lang C CXX)
    string(APPEND CMAKE_${_echo_lang}_FLAGS_RELEASE " /Zi")
    string(APPEND CMAKE_${_echo_lang}_FLAGS_RELWITHDEBINFO " /Zi")
  endforeach()
  # /Zi alone produces a compiler PDB the linker never references (COFF-group
  # debug dir only, no CodeView entry) — SymInitialize can't map it and the
  # archived PDB would be an orphan. /DEBUG writes the CodeView entry;
  # /OPT:REF/ICF restore the dead-strip/COMDAT folding that /DEBUG turns off.
  foreach(_echo_linker EXE SHARED STATIC)
    string(APPEND CMAKE_${_echo_linker}_LINKER_FLAGS_RELEASE
      " /DEBUG /OPT:REF /OPT:ICF")
    string(APPEND CMAKE_${_echo_linker}_LINKER_FLAGS_RELWITHDEBINFO
      " /OPT:REF /OPT:ICF")
  endforeach()
endif()
if(CMAKE_CONFIGURATION_TYPES)
  # VS multi-config: EXE/PDB land side by side per config already; copy them
  # into the archive dir per build.
  add_custom_target(native_pdb_archive ALL
    COMMAND ${CMAKE_COMMAND}
      -DCONFIG=$<CONFIG>
      -DBINARY_DIR=${CMAKE_BINARY_DIR}
      -DARCHIVE_DIR=${CMAKE_BINARY_DIR}/pdb/$<CONFIG>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ArchivePdb.cmake
    COMMENT "Archiving PDBs for symbol retention")
  add_dependencies(native_pdb_archive EchoCAPI EchoNativeSmokeTests)
elseif(CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo")
  add_custom_target(native_pdb_archive ALL
    COMMAND ${CMAKE_COMMAND}
      -DCONFIG=${CMAKE_BUILD_TYPE}
      -DBINARY_DIR=${CMAKE_BINARY_DIR}
      -DARCHIVE_DIR=${CMAKE_BINARY_DIR}/pdb/${CMAKE_BUILD_TYPE}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ArchivePdb.cmake
    COMMENT "Archiving PDBs for symbol retention")
  add_dependencies(native_pdb_archive EchoCAPI EchoNativeSmokeTests)
endif()

# Stage 0 fault-forensics probe: controlled fault generator for validating the
# minidump/report pipeline end to end. NOT a CTest entry — it crashes by
# design. Run manually:
#   CrashForensicsProbe.exe <outdir>            (SEH access-violation path)
# Expect <outdir>/crash-*.dmp + crash-*.txt with build hash + symbolized stack.
add_executable(CrashForensicsProbe tools/crash_forensics_probe.cpp)
target_include_directories(CrashForensicsProbe PRIVATE include)
target_link_libraries(CrashForensicsProbe PRIVATE EchoDiagnostics)

add_library(EchoAsync STATIC
  async/EventQueue.cpp
  async/TaskScheduler.cpp
  async/RequestScheduler.cpp
  async/RequestWatchdog.cpp
)
target_include_directories(EchoAsync PUBLIC include)

# EchoCore: services + CompatApi only. C_API.cpp lives solely in EchoCAPI
# (was dual-compiled in EchoCore + EchoCAPI — P2-L).
add_library(EchoCore STATIC
  core/Authorization.cpp
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
  stats/PlayStatsService.cpp
)
target_include_directories(EchoCore PUBLIC include)
target_link_libraries(EchoCore
  PUBLIC EchoStorage EchoDiagnostics EchoAsync nlohmann_json::nlohmann_json
  PRIVATE winhttp bcrypt crypt32
)

add_library(EchoCAPI SHARED core/C_API.cpp)
target_include_directories(EchoCAPI PUBLIC include)
target_link_libraries(EchoCAPI PUBLIC EchoCore)

add_library(EchoImage STATIC
  image/ImageCache.cpp
  image/ImageLoader.cpp
)
target_include_directories(EchoImage PUBLIC include)
target_link_libraries(EchoImage PUBLIC EchoAsync PRIVATE windowscodecs ole32)

if(ECHO_NATIVE_BUILD_TESTS)
  enable_testing()

  # Full smoke test (covers route/crypto/service contracts).
  add_executable(EchoNativeSmokeTests tests/basic_contract_tests.cpp)
  target_include_directories(EchoNativeSmokeTests PRIVATE include)
  target_link_libraries(EchoNativeSmokeTests PRIVATE EchoCore EchoAsync EchoImage EchoDiagnostics)
  add_test(NAME EchoNativeSmokeTests COMMAND EchoNativeSmokeTests)

  # Independent contract test targets
  add_executable(EchoRouteContractTest tests/route_contract_test.cpp)
  target_include_directories(EchoRouteContractTest PRIVATE include)
  target_link_libraries(EchoRouteContractTest PRIVATE EchoCore EchoStorage)
  add_test(NAME EchoRouteContractTest COMMAND EchoRouteContractTest)

  add_executable(EchoDeviceIdentityContractTest tests/device_identity_contract_test.cpp)
  target_include_directories(EchoDeviceIdentityContractTest PRIVATE include)
  target_link_libraries(EchoDeviceIdentityContractTest PRIVATE EchoCore EchoStorage)
  add_test(NAME EchoDeviceIdentityContractTest COMMAND EchoDeviceIdentityContractTest)

  add_executable(EchoSongUrlContractTest tests/songurl_contract_test.cpp)
  target_include_directories(EchoSongUrlContractTest PRIVATE include)
  target_link_libraries(EchoSongUrlContractTest PRIVATE EchoCore)
  add_test(NAME EchoSongUrlContractTest COMMAND EchoSongUrlContractTest)

  add_executable(EchoPlaylistContractTest tests/playlist_contract_test.cpp)
  target_include_directories(EchoPlaylistContractTest PRIVATE include)
  target_link_libraries(EchoPlaylistContractTest PRIVATE EchoCore)
  add_test(NAME EchoPlaylistContractTest COMMAND EchoPlaylistContractTest)

  add_executable(EchoYouthVipContractTest tests/youth_vip_contract_test.cpp)
  target_include_directories(EchoYouthVipContractTest PRIVATE include)
  target_link_libraries(EchoYouthVipContractTest PRIVATE EchoCore)
  add_test(NAME EchoYouthVipContractTest COMMAND EchoYouthVipContractTest)

  add_executable(EchoLoginContractTest tests/login_contract_test.cpp)
  target_include_directories(EchoLoginContractTest PRIVATE include)
  target_link_libraries(EchoLoginContractTest PRIVATE EchoCore)
  add_test(NAME EchoLoginContractTest COMMAND EchoLoginContractTest)

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
  # Stage 3a structural test reads HttpClient.cpp to pin "the watchdog
  # action never closes request handles".
  target_compile_definitions(EchoHttpClientResilienceTest PRIVATE
    ECHO_NATIVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME EchoHttpClientResilienceTest COMMAND EchoHttpClientResilienceTest)

  add_executable(EchoRequestSchedulerResilienceTest tests/request_scheduler_resilience_test.cpp)
  target_include_directories(EchoRequestSchedulerResilienceTest PRIVATE include)
  target_link_libraries(EchoRequestSchedulerResilienceTest PRIVATE EchoCore EchoAsync)
  add_test(NAME EchoRequestSchedulerResilienceTest COMMAND EchoRequestSchedulerResilienceTest)

  # Crash forensics contract (Stage 0): build hash + report generation.
  add_executable(EchoCrashCaptureContractTest tests/crash_capture_contract_test.cpp)
  target_include_directories(EchoCrashCaptureContractTest PRIVATE include)
  target_link_libraries(EchoCrashCaptureContractTest PRIVATE EchoDiagnostics)
  add_test(NAME EchoCrashCaptureContractTest COMMAND EchoCrashCaptureContractTest)

  # Storage Actor lifecycle + WAL concurrent access (serialized; no TLS).
  add_executable(EchoDatabaseActorLifecycleTest tests/database_actor_lifecycle_test.cpp)
  target_include_directories(EchoDatabaseActorLifecycleTest PRIVATE include)
  target_link_libraries(EchoDatabaseActorLifecycleTest PRIVATE EchoStorage)
  add_test(NAME EchoDatabaseActorLifecycleTest COMMAND EchoDatabaseActorLifecycleTest)

  if(ECHO_NATIVE_SQLITE_AVAILABLE)
    add_executable(EchoDatabaseWalConcurrencyTest tests/database_wal_concurrency_test.cpp)
    target_include_directories(EchoDatabaseWalConcurrencyTest PRIVATE include)
    target_link_libraries(EchoDatabaseWalConcurrencyTest PRIVATE EchoStorage)
    add_test(NAME EchoDatabaseWalConcurrencyTest COMMAND EchoDatabaseWalConcurrencyTest)
  endif()

  add_executable(EchoPlayStatsTest tests/play_stats_test.cpp)
  target_include_directories(EchoPlayStatsTest PRIVATE include)
  target_link_libraries(EchoPlayStatsTest PRIVATE EchoCore EchoCAPI)
  add_test(NAME EchoPlayStatsTest COMMAND EchoPlayStatsTest)

  set(ECHO_NATIVE_TESTS
    EchoNativeSmokeTests
    EchoRouteContractTest
    EchoDeviceIdentityContractTest
    EchoSongUrlContractTest
    EchoPlaylistContractTest
    EchoYouthVipContractTest
    EchoLoginContractTest
    EchoProfileSignatureContractTest
    EchoHomeContractTest
    EchoHttpClientResilienceTest
    EchoRequestSchedulerResilienceTest
    EchoCrashCaptureContractTest
    EchoDatabaseActorLifecycleTest
    EchoPlayStatsTest
  )
  if(ECHO_NATIVE_SQLITE_AVAILABLE)
    list(APPEND ECHO_NATIVE_TESTS EchoDatabaseWalConcurrencyTest)
  endif()
  # These test binaries use the standard assert macro as their failure
  # mechanism. Keep assertions enabled even when a Release preset defines
  # NDEBUG, otherwise CTest can report false greens.
  foreach(_echo_test IN LISTS ECHO_NATIVE_TESTS)
    if(MSVC)
      target_compile_options(${_echo_test} PRIVATE /UNDEBUG)
    else()
      target_compile_options(${_echo_test} PRIVATE -UNDEBUG)
    endif()
  endforeach()
  # 测试进程也需要找到 vcpkg 的动态库；把 bin 目录临时挂到 PATH，
  # 避免普通 PowerShell/CTest 启动时报 0xc0000135。
  set_tests_properties(${ECHO_NATIVE_TESTS} PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${ECHO_NATIVE_VCPKG_INSTALLED_DIR}/bin")
endif()
