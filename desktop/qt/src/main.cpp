#include "music/MusicController.h"
#include "ui/UiAssets.h"
#include "ui/WindowPresentation.h"
#include "ui/PlayerLayout.h"
#include "account/AccountController.h"
#include "account/VipController.h"
#include "library/LibraryController.h"
#include "library/HistoryController.h"
#include "library/FavoriteController.h"
#include <QPointer>
#include "stats/StatsController.h"
#include "stats/AiAnalyzer.h"
#include "ui/AppearanceController.h"
#include "settings/SettingsController.h"
#include "settings/DataMigration.h"
#include "playback/EqualizerController.h"
#include "platform/MediaIntegration.h"
#include "platform/UpdateController.h"
#include "home/HomeController.h"
#include <QQuickWindow>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("BottleMusic");
    app.setApplicationName("BottleMusic Qt Preview");
    app.setApplicationVersion("1.1.0-qt-preview");
    app.setWindowIcon(QIcon(QDir(app.applicationDirPath()).filePath("BottleMusic.ico")));
    QQuickStyle::setStyle("Basic");
    configureWindowPresentation();
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"profile", "Independent data directory", "path", QDir(app.applicationDirPath()).filePath("profile")});
    parser.addOption({"native-dll", "EchoCAPI x64 runtime", "path", QDir(app.applicationDirPath()).filePath("EchoCAPI.dll")});
    parser.process(app);
    MusicController music(parser.value("native-dll"), QDir(parser.value("profile")).absolutePath());
    AccountController account(music.nativeGateway());
    VipController vip(music.nativeGateway());
    LibraryController library(music.nativeGateway());
    HistoryController history(music.nativeGateway(), QDir(parser.value("profile")).absolutePath());
    StatsController stats(music.nativeGateway());
    HomeController home(music.nativeGateway());
    QObject::connect(&home, &HomeController::tracksReady, &music, &MusicController::setFeed);
    QObject::connect(&account, &AccountController::sessionChanged, &home, &HomeController::setAccount);
    QObject::connect(&account, &AccountController::sessionChanged, &music, &MusicController::setAccount);
    music.setFeedRefresh([&home] { home.refresh(); });
    AiAnalyzer analyzer;
    FavoriteController favorite(music.nativeGateway(), QDir(parser.value("profile")).absolutePath());
    QObject::connect(&account, &AccountController::sessionChanged, &library, &LibraryController::setAccount);
    QObject::connect(&account, &AccountController::sessionChanged, &vip, &VipController::setAccount);
    QObject::connect(&account, &AccountController::sessionChanged, &history, &HistoryController::setAccount);
    QObject::connect(&account, &AccountController::sessionChanged, &favorite, &FavoriteController::setAccount);
    QObject::connect(&library, &LibraryController::changed, &favorite, [&] { favorite.setPlaylists(library.playlists()); });
    QObject::connect(&favorite, &FavoriteController::changed, &music, &MusicController::favoriteChanged);
    QObject::connect(&favorite, &FavoriteController::notice, &music, &MusicController::notice);
    QObject::connect(&favorite, &FavoriteController::playlistChanged, &library, [&](const QString& id) { if (id == library.playlistId()) library.retryTracks(); });
    const QPointer<FavoriteController> favoriteOwner(&favorite);
    music.setFavoritePolicy([favoriteOwner](const QVariantMap& track) { return favoriteOwner && favoriteOwner->isFavorite(track); },
        [favoriteOwner](const QVariantMap& track) { if (favoriteOwner) favoriteOwner->toggle(track); });
    QObject::connect(&music, &MusicController::trackStarted, &history, &HistoryController::started);
    QObject::connect(&music, &MusicController::listenRecorded, &stats, &StatsController::record);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &music, &MusicController::finishSession);
    PlayerLayout playerLayout(QDir(parser.value("profile")).absolutePath());
    AppearanceController appearance(QDir(parser.value("profile")).absolutePath());
    SettingsController settings(music.nativeGateway(), QDir(parser.value("profile")).absolutePath());
    DataMigration migration(QDir(parser.value("profile")).absolutePath());
    migration.setNativeGateway(&music.nativeGateway());
    QObject::connect(&migration, &DataMigration::statisticsImported, &stats, &StatsController::refresh);
    UpdateController updates(QDir(parser.value("profile")).absolutePath());
    EqualizerController equalizer(*music.mediaPlayer(), *music.audioOutput(), QDir(parser.value("profile")).absolutePath());
    QObject::connect(&migration, &DataMigration::importing, &music, &MusicController::holdPersistenceForImport);
    QObject::connect(&appearance, &AppearanceController::changed, &equalizer, [&] { equalizer.setAllowed(appearance.labMode()); });
    equalizer.setAllowed(appearance.labMode());
    QQmlApplicationEngine engine;
    installUiAssets(engine);
    engine.rootContext()->setContextProperty("music", &music);
    engine.rootContext()->setContextProperty("playerLayoutState", &playerLayout);
    engine.rootContext()->setContextProperty("accountState", &account);
    engine.rootContext()->setContextProperty("vipState", &vip);
    engine.rootContext()->setContextProperty("libraryState", &library);
    engine.rootContext()->setContextProperty("historyState", &history);
    engine.rootContext()->setContextProperty("favoriteState", &favorite);
    engine.rootContext()->setContextProperty("statsState", &stats);
    engine.rootContext()->setContextProperty("homeState", &home);
    engine.rootContext()->setContextProperty("analysisState", &analyzer);
    engine.rootContext()->setContextProperty("appearanceState", &appearance);
    engine.rootContext()->setContextProperty("settingsState", &settings);
    engine.rootContext()->setContextProperty("migrationState", &migration);
    engine.rootContext()->setContextProperty("updateState", &updates);
    engine.rootContext()->setContextProperty("equalizerState", &equalizer);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(2); }, Qt::QueuedConnection);
    engine.loadFromModule("BottleMusic", "Main");
    MediaIntegration media(music);
    if (!engine.rootObjects().isEmpty()) media.attach(qobject_cast<QQuickWindow*>(engine.rootObjects().first()));
    music.initialize();
    return app.exec();
}
