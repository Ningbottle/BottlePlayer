cmake_minimum_required(VERSION 3.24)
project(BottleMusicQt VERSION 1.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 Concurrent Multimedia Network Svg ShaderTools Sql Test)
qt_standard_project_setup(REQUIRES 6.8)
add_library(bottle_qrcode STATIC third_party/qrcodegen/qrcodegen.cpp)
target_include_directories(bottle_qrcode PUBLIC third_party/qrcodegen)
add_library(bottle_qt_core STATIC
    src/core/NativeGateway.cpp src/core/NativeGateway.h
    src/core/Diagnostics.cpp src/core/Diagnostics.h
    src/music/Track.cpp src/music/Track.h
    src/music/MusicController.cpp src/music/MusicController.h
    src/account/AccountController.cpp src/account/AccountController.h src/account/QrImage.cpp src/account/QrImage.h
    src/account/VipController.cpp src/account/VipController.h
    src/library/LibraryController.cpp src/library/LibraryController.h
    src/library/HistoryController.cpp src/library/HistoryController.h
    src/library/FavoriteController.cpp src/library/FavoriteController.h
    src/home/HomeController.cpp src/home/HomeController.h
    src/playback/PlaySession.cpp src/playback/PlaySession.h
    src/playback/EqProcessor.cpp src/playback/EqProcessor.h
    src/playback/PcmOutput.cpp src/playback/PcmOutput.h
    src/playback/EqualizerController.cpp src/playback/EqualizerController.h
    src/stats/StatsController.cpp src/stats/StatsController.h
    src/stats/AiAnalyzer.cpp src/stats/AiAnalyzer.h
    src/settings/SettingsController.cpp src/settings/SettingsController.h
    src/settings/DataMigration.cpp src/settings/DataMigration.h
    src/settings/StatsArchive.cpp src/settings/StatsArchive.h
    src/platform/MediaIntegration.cpp src/platform/MediaIntegration.h
    src/platform/UpdateController.cpp src/platform/UpdateController.h
    src/lyrics/LyricTimeline.cpp src/lyrics/LyricTimeline.h
    src/ui/UiAssets.cpp src/ui/UiAssets.h
    src/ui/WindowPresentation.cpp src/ui/WindowPresentation.h
    src/ui/PlayerLayout.cpp src/ui/PlayerLayout.h
    src/ui/AppearanceController.cpp src/ui/AppearanceController.h
    src/ui/GrayTextBackend.cpp)
target_include_directories(bottle_qt_core PUBLIC src PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../native/include")
target_link_libraries(bottle_qt_core PUBLIC Qt6::Quick Qt6::Concurrent Qt6::Multimedia Qt6::Svg Qt6::Sql PRIVATE bottle_qrcode)
# The gray-text scene graph backend subclasses the stock RHI context, a
# private Qt Quick class; it is linked in as a static plugin.
target_link_libraries(bottle_qt_core PRIVATE Qt6::QuickPrivate Qt6::GuiPrivate)
if(WIN32)
    target_link_libraries(bottle_qt_core PRIVATE windowsapp runtimeobject shell32 ole32)
endif()
set_source_files_properties(src/ui/GrayTextBackend.cpp PROPERTIES COMPILE_DEFINITIONS QT_STATICPLUGIN)
if(MSVC)
    target_compile_options(bottle_qt_core PUBLIC /utf-8 /W4)
endif()
qt_add_executable(BottleMusicQt WIN32 src/main.cpp)
target_link_libraries(BottleMusicQt PRIVATE bottle_qt_core Qt6::QuickControls2)
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/components/BravaisLayout.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml)
# Original official Lucide SVGs (see licenses/THIRD_PARTY.md); served through the
# image://icon provider from :/qt/qml/BottleMusic/assets/icons/.
file(GLOB bottle_icons RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS assets/icons/*.svg)
qt_add_library(bottle_qt_ui STATIC)
qt_add_qml_module(bottle_qt_ui URI BottleMusic VERSION 1.0
    SOURCES src/ui/PixelSnap.cpp src/ui/PixelSnap.h src/ui/LyricGeometry.cpp src/ui/LyricGeometry.h
    src/visuals/ColorExtractor.cpp src/visuals/ColorExtractor.h
    QML_FILES qml/Main.qml qml/Theme.qml
    qml/components/ActionButton.qml qml/components/Cover.qml qml/components/CssText.qml qml/components/Hairline.qml
    qml/components/Icon.qml qml/components/IconButton.qml qml/components/Plate.qml
    qml/components/SeekBar.qml qml/components/Segmented.qml qml/components/SettingItem.qml
    qml/components/ShadowEffect.qml qml/components/SmoothWheel.qml qml/components/SongCard.qml
    qml/components/ThinScrollBar.qml qml/components/Toast.qml qml/components/Toggle.qml qml/components/Transport.qml
    qml/components/WallPrint.qml qml/components/LyricView.qml
    qml/components/BravaisLayout.qml qml/components/BravaisTile.qml qml/components/BravaisBar.qml
    qml/components/PlayerArtwork.qml qml/components/PlayerLayoutEditor.qml qml/components/PlayerCoverShelf.qml
    qml/components/AccountPanel.qml qml/components/ConfirmDialog.qml
    qml/components/TrackRows.qml
    qml/components/QualityMenu.qml
    qml/components/AddToPlaylistDialog.qml
    qml/components/DeviceSettings.qml
    qml/components/HomeCollections.qml
    qml/components/ImmersiveBackground.qml qml/components/ImmersiveControls.qml
    qml/shell/TitleBar.qml qml/shell/Sidebar.qml qml/shell/TopBar.qml qml/shell/PlayerBar.qml
    qml/pages/PaperPage.qml qml/pages/CurtainPage.qml qml/pages/BravaisHome.qml qml/pages/HomeSearchPage.qml qml/pages/PosterWallPage.qml
    qml/pages/PlayerPage.qml qml/pages/SettingsPage.qml qml/pages/ImmersivePlayerPage.qml
    qml/pages/LibraryPage.qml
    qml/pages/HistoryPage.qml qml/pages/StatsPage.qml qml/pages/LocalFavoritesPage.qml
    qml/pages/EqualizerPage.qml
    qml/panels/ListeningPanel.qml qml/panels/QueuePanel.qml
    RESOURCES ${bottle_icons})
# The generated type registrations include PixelSnap.h by bare name.
target_include_directories(bottle_qt_ui PRIVATE src/ui src/visuals)
target_link_libraries(bottle_qt_ui PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent Qt6::Network)
qt_add_shaders(bottle_qt_ui immersive_shaders
    PREFIX "/bottle/shaders" BASE "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    FILES shaders/immersive-fluid.frag)
if(MSVC)
    target_compile_options(bottle_qt_ui PRIVATE /utf-8 /W4)
    set_source_files_properties(src/ui/PixelSnap.cpp PROPERTIES COMPILE_OPTIONS "/utf-8;/W4")
endif()
target_link_libraries(BottleMusicQt PRIVATE bottle_qt_uiplugin)
add_custom_command(TARGET BottleMusicQt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/icons/icon.ico"
        "$<TARGET_FILE_DIR:BottleMusicQt>/BottleMusic.ico"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:BottleMusicQt>/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses/QueMusic-LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses/QueMusic-NOTICE"
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses/THIRD_PARTY.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses/Folia-LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses/Nayuki-LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses/Lucide-LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses/Phosphor-LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses/Inter-LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses/EBGaramond-LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses/LibreCaslonDisplay-LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses/NotoSerifSC-LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses/LXGWWenKaiLite-LICENSE"
        "$<TARGET_FILE_DIR:BottleMusicQt>/licenses")
set(ECHO_NATIVE_DIR "" CACHE PATH "Existing x64 EchoCAPI.dll and sqlite3.dll directory")
if(ECHO_NATIVE_DIR)
    foreach(dll EchoCAPI.dll sqlite3.dll)
        if(NOT EXISTS "${ECHO_NATIVE_DIR}/${dll}")
            message(FATAL_ERROR "Missing native runtime: ${ECHO_NATIVE_DIR}/${dll}")
        endif()
        add_custom_command(TARGET BottleMusicQt POST_BUILD COMMAND ${CMAKE_COMMAND} -E
            copy_if_different "${ECHO_NATIVE_DIR}/${dll}" "$<TARGET_FILE_DIR:BottleMusicQt>/${dll}")
    endforeach()
endif()
# Runtime fonts are source assets, with redistribution notices in licenses/.
# UiAssets.cpp registers them from <app>/fonts.
set(BOTTLE_FONT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts" CACHE PATH "Directory holding bundled TTF fonts")
if(BOTTLE_FONT_DIR)
    file(GLOB bottle_fonts "${BOTTLE_FONT_DIR}/*.ttf")
    if(NOT bottle_fonts)
        message(FATAL_ERROR "No .ttf files in BOTTLE_FONT_DIR=${BOTTLE_FONT_DIR}")
    endif()
    add_custom_command(TARGET BottleMusicQt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:BottleMusicQt>/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${bottle_fonts} "$<TARGET_FILE_DIR:BottleMusicQt>/fonts")
endif()
include(CTest)
if(BUILD_TESTING)
    add_library(bottle_native_fixture SHARED tests/native_fixture.cpp)
    target_link_libraries(bottle_native_fixture PRIVATE Qt6::Core)
    if(MSVC)
        target_compile_options(bottle_native_fixture PRIVATE /utf-8 /W4)
    endif()
    qt_add_executable(bottle_account_tests tests/tst_account.cpp)
    target_link_libraries(bottle_account_tests PRIVATE bottle_qt_core Qt6::Test)
    add_test(NAME qt_account_contracts COMMAND bottle_account_tests)
    set_tests_properties(qt_account_contracts PROPERTIES ENVIRONMENT "BOTTLE_ACCOUNT_TEST_DLL=$<TARGET_FILE:bottle_native_fixture>")
    qt_add_executable(bottle_playback_migration_tests tests/tst_playback_migration.cpp)
    target_link_libraries(bottle_playback_migration_tests PRIVATE bottle_qt_core Qt6::Test)
    add_test(NAME qt_playback_migration COMMAND bottle_playback_migration_tests)
    set_tests_properties(qt_playback_migration PROPERTIES ENVIRONMENT "BOTTLE_ACCOUNT_TEST_DLL=$<TARGET_FILE:bottle_native_fixture>")
    qt_add_executable(bottle_audio_runtime_tests tests/tst_audio_runtime.cpp)
    target_link_libraries(bottle_audio_runtime_tests PRIVATE bottle_qt_core Qt6::Test Qt6::Network)
    add_test(NAME qt_audio_runtime COMMAND bottle_audio_runtime_tests)
    set_tests_properties(qt_audio_runtime PROPERTIES ENVIRONMENT "BOTTLE_ACCOUNT_TEST_DLL=$<TARGET_FILE:bottle_native_fixture>")
    qt_add_executable(bottle_music_flow_tests tests/tst_music_flow.cpp)
    target_link_libraries(bottle_music_flow_tests PRIVATE bottle_qt_core Qt6::Test)
    add_test(NAME qt_music_flows COMMAND bottle_music_flow_tests)
    set_tests_properties(qt_music_flows PROPERTIES ENVIRONMENT "BOTTLE_ACCOUNT_TEST_DLL=$<TARGET_FILE:bottle_native_fixture>")
    qt_add_executable(bottle_library_migration_tests tests/tst_library_migration.cpp)
    target_link_libraries(bottle_library_migration_tests PRIVATE bottle_qt_core Qt6::Test)
    add_test(NAME qt_library_migration COMMAND bottle_library_migration_tests)
    set_tests_properties(qt_library_migration PROPERTIES ENVIRONMENT "BOTTLE_ACCOUNT_TEST_DLL=$<TARGET_FILE:bottle_native_fixture>")
    qt_add_executable(bottle_settings_migration_tests tests/tst_settings_migration.cpp)
    target_link_libraries(bottle_settings_migration_tests PRIVATE bottle_qt_core Qt6::Test)
    add_test(NAME qt_settings_migration COMMAND bottle_settings_migration_tests)
    qt_add_executable(bottle_qt_tests tests/tst_contracts.cpp)
    target_link_libraries(bottle_qt_tests PRIVATE bottle_qt_core Qt6::Test)
    add_test(NAME qt_contracts COMMAND bottle_qt_tests)
    qt_add_executable(bottle_player_layout_tests tests/tst_player_layout.cpp)
    target_link_libraries(bottle_player_layout_tests PRIVATE bottle_qt_core Qt6::Test)
    add_test(NAME qt_player_layout COMMAND bottle_player_layout_tests)
    qt_add_executable(bottle_visual_tests tests/tst_visuals.cpp)
    target_include_directories(bottle_visual_tests PRIVATE src/visuals)
    target_link_libraries(bottle_visual_tests PRIVATE bottle_qt_uiplugin Qt6::Quick Qt6::Concurrent Qt6::Network Qt6::Test)
    if(MSVC)
        target_compile_options(bottle_visual_tests PRIVATE /utf-8 /W4)
    endif()
    add_test(NAME qt_visual_contracts COMMAND bottle_visual_tests)
    qt_add_executable(bottle_qt_smoke tests/smoke.cpp)
    target_link_libraries(bottle_qt_smoke PRIVATE bottle_qt_core bottle_qt_uiplugin Qt6::QuickControls2 Qt6::Test)
    qt_add_executable(bottle_resize_probe tests/resize_probe.cpp)
    target_link_libraries(bottle_resize_probe PRIVATE bottle_qt_core bottle_qt_uiplugin Qt6::QuickControls2 Qt6::Test)
endif()
