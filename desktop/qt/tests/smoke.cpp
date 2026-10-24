#include "music/MusicController.h"
#include "ui/UiAssets.h"
#include "ui/WindowPresentation.h"
#include "ui/PlayerLayout.h"
#include "account/AccountController.h"
#include "account/VipController.h"
#include "library/LibraryController.h"
#include "library/HistoryController.h"
#include "library/FavoriteController.h"
#include "stats/StatsController.h"
#include "stats/AiAnalyzer.h"
#include "ui/AppearanceController.h"
#include "settings/SettingsController.h"
#include "settings/DataMigration.h"
#include "playback/EqualizerController.h"
#include "platform/UpdateController.h"
#include "platform/MediaIntegration.h"
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "home/HomeController.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QAudioBufferOutput>
#include <QAudioBuffer>
#include <QAudioDevice>
#include <QJsonDocument>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QElapsedTimer>
#include <QTimer>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QtEndian>
#include <QtTest>
#include <QTextLayout>
#include <QLibrary>
#include <QTcpServer>
#include <QTcpSocket>
#include <QRegularExpression>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlListReference>
#include <QMutex>
#include <QMutexLocker>
#include <QtMath>
#include <QSet>
#include <functional>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
QStringList warnings;
QMutex warningMutex;
void logMessage(QtMsgType type, const QMessageLogContext&, const QString& text) {
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        // Keep signed URLs / native credentials out of durable evidence.
        if (text.contains(".qml") || text.contains("QQml") || text.contains("shader", Qt::CaseInsensitive)
            || text.contains(".qsb") || text.contains("QSG") || text.startsWith("BottleMusic: missing icon")) { QMutexLocker lock(&warningMutex); warnings.append(text.section("https:", 0, 0)); }
    }
}
bool waitFor(const std::function<bool()>& predicate, int timeout = 20000) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < timeout) QTest::qWait(50);
    return predicate();
}
// Repeater delegates are visual children only (no QObject parent), so
// findChild misses them; walk the item tree as well.
QObject* findItem(QQuickItem* item, const QString& name) {
    if (!item) return nullptr;
    if (item->objectName() == name) return item;
    if (auto* match = item->findChild<QObject*>(name)) return match;
    for (auto* child : item->childItems())
        if (auto* match = findItem(child, name)) return match;
    return nullptr;
}
void click(QQuickWindow* window, QObject* item) {
    auto* quick = qobject_cast<QQuickItem*>(item);
    if (!quick) return;
    const auto point = quick->mapToScene(QPointF(quick->width()/2, quick->height()/2)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point); QTest::qWait(150);
}
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv); QQuickStyle::setStyle("Basic"); qInstallMessageHandler(logMessage);
    configureWindowPresentation();
    const QStringList args = app.arguments();
    if (args.size() != 4) return 2;
    QDir().mkpath(args[2]);
    std::unique_ptr<QTemporaryDir> interactionProfile;
    if (args[3] == "navigation-interactions" || args[3] == "text-raster" || args[3] == "quality-interactions" || args[3] == "theme-interactions" || args[3] == "shell-interactions") {
        interactionProfile = std::make_unique<QTemporaryDir>(QDir(args[2]).filePath("profile-XXXXXX"));
        if (!interactionProfile->isValid()) return 2;
    }
    const QString profile = interactionProfile ? interactionProfile->path()
        : qEnvironmentVariable("BOTTLE_SMOKE_PROFILE", QDir(args[2]).filePath("profile"));
    if (args[3] == "shell-interactions") {
        QSettings legacy(QDir(profile).filePath("appearance.ini"),QSettings::IniFormat);
        legacy.setValue("presentation","curtain"); legacy.sync();
    }
    MusicController music(args[1], profile);
    AccountController account(music.nativeGateway()); LibraryController library(music.nativeGateway());
    VipController vip(music.nativeGateway());
    HistoryController history(music.nativeGateway(), profile); StatsController stats(music.nativeGateway());
    HomeController home(music.nativeGateway());
    QObject::connect(&home, &HomeController::tracksReady, &music, &MusicController::setFeed);
    QObject::connect(&account, &AccountController::sessionChanged, &home, &HomeController::setAccount);
    music.setFeedRefresh([&home] { home.refresh(); });
    AiAnalyzer analyzer;
    FavoriteController favorite(music.nativeGateway(), profile);
    QObject::connect(&account, &AccountController::sessionChanged, &library, &LibraryController::setAccount);
    QObject::connect(&account, &AccountController::sessionChanged, &vip, &VipController::setAccount);
    QObject::connect(&account, &AccountController::sessionChanged, &history, &HistoryController::setAccount);
    QObject::connect(&account, &AccountController::sessionChanged, &favorite, &FavoriteController::setAccount);
    QObject::connect(&library, &LibraryController::changed, &favorite, [&] { favorite.setPlaylists(library.playlists()); });
    QObject::connect(&favorite, &FavoriteController::changed, &music, &MusicController::favoriteChanged);
    QObject::connect(&favorite, &FavoriteController::notice, &music, &MusicController::notice);
    QObject::connect(&favorite, &FavoriteController::playlistChanged, &library, [&](const QString& id) { if (id == library.playlistId()) library.retryTracks(); });
    music.setFavoritePolicy([&favorite](const QVariantMap& track) { return favorite.isFavorite(track); }, [&favorite](const QVariantMap& track) { favorite.toggle(track); });
    QObject::connect(&music, &MusicController::trackStarted, &history, &HistoryController::started);
    QObject::connect(&music, &MusicController::listenRecorded, &stats, &StatsController::record);
    PlayerLayout layoutModel(profile);
    AppearanceController appearance(profile);
    SettingsController settings(music.nativeGateway(), profile);
    DataMigration migration(profile);
    migration.setNativeGateway(&music.nativeGateway());
    migration.setPrepareSnapshot([&music, &appearance] { return music.flushStorage() && appearance.flushStorage(); });
    UpdateController updates(profile);
    EqualizerController equalizer(*music.mediaPlayer(), *music.audioOutput(), profile);
    QObject::connect(&migration, &DataMigration::importing, &music, &MusicController::holdPersistenceForImport);
    QObject::connect(&appearance, &AppearanceController::changed, &equalizer, [&] { equalizer.setAllowed(appearance.labMode()); });
    equalizer.setAllowed(appearance.labMode());
    QQmlApplicationEngine engine; installUiAssets(engine); engine.rootContext()->setContextProperty("music", &music);
    engine.rootContext()->setContextProperty("playerLayoutState", &layoutModel);
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
    // Record Qt's native small-glyph coverage before the window creates our
    // scene graph backend. Rendering the UI must not silently weaken it.
    struct NativeMask { QRawFont font; quint32 glyph; QImage image; };
    QList<NativeMask> nativeSmallMasks;
    if (args[3] == "text-raster") {
        for (int size : {12,14}) {
            QFont font=QGuiApplication::font(); font.setPixelSize(size);
            QTextLayout shaped(QStringLiteral("山日曲音 e08"),font);
            shaped.beginLayout(); auto line=shaped.createLine(); line.setLineWidth(1000); shaped.endLayout();
            for (const auto& run:shaped.glyphRuns()) {
                QRawFont raw=run.rawFont(); raw.setPixelSize(size*app.devicePixelRatio());
                for (quint32 glyph:run.glyphIndexes())
                    nativeSmallMasks.append({raw,glyph,raw.alphaMapForGlyph(glyph,QRawFont::PixelAntialiasing)});
            }
        }
    }
    engine.loadFromModule("BottleMusic", "Main");
    if (engine.rootObjects().isEmpty()) {
        for (const QString& w : warnings) fprintf(stderr, "%s\n", qPrintable(w));
        return 3;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    QJsonObject report; QJsonArray checks; QJsonObject capturedImages; QJsonArray skippedCaptures;
    const auto check = [&checks](QString name, bool ok) { checks.append(QJsonObject{{"check", name}, {"ok", ok}, {"qml_warning_count",warnings.size()}}); return ok; };
    const auto find = [window](const char* name) {
        auto* match = window->findChild<QObject*>(QLatin1String(name));
        return match ? match : findItem(window->contentItem(), QLatin1String(name));
    };
    const auto screenshot = [window, &args, &capturedImages, &skippedCaptures](const QString& name) {
        const auto available=window->visibility()==QWindow::FullScreen?window->screen()->geometry().size():window->screen()->availableGeometry().size();
        if(window->width()>available.width()||window->height()>available.height()) {skippedCaptures.append(name);return false;}
        QTest::qWait(600);
        const auto capture = window->grabWindow();
        if (capture.save(QDir(args[2]).filePath(name + ".png"))) return true;
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
        if (!capture.save(&buffer, "PNG")) return false;
        capturedImages.insert(name, QString::fromLatin1(bytes.toBase64()));
        return true;
    };
    const auto writeReport = [&] {
        report.insert("captured_images", capturedImages);
        report.insert("skipped_captures",skippedCaptures);
        const auto json = QJsonDocument(report).toJson();
        fwrite(json.constData(), 1, size_t(json.size()), stdout);
        QFile file(QDir(args[2]).filePath("smoke.json"));
        if (file.open(QIODevice::WriteOnly)) file.write(json);
    };
    #include "visual_assertions.h"
    if (args[3] == "shell-interactions") {
        #include "shell_interactions_smoke.h"
    }
    if (args[3] == "user-fixes") {
        #include "user_report_smoke.h"
    }
    if (args[3] == "render-review") {
        #include "render_review_smoke.h"
    }
    if (args[3] == "navigation-interactions") {
        #include "navigation_interactions_smoke.h"
    }
    if (args[3] == "quality-interactions") {
        #include "quality_interactions_smoke.h"
    }
    if (args[3] == "theme-interactions") {
        #include "theme_interactions_smoke.h"
    }
    if (args[3] == "settings-probe") {
        // Font-weight probe: the settings page is the heaviest bold CJK sans
        // surface; captures it so gamma values can be A/B'd on the same text.
        QLibrary sFixture(args[1]);
        if (!sFixture.load()) return 2;
        auto sConfigure = reinterpret_cast<void(*)(const char*)>(sFixture.resolve("EchoTestConfigure"));
        if (!sConfigure) return 2;
        sConfigure("{\"feed_count\":30}");
        music.setVolume(0); music.setReducedMotion(true); music.initialize();
        check("probe_core_ready", waitFor([&]{return music.ready();},10000));
        window->resize(900, 620);
        QMetaObject::invokeMethod(window,"openSettings",Q_ARG(QVariant,QStringLiteral("appearance"))); QTest::qWait(900);
        screenshot("settings-top");
        report.insert("checks", checks); report.insert("qml_warnings", QJsonArray::fromStringList(warnings));
        bool sOk = warnings.isEmpty(); for (const auto& item : checks) sOk = sOk && item.toObject().value("ok").toBool();
        report.insert("ok", sOk); writeReport(); window->close(); return sOk ? 0 : 1;
    }
    if (args[3] == "blur-probe") {
        // Rendering diagnosis: capture identical content at a device-pixel-snapped
        // scroll offset and at a half-device-pixel offset. Pairwise crops then
        // measure how much the scene softens when the flickable rests off-grid.
        QLibrary probeFixture(args[1]);
        if (!probeFixture.load()) return 2;
        auto probeConfigure = reinterpret_cast<void(*)(const char*)>(probeFixture.resolve("EchoTestConfigure"));
        if (!probeConfigure) return 2;
        probeConfigure("{\"feed_count\":30}");
        music.setVolume(0); music.setReducedMotion(true); music.initialize();
        check("probe_core_ready", waitFor([&]{return music.ready() && music.feed().size()>=20 && !home.loading();},10000));
        QMetaObject::invokeMethod(window,"navigate",Q_ARG(QVariant,QStringLiteral("home"))); QTest::qWait(600);
        auto* scroller = qobject_cast<QQuickItem*>(find("homeScroller"));
        check("probe_scroller", scroller != nullptr);
        const double dpr = window->devicePixelRatio();
        report.insert("dpr", dpr);
        if (scroller) { scroller->setProperty("contentY", 0.0); QTest::qWait(300); }
        screenshot("probe-top");
        const double snapY = std::round(200.0 * dpr) / dpr;
        const double halfY = snapY + 0.5 / dpr;
        if (scroller) { scroller->setProperty("contentY", snapY); QTest::qWait(300); }
        screenshot("probe-scrolled-snap");
        if (scroller) { scroller->setProperty("contentY", halfY); QTest::qWait(300); }
        screenshot("probe-scrolled-half");
        report.insert("snap_contentY", snapY); report.insert("half_contentY", halfY);
        report.insert("checks", checks); report.insert("qml_warnings", QJsonArray::fromStringList(warnings));
        bool probeOk = warnings.isEmpty(); for (const auto& item : checks) probeOk = probeOk && item.toObject().value("ok").toBool();
        report.insert("ok", probeOk); writeReport(); window->close(); return probeOk ? 0 : 1;
    }
    if (args[3] == "font-layout") {
        QLibrary fixture(args[1]); fixture.setLoadHints(QLibrary::PreventUnloadHint);
        if (!fixture.load()) return 2;
        auto configure = reinterpret_cast<void(*)(const char*)>(fixture.resolve("EchoTestConfigure"));
        if (!configure) return 2;
        configure("{\"feed_count\":30}");
        music.setVolume(0); music.setReducedMotion(true); music.initialize();
        check("font_layout_ready", waitFor([&] { return music.ready() && !home.loading(); }, 10000));
        window->resize(1280, 850);
        QMetaObject::invokeMethod(window,"navigate",Q_ARG(QVariant,QStringLiteral("home")));
        QTest::qWait(500);
        auto* scroller=quick("homeScroller"); auto* carousel=quick("homeCarousel");
        check("font_layout_carousel_present", scroller && carousel && carousel->isVisible());
        const qreal cardSize=carousel?carousel->property("cardSize").toDouble():0;
        check("font_layout_carousel_space",carousel && cardSize>=280 && carousel->height()-cardSize<=185);
        screenshot("font-layout-home");
        if (scroller && carousel) {
            const qreal top=carousel->mapToItem(scroller,QPointF()).y()+scroller->property("contentY").toDouble();
            scroller->setProperty("contentY",std::max(0.0,double(top-24)));
            QTest::qWait(300);
            screenshot("font-layout-carousel");
            report.insert("carousel_height",carousel->height());
            report.insert("carousel_card_size",cardSize);
            report.insert("carousel_top",top);
        }
        window->resize(1600, 900); QTest::qWait(300);
        const qreal wideCardSize=carousel?carousel->property("cardSize").toDouble():0;
        check("font_layout_wide_card",carousel && wideCardSize>=339 && wideCardSize<=340.5
            && carousel->height()-wideCardSize<=185);
        if (scroller && carousel) {
            const qreal top=carousel->mapToItem(scroller,QPointF()).y()+scroller->property("contentY").toDouble();
            scroller->setProperty("contentY",std::max(0.0,double(top-24)));
            QTest::qWait(300); screenshot("font-layout-wide-carousel");
            report.insert("wide_carousel_height",carousel->height());
            report.insert("wide_carousel_card_size",wideCardSize);
        }
        check("font_layout_qml_warnings_absent",warnings.isEmpty());
        report.insert("dpr",window->devicePixelRatio()); report.insert("checks",checks);
        bool passed=true; for (const auto& item:checks) passed=passed&&item.toObject().value("ok").toBool();
        report.insert("ok",passed); writeReport(); music.finishSession(); return passed?0:1;
    }
    if (args[3] == "text-raster") {
        #include "text_raster_smoke.h"
    }
    if (args[3] == "qr-live") {
        music.initialize();
        check("native_core_ready", waitFor([&] { return music.ready(); }, 10000));
        if (account.loggedIn()) return 2; // Caller must use a fresh, isolated profile.
        window->setProperty("settingsSection", "account"); window->setProperty("route", "settings");
        check("real_native_qr_created", waitFor([&] { return !account.qrImage().isEmpty() || account.state() == "error"; }, 40000)
            && account.qrImage().startsWith("data:image/png;base64,"));
        report.insert("state", account.state()); // Never record the QR payload/key.
        account.deactivate(); QTest::qWait(300);
        check("live_attempt_cancelled", account.qrImage().isEmpty() && account.state() == "idle");
        report.insert("checks", checks); report.insert("qml_warnings", QJsonArray::fromStringList(warnings));
        bool passed = warnings.isEmpty(); for (const auto& item : checks) passed = passed && item.toObject().value("ok").toBool();
        report.insert("ok", passed); writeReport(); window->close(); return passed ? 0 : 1;
    }
    if (args[3] == "account") {
        MediaIntegration media(music); media.attach(window);
        QLibrary fixture(args[1]);
        using Configure = void(*)(const char*); using Snapshot = char*(*)(); using Free = void(*)(char*);
        if (!fixture.load()) return 2;
        const auto configure = reinterpret_cast<Configure>(fixture.resolve("EchoTestConfigure"));
        const auto snapshot = reinterpret_cast<Snapshot>(fixture.resolve("EchoTestSnapshot"));
        const auto free = reinterpret_cast<Free>(fixture.resolve("EchoFreeString"));
        // A live runtime must never run this fixture's login/logout actions.
        if (!configure || !snapshot || !free) return 2;
        QFile audio(qEnvironmentVariable("BOTTLE_SMOKE_AUDIO")); if (!audio.open(QIODevice::ReadOnly)) return 2;
        const auto wav = audio.readAll(); QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost)) return 2;
        QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
            auto* socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, wav] {
                if (socket->property("sent").toBool()) return;
                const auto request = socket->readAll(); if (!request.contains("\r\n\r\n")) return;
                socket->setProperty("sent", true);
                if (request.startsWith("GET /missing.wav")) { socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); socket->disconnectFromHost(); return; }
                const auto range = QRegularExpression("Range: bytes=(\\d+)-(\\d*)", QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(request));
                const qint64 start = range.hasMatch() ? range.captured(1).toLongLong() : 0;
                const qint64 end = range.hasMatch() && !range.captured(2).isEmpty() ? qMin(range.captured(2).toLongLong(), qint64(wav.size() - 1)) : wav.size() - 1;
                const auto body = wav.mid(start, qMax<qint64>(0, end - start + 1));
                QByteArray header = range.hasMatch() ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
                if (range.hasMatch()) header += "Content-Range: bytes " + QByteArray::number(start) + "-" + QByteArray::number(end) + "/" + QByteArray::number(wav.size()) + "\r\n";
                header += "Content-Type: audio/wav\r\nAccept-Ranges: bytes\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
                socket->write(header + body); socket->disconnectFromHost();
            });
        });
        const auto audioUrl = QStringLiteral("http://127.0.0.1:%1/fixture.wav").arg(server.serverPort());
        const auto options = QJsonDocument(QJsonObject{{"statuses", QJsonArray{1, 2, 4}}, {"audio_url", audioUrl}, {"liked", true}}).toJson(QJsonDocument::Compact);
        configure(options.constData()); music.setReducedMotion(true); music.initialize();
        check("fixture_core_ready", waitFor([&] { return music.ready(); }, 5000));
        if (music.fingerprint() != QLatin1String("qt-account-fixture")) return 2;
        // This suite exercises the full persistent navigation in paper mode;
        // The main shell owns the account and personal-library routes.
        QMetaObject::invokeMethod(window,"navigate",Q_ARG(QVariant,QStringLiteral("home"))); QTest::qWait(350);
        window->setProperty("settingsSection", "account"); window->setProperty("route", "settings");
        check("qr_login_page", waitFor([&] { return find("loginQrImage") && !account.qrImage().isEmpty(); }, 5000));
        screenshot("qt-account-qr");
        check("qr_scanned_state", waitFor([&] { return account.state() == QLatin1String("scanned"); }, 6000));
        check("confirmed_login_updates_profile", waitFor([&] { return account.loggedIn() && account.username() == QStringLiteral("纸页听众"); }, 6000));
        check("personal_playlists_loaded", waitFor([&] { return library.playlists().size() == 2; }, 5000));
        click(window,find("settingsClose")); QTest::qWait(200); screenshot("sidebar-1280x850-loggedin");
        window->resize(900,620);QTest::qWait(300);check("sidebar_personal_list_not_duplicated_900",!find("sidebarPlaylists")&&quick("libraryEntry")&&quick("libraryEntry")->isVisible());screenshot("sidebar-900x620-loggedin");
        window->resize(1280,850);click(window,find("sidebarUser"));QTest::qWait(200);
        check("daily_recommendation_and_public_collections", waitFor([&] { return !home.loading() && home.playlists().size() == 2 && music.feed().size() == 2; }, 5000));
        check("membership_initial_query", waitFor([&] { return !vip.loading() && vip.tier() == "none"; }, 5000));
        vip.claim(); check("membership_claim_confirmed", waitFor([&] { return !vip.busy() && vip.tier() == "premium"; }, 5000));
        screenshot("qt-account-signed-in");
        click(window, find("settingsClose"));
        const auto routeBeforeLibrary = window->property("route").toString();
        click(window, find("libraryEntry"));
        check("library_entry_expands_in_place", window->property("route").toString() == routeBeforeLibrary);
        window->setProperty("route", QStringLiteral("library")); QTest::qWait(200);
        check("personal_library_route", window->property("route").toString() == "library" && find("libraryPage"));
        screenshot("qt-personal-library");
        click(window, find("userPlaylist_0"));
        check("playlist_pagination_complete", waitFor([&] { return library.complete() && library.tracks().size() == 51; }, 5000));
        check("playlist_route", window->property("route").toString() == "playlist"); screenshot("qt-personal-playlist");
        window->resize(900, 620); QTest::qWait(250); screenshot("qt-personal-playlist-minimum");
        window->resize(1280, 850); QTest::qWait(250); click(window, find("playlistPlayAll"));
        check("play_all_uses_loaded_playlist", window->property("route").toString() == "player" && music.queue().size() == 51);
        check("playlist_audio_decodes", waitFor([&] { return music.duration() > 50000 && music.playing(); }, 5000));
        check("windows_smtc_attaches_to_qt_window", media.available());
#ifdef Q_OS_WIN
        MSG mediaCommand{}; mediaCommand.hwnd = reinterpret_cast<HWND>(window->winId()); mediaCommand.message = WM_APPCOMMAND;
        mediaCommand.lParam = LPARAM(APPCOMMAND_MEDIA_PAUSE) << 16; qintptr nativeResult = 0;
        check("native_media_pause_command", media.nativeEventFilter("windows_generic_MSG", &mediaCommand, &nativeResult) && !music.playing());
        media.nativeEventFilter("windows_generic_MSG", &mediaCommand, &nativeResult);
        check("native_pause_is_idempotent", !music.playing());
        music.setPlaying(true);
#endif
        report.insert("playlist_media_state", QJsonObject{{"status", int(music.mediaPlayer()->mediaStatus())},
            {"state", int(music.mediaPlayer()->playbackState())}, {"error", int(music.mediaPlayer()->error())},
            {"loading", music.loading()}, {"duration", music.mediaPlayer()->duration()}, {"position", music.position()}});
        music.toggle();
        char* rawSnapshot = snapshot(); const auto native = QJsonDocument::fromJson(rawSnapshot).object(); free(rawSnapshot);
        bool songIdentity = false;
        for (const auto& raw : native.value("requests").toArray()) {
            const auto row = raw.toObject(), query = row.value("query").toObject();
            if (row.value("path") == "/song/url") songIdentity = query.value("album_id") == "album-42" && query.value("album_audio_id") == "9001";
        }
        check("playback_keeps_native_song_ids", songIdentity);
        music.seek(12000); music.setQuality("320");
        check("paused_quality_switch_preserves_clock", waitFor([&] { return !music.loading() && music.actualQuality() == "320"; }, 5000)
            && !music.playing() && std::abs(music.position() - 12000) < 150);
        // The menu used to render whatever single quality the last /song/url
        // answer carried, which is why only 标准 stayed selectable after login.
        click(window, find("qualityButton"));
        check("quality_menu_offers_every_available_quality", waitFor([&] {
            return find("quality_128") && find("quality_320") && find("quality_flac") && find("quality_high"); }, 3000));
        // Re-clicking the selected entry closes the menu without re-resolving.
        click(window, find("quality_320")); QTest::qWait(200);
        check("quality_menu_selection_keeps_the_clock", music.quality() == QLatin1String("320")
            && std::abs(music.position() - 12000) < 150);
        check("cloud_favorite_loaded", waitFor([&] { return !favorite.loading() && music.favorite(); }, 5000));
        click(window, find("favoriteButton")); check("cloud_favorite_remove_confirmed", waitFor([&] { return favorite.pendingCount() == 0 && !music.favorite(); }, 5000));
        click(window, find("favoriteButton")); check("cloud_favorite_add_confirmed", waitFor([&] { return !favorite.loading() && favorite.pendingCount() == 0 && music.favorite(); }, 5000));
        click(window, find("queueButton")); check("queue_panel_has_all_tracks", window->property("listeningOpen").toBool() && find("queuePanel")); screenshot("qt-queue");
        const auto playingHash = music.current().value("hash").toString(); music.moveQueue(0, 1);
        check("queue_reorder_keeps_current_audio", music.queueIndex() == 1 && music.current().value("hash").toString() == playingHash && std::abs(music.position() - 12000) < 150);
        music.removeQueue(0); check("queue_remove_other_keeps_current_audio", music.queueIndex() == 0 && music.current().value("hash").toString() == playingHash);
        click(window, find("queueClose"));
        click(window, find("statsEntry"));
        check("statistics_route_and_data", waitFor([&] { return find("statsPage") && !stats.loading() && stats.songs().size() == 1; }, 5000));
        screenshot("qt-statistics");
        window->resize(900, 620); QTest::qWait(250); screenshot("qt-statistics-minimum"); window->resize(1280, 850);
        click(window, find("historyEntry"));
        check("recent_playback_and_remote_history", waitFor([&] { return find("historyPage") && !history.loading() && !history.tracks().isEmpty(); }, 5000));
        screenshot("qt-history");
        window->setProperty("settingsSection", "appearance"); window->setProperty("route", "settings"); QTest::qWait(200);
        const auto lightBackground=window->color();
        appearance.setMode("dark");
        check("dark_mode_updates_window", waitFor([&] { return themeObject->property("dark").toBool()
            && window->color()!=lightBackground && window->color()==themeObject->property("bg").value<QColor>(); }, 2000)); screenshot("qt-appearance-dark");
        appearance.setLyricFont("kai"); window->setProperty("route", "player"); QTest::qWait(250);
        check("lyric_font_preference_applies", find("activeLyricText") && find("activeLyricText")->property("font").value<QFont>().family() == "LXGW WenKai Lite");
        appearance.setLyricFont("skin"); appearance.setMode("light");
        window->setProperty("settingsSection", "playback"); window->setProperty("route", "settings");
        check("device_settings_loaded", waitFor([&] { return settings.device().value("registered").toBool(); }, 5000));
        settings.saveDevice("fixture-dfid", "fixture-mid", "fixture-uuid");
        check("device_settings_saved", waitFor([&] { return !settings.busy() && settings.device().value("dfid").toString() == "fixture-dfid"; }, 5000));
        settings.loadDiagnostics(); check("diagnostics_connected", waitFor([&] { return settings.memory().value("working_set_bytes").toLongLong() == 104857600; }, 5000));
        window->setProperty("route", "equalizer"); appearance.setLabMode(true); equalizer.applyPreset("Vocal"); equalizer.setEnabled(true); music.setPlaying(true);
        check("equalizer_processes_real_pcm", waitFor([&] { return equalizer.active(); }, 5000)); screenshot("qt-equalizer");
        check("short_button_labels_are_not_elided", find("eqPreset_Flat_label") && !find("eqPreset_Flat_label")->property("truncated").toBool());
        equalizer.setEnabled(false); music.setPlaying(false); appearance.setLabMode(false);
        check("equalizer_bypass_restores_raw_output", !equalizer.active() && !music.audioOutput()->isMuted());
        QMetaObject::invokeMethod(window,"openPlaylist",Q_ARG(QVariant,library.playlists().first().toMap().value("id")));
        check("sidebar_opens_native_personal_playlist",waitFor([&]{return window->property("route").toString()=="playlist"
            && !library.publicPlaylist() && library.complete() && library.tracks().size()>2;},5000));
        QMetaObject::invokeMethod(window,"navigate",Q_ARG(QVariant,QStringLiteral("home"))); QTest::qWait(350);
        click(window, find("sidebarUser")); QTest::qWait(100); click(window, find("accountLogout"));
        check("logout_clears_personal_library", waitFor([&] { return !account.loggedIn() && library.playlists().isEmpty() && library.tracks().isEmpty(); }, 5000));
        const auto publicPick = home.playlists().isEmpty() ? QVariantMap{{"id","18"},{"name",QStringLiteral("窗边发现")}} : home.playlists().last().toMap();
        QMetaObject::invokeMethod(window,"openPublicPlaylist",Q_ARG(QVariant,publicPick));
        check("guest_public_playlist_loads", waitFor([&] { return library.publicPlaylist() && library.complete() && library.tracks().size() == 2; },5000));
        window->setProperty("route","home"); QTest::qWait(250); screenshot("qt-home");
        configure(QJsonDocument(QJsonObject{{"audio_url",QString(audioUrl).replace("/fixture.wav","/missing.wav")},{"backup_url",QJsonArray{audioUrl}}}).toJson(QJsonDocument::Compact).constData());
        const auto fallbackTrack = music.current(); music.playTracks(QVariantList{fallbackTrack},0);
        check("failed_primary_recovers_to_backup",waitFor([&] { return music.playing() && music.mediaPlayer()->source().path() == "/fixture.wav" && !music.loading(); },8000)); music.setPlaying(false);
        favorite.toggle(music.current()); window->setProperty("route","favorites");
        check("guest_favorites_have_playable_page",waitFor([&] { return find("localFavoritesPage") && !favorite.localTracks().isEmpty(); },2000)); screenshot("qt-local-favorites");
        window->setProperty("settingsSection","storage"); window->setProperty("route","settings"); QTest::qWait(250); screenshot("qt-storage-migration");
        report.insert("checks", checks); report.insert("qml_warnings", QJsonArray::fromStringList(warnings));
        bool passed = warnings.isEmpty(); for (const auto& item : checks) passed = passed && item.toObject().value("ok").toBool();
        report.insert("ok", passed); writeReport(); window->close(); return passed ? 0 : 1;
    }
    if (args[3] == "eq") {
        music.openLocal(QUrl::fromLocalFile(qEnvironmentVariable("BOTTLE_SMOKE_AUDIO")));
        check("audio_ready",waitFor([&] { return music.playing() && music.duration() > 50000; },5000)); music.setPlaying(false);
        const auto observed = std::make_shared<QJsonObject>();
        const auto observeBuffers = [&, observed] {
            auto* output = music.mediaPlayer()->audioBufferOutput(); if (!output) return;
            QObject::connect(output,&QAudioBufferOutput::audioBufferReceived,&app,[observed](const QAudioBuffer& buffer) {
                if (!buffer.isValid()) return; const auto actual = buffer.format();
                *observed = {{"rate",actual.sampleRate()},{"channels",actual.channelCount()},{"format",int(actual.sampleFormat())},{"layout",int(actual.channelConfig())}};
            });
        };
        QObject::connect(music.mediaPlayer(), &QMediaPlayer::audioBufferOutputChanged, &app, observeBuffers);
        observeBuffers();
        appearance.setLabMode(true); equalizer.setEnabled(true); music.setPlaying(true);
        check("eq_active",waitFor([&] { return equalizer.active(); },5000));
        check("decoded_pcm_format", waitFor([&] { return !observed->isEmpty(); }, 5000));
        QAudioFormat wanted; wanted.setSampleRate(48000); wanted.setChannelConfig(QAudioFormat::ChannelConfigStereo); wanted.setSampleFormat(QAudioFormat::Float);
        report.insert("buffer_format",*observed); report.insert("device_supports_format",music.audioOutput()->device().isFormatSupported(wanted)); report.insert("device_null",music.audioOutput()->device().isNull()); report.insert("eq_reason",equalizer.reason());
        const auto device = music.audioOutput()->device(); const auto preferred = device.preferredFormat(); QJsonArray formats;
        for (const auto format : device.supportedSampleFormats()) formats.append(int(format));
        report.insert("device_capabilities",QJsonObject{{"min_rate",device.minimumSampleRate()},{"max_rate",device.maximumSampleRate()},{"min_channels",device.minimumChannelCount()},{"max_channels",device.maximumChannelCount()},{"formats",formats},{"preferred_rate",preferred.sampleRate()},{"preferred_channels",preferred.channelCount()},{"preferred_format",int(preferred.sampleFormat())},{"preferred_supported",device.isFormatSupported(preferred)}});
        bool passed = equalizer.active() && warnings.isEmpty();
        for (const auto& item : checks) passed = passed && item.toObject().value("ok").toBool();
        report.insert("checks",checks); report.insert("qml_warnings",QJsonArray::fromStringList(warnings)); report.insert("ok",passed); writeReport(); return passed ? 0 : 1;
    }
    if (args[3] == "focus") {
        const QString localFixture = qEnvironmentVariable("BOTTLE_SMOKE_AUDIO");
        if (!QFileInfo(localFixture).isFile()) return 2;
        music.openLocal(QUrl::fromLocalFile(localFixture));
        check("local_audio_ready", waitFor([&] { return music.duration() > 50000; }, 5000));
        music.toggle(); music.seek(10000);
        window->setProperty("route", "player"); QTest::qWait(350);
        QMetaObject::invokeMethod(window, "enterImmersive", Q_ARG(QVariant, true)); QTest::qWait(350);
        auto* page = qobject_cast<QQuickItem*>(find("immersivePlayerPage"));
        auto* idle = page ? findItem(page, "immersiveIdleTimer") : nullptr;
        QSignalSpy idleEvents(idle, SIGNAL(triggered()));
        QSignalSpy playbackEvents(&music, &MusicController::playbackChanged);
        music.toggle();
        QTest::mouseMove(window, QPoint(window->width() / 2, window->height() / 2));
        QTest::qWait(3250);
        check("idle_hides_controls", page && !page->property("controlsVisible").toBool());
        report.insert("idle", QJsonObject{{"timer_events", idleEvents.count()}, {"playback_events", playbackEvents.count()},
            {"awake", page && page->property("controlsAwake").toBool()}, {"playing", music.playing()}});
        screenshot("clean");
        QTest::mouseMove(window, QPoint(window->width() / 2 + 40, window->height() / 2));
        check("pointer_wakes_controls", page && page->property("controlsVisible").toBool());
        music.toggle();
        QTest::keyClick(window, Qt::Key_Escape); QTest::qWait(350);
        check("windowed_size_restored", window->size() == QSize(1280, 850));
        window->showMaximized(); QTest::qWait(350);
        QMetaObject::invokeMethod(window, "enterImmersive", Q_ARG(QVariant, true)); QTest::qWait(350);
        QTest::keyClick(window, Qt::Key_Escape); QTest::qWait(350);
        check("maximized_restored", window->visibility() == QWindow::Maximized);
        window->showNormal(); QTest::qWait(350); window->resize(900, 620); QTest::qWait(350);
        report.insert("before_minimum", QJsonObject{{"width", window->width()}, {"height", window->height()}, {"visibility", int(window->visibility())}});
        QMetaObject::invokeMethod(window, "enterImmersive", Q_ARG(QVariant, false)); QTest::qWait(350);
        check("minimum_size", window->size() == QSize(900, 620));
        report.insert("after_minimum", QJsonObject{{"width", window->width()}, {"height", window->height()}, {"visibility", int(window->visibility())}});
        music.setReducedMotion(true); music.seek(12000); QTest::qWait(400);
        check("media_box_integer_immersive_prev",mediaGeometry("immersivePreviousButton",false));
        music.setPlaying(true);QTest::qWait(200);window->setProperty("listeningOpen",true);QTest::qWait(250);
        auto* transportGlyph=find("immersivePlayButton_glyph");
        check("transport_button_pause_glyph",transportGlyph&&transportGlyph->property("name")=="transport-pause"&&!transportGlyph->property("stroked").toBool());
        window->setProperty("listeningOpen",false);music.setPlaying(false);music.seek(12000);QTest::qWait(200);
        for(const auto& extra:captureSizes()) {
            window->resize(extra); QTest::qWait(400); const auto suffix=QStringLiteral("%1x%2").arg(extra.width()).arg(extra.height()); immersiveCompositionOk(suffix);
            for(int cue:{0,52000}) { music.seek(cue);QTest::qWait(100);immersiveCompositionOk(suffix+QStringLiteral("_cue_%1").arg(cue)); }
            music.seek(12000);QTest::qWait(100);
            auto* list=quick("immersiveLyricList"); if(list) { const auto start=sceneRect(list).center().toPoint(); QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start); QTest::mouseMove(window,start-QPoint(0,80),100); QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start-QPoint(0,80)); QTest::qWait(150); click(window,find("lyricResumeButton")); QTest::qWait(100); immersiveCompositionOk(suffix+"_after_resume"); }
            screenshot("immersive-"+suffix);
        }
        window->showFullScreen(); QTest::qWait(400); immersiveCompositionOk("fullscreen"); screenshot("fullscreen");
        report.insert("checks", checks); report.insert("qml_warnings", QJsonArray::fromStringList(warnings));
        bool passed = warnings.isEmpty(); for (const auto& item : checks) passed = passed && item.toObject().value("ok").toBool();
        report.insert("ok", passed);
        writeReport();
        window->close(); return passed ? 0 : 1;
    }
    if (args[3] == "player") {
        window->setProperty("route", "player"); QTest::qWait(350);
        music.setReducedMotion(true);
        screenshot("qt-empty");
        const QString localFixture = qEnvironmentVariable("BOTTLE_SMOKE_AUDIO");
        if (!QFileInfo(localFixture).isFile()) return 2;
        music.openLocal(QUrl::fromLocalFile(localFixture));
        check("local_audio_ready", waitFor([&] { return music.duration() > 50000; }, 5000));
        music.toggle(); music.seek(12000); QTest::qWait(600);
        check("same_lyric_line", music.lyricIndex() == 3);
        auto* page = qobject_cast<QQuickItem*>(find("playerPage"));
        const auto geometry = [&find](const char* name) {
            auto* item = qobject_cast<QQuickItem*>(find(name));
            if (!item) return QJsonObject{};
            const auto point = item->mapToScene(QPointF{});
            return QJsonObject{{"x", point.x()}, {"y", point.y()}, {"width", item->width()}, {"height", item->height()}, {"visible", item->isVisible()}};
        };
        const auto captureGeometry = [&geometry] {
            return QJsonObject{{"lyrics", geometry("playerLyricCard")}, {"coverCard", geometry("playerCoverCard")},
                {"cover", geometry("playerCoverArt")}, {"info", geometry("playerInfoCard")},
                {"toolbar", geometry("playerToolbar")}, {"activeText", geometry("activeLyricText")}};
        };
        const auto captureLyricRows = [&find, &geometry] {
            QJsonArray rows;
            for (int index = 0; index < 14; ++index) {
                const QByteArray name = "lyricLine_" + QByteArray::number(index);
                auto* row = qobject_cast<QQuickItem*>(find(name.constData()));
                if (!row) continue;
                QJsonObject value = geometry(name.constData()); value.insert("index", index);
                QJsonArray texts;
                for (auto* group : row->childItems()) for (auto* child : group->childItems()) {
                    if (!child->property("text").isValid()) continue;
                    const auto point = child->mapToScene(QPointF{});
                    texts.append(QJsonObject{{"text", child->property("text").toString()}, {"visible", child->isVisible()},
                        {"x", point.x()}, {"y", point.y()}, {"width", child->width()}, {"height", child->height()}});
                }
                value.insert("texts", texts); rows.append(value);
            }
            return rows;
        };
        if (page) QMetaObject::invokeMethod(page, "wakeTools");
        check("lyrics_visible_on_right", geometry("playerLyricCard").value("visible").toBool()
            && geometry("playerLyricCard").value("width").toDouble() >= 480
            && geometry("playerLyricCard").value("x").toDouble() > geometry("playerCoverCard").value("x").toDouble());
        report.insert("geometry_1280x850", captureGeometry());
        if (auto* text = find("activeLyricText")) {
            QTextLayout shaped(text->property("text").toString(), text->property("font").value<QFont>());
            shaped.beginLayout(); auto line = shaped.createLine(); line.setLineWidth(500); shaped.endLayout();
            QJsonArray families;
            for (const auto& run : shaped.glyphRuns()) families.append(run.rawFont().familyName());
            report.insert("lyric_font_families", families);
        }
        playerCompositionOk("1280x850");
        screenshot("qt-player-1280x850");
        window->resize(900, 620); QTest::qWait(400);
        if (page) QMetaObject::invokeMethod(page, "wakeTools");
        check("minimum_lyrics_fit", geometry("playerLyricCard").value("width").toDouble() > 300
            && geometry("playerLyricCard").value("x").toDouble() + geometry("playerLyricCard").value("width").toDouble() <= window->width());
        const auto listGeometry = geometry("lyricList"), textGeometry = geometry("activeLyricText");
        if (auto* list = find("lyricList")) {
            QJsonObject state;
            for (const auto* property : {"currentIndex", "contentY", "originY", "contentHeight", "moving", "flicking", "dragging"})
                state.insert(QLatin1String(property), QJsonValue::fromVariant(list->property(property)));
            if (auto* view = find("lyricView")) state.insert("following", view->property("followPlayback").toBool());
            report.insert("minimum_list_state", state);
        }
        playerCompositionOk("900x620");
        report.insert("geometry_900x620", captureGeometry());
        screenshot("qt-player-900x620");
        QTest::mouseMove(window,QPoint(quick("playerCardGrid")->mapToScene(QPointF(8,8)).toPoint())); QTest::qWait(200);
        check("media_glyph_optical_centre_player_play",mediaGeometry("playButton",true)); check("media_box_integer_prev",mediaGeometry("prevButton",false)); check("media_box_integer_next",mediaGeometry("nextButton",false));
        music.setPlaying(true); QTest::qWait(200); check("media_glyph_optical_centre_player_pause",mediaGeometry("playButton",true)); screenshot("playerbar-playing"); music.setPlaying(false); music.seek(12000); screenshot("playerbar-paused");
        for(const auto& extra:captureSizes()) { window->resize(extra); QTest::qWait(400); playerCompositionOk(QStringLiteral("%1x%2").arg(extra.width()).arg(extra.height())); screenshot(QStringLiteral("qt-player-%1x%2").arg(extra.width()).arg(extra.height())); }
        for(const auto& extra:{QSize(1280,850),QSize(900,620)}) {
            window->resize(extra);auto mirrored=layoutModel.cards();for(auto it=mirrored.begin();it!=mirrored.end();++it){auto cell=it.value().toMap();cell["x"]=12-cell.value("x").toInt()-cell.value("w").toInt();it.value()=cell;}
            check("mirrored_layout_commits",layoutModel.commit(mirrored));QTest::qWait(250);
            auto* text=quick("activeLyricText");const auto art=sceneRect(quick("playerCoverArt"));
            check(QStringLiteral("mirrored_cover_lyric_gap_%1x%2").arg(extra.width()).arg(extra.height()),text&&std::abs(art.left()-sceneRect(text).right()-themeCall("coverLyricGap",{art.width()}).toNumber())<=2);
            screenshot(QStringLiteral("qt-player-mirrored-%1x%2").arg(extra.width()).arg(extra.height()));layoutModel.reset();QTest::qWait(200);
        }

        window->resize(1280, 850); QTest::qWait(400);
        if (page) QMetaObject::invokeMethod(page, "wakeTools");
        click(window, find("playerEditLayout"));
        check("layout_editor_opens", page && page->property("editing").toBool());
        click(window, find("playerLayoutFrame_cover"));
        QTest::keyClick(window, Qt::Key_Up, Qt::ShiftModifier); QTest::qWait(150);
        check("keyboard_resizes_card", layoutModel.cards().value("cover").toMap().value("h").toInt() == 4);
        auto* frame = qobject_cast<QQuickItem*>(find("playerLayoutFrame_info"));
        if (frame) {
            const QPoint start = frame->mapToScene(QPointF(frame->width()/2, frame->height()/2)).toPoint();
            const QPoint end = start - QPoint(0, 104);
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(window, end, 80); QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end); QTest::qWait(150);
        }
        check("pointer_moves_card", layoutModel.cards().value("info").toMap().value("y").toInt() == 4);
        click(window, find("playerHide_cover"));
        check("hide_cover", !layoutModel.cards().contains("cover"));
        click(window, find("playerAdd_cover"));
        check("restore_cover", layoutModel.cards().contains("cover"));
        click(window, find("playerAdd_progress")); click(window, find("playerAdd_quality"));
        check("optional_cards_are_live", layoutModel.cards().contains("progress") && layoutModel.cards().contains("quality"));
        screenshot("qt-layout-editor");
        click(window, find("playerResetLayout")); click(window, find("playerDoneLayout"));
        check("reset_and_finish", page && !page->property("editing").toBool() && layoutModel.cards().size() == 3);
        click(window, find("playerShelfButton"));
        check("cover_shelf_opens", page && page->property("shelfOpen").toBool()); screenshot("qt-cover-shelf");
        QTest::keyClick(window, Qt::Key_Escape); QTest::qWait(150);
        check("escape_closes_shelf", page && !page->property("shelfOpen").toBool() && window->property("route").toString() == "player");
        click(window, find("playerEditLayout"));
        check("editor_reopens_after_shelf", page && page->property("editing").toBool());
        QTest::keyClick(window, Qt::Key_Escape); QTest::qWait(150);
        check("escape_finishes_editing", page && !page->property("editing").toBool() && window->property("route").toString() == "player");
        check("editing_preserves_audio_clock", !music.playing() && std::abs(music.position() - 12000) < 100);
        click(window, find("lyricLine_5"));
        check("click_lyric_seeks_real_clock", music.lyricIndex() == 5 && std::abs(music.position() - 20000) < 100);
        music.seek(12000);
        QJsonArray fontSteps;
        double previousSize = 0;
        for (const auto* name : {"lyricSizeM", "lyricSizeL"}) {
            click(window, find("settingsButton"));
            auto* settingsPage = qobject_cast<QQuickItem*>(find("settingsPage"));
            auto* option = qobject_cast<QQuickItem*>(find(name));
            if (settingsPage && option) {
                const double sceneY = option->mapToScene(QPointF(0, option->height()/2)).y();
                settingsPage->setProperty("contentY", std::max(0.0, settingsPage->property("contentY").toDouble() + sceneY - window->height()/2));
                QTest::qWait(150);
            }
            click(window, find(name));
            const int setting = music.lyricSize();
            click(window, find("settingsClose"));
            QTest::qWait(200);
            auto* view = find("lyricView"); auto* active = find("activeLyricText");
            const double size = view ? view->property("textSize").toDouble() : 0;
            const QFont font = active ? active->property("font").value<QFont>() : QFont{};
            fontSteps.append(QJsonObject{{"control", QLatin1String(name)}, {"setting", setting}, {"size", size}, {"point_size", font.pointSizeF()}});
            check(QStringLiteral("font_control_") + QLatin1String(name), option && size > previousSize
                && font.pixelSize() == size);
            previousSize = size;
        }
        report.insert("font_steps", fontSteps);
        music.seek(36000); QTest::qWait(150);
        screenshot("qt-player-xl-1280x850");
        window->resize(900, 620); QTest::qWait(400);
        const auto xlList = geometry("lyricList"), xlText = geometry("activeLyricText");
        // With the reference's 20px base and .82 compact scale this cue fits
        // on one line. The long bilingual fixture below verifies wrapping.
        playerCompositionOk("xl_900x620");
        screenshot("qt-player-xl-900x620");
        const QString longFixture = qEnvironmentVariable("BOTTLE_SMOKE_LONG_AUDIO");
        if (!longFixture.isEmpty()) {
            music.openLocal(QUrl::fromLocalFile(longFixture));
            check("long_fixture_ready", waitFor([&] { return music.duration() > 50000
                && music.lyrics().size() == 14 && !music.lyrics()[3].toMap().value("translation").toString().isEmpty(); }, 5000));
            if (music.playing()) music.toggle();
            music.setLyricSize(32); music.seek(12000); QTest::qWait(250);
            const auto currentRow = geometry("lyricLine_3"), list = geometry("lyricList");
            const auto translation = geometry("activeLyricTranslation");
            check("long_bilingual_lyrics_fit", translation.value("visible").toBool()
                && currentRow.value("height").toDouble() > 100
                && currentRow.value("y").toDouble() >= list.value("y").toDouble()
                && currentRow.value("y").toDouble() + currentRow.value("height").toDouble() <= list.value("y").toDouble() + list.value("height").toDouble());
            auto* title = find("playerTrackTitle");
            check("long_title_elides_inside_info", title && title->property("truncated").toBool()
                && geometry("playerTrackTitle").value("height").toDouble() <= geometry("playerInfoCard").value("height").toDouble());
            if (auto* currentPage = find("playerPage")) QMetaObject::invokeMethod(currentPage, "wakeTools");
            screenshot("qt-player-long-900x620");
            report.insert("long_lyric_rows", captureLyricRows());
            const auto longRow = geometry("lyricLine_3"), followingRow = geometry("lyricLine_4");
            check("long_rows_do_not_overlap", !followingRow.isEmpty() && followingRow.value("y").toDouble()
                >= longRow.value("y").toDouble() + longRow.value("height").toDouble() - 0.1);
            window->resize(1280, 850); QTest::qWait(250);
            screenshot("qt-player-long-1280x850");
            music.openLocal(QUrl::fromLocalFile(localFixture));
            waitFor([&] { return music.duration() > 50000 && music.lyrics().size() == 14; }, 5000);
            if (music.playing()) music.toggle();
        }
        music.seek(12000); music.setLyricSize(32); window->resize(1280, 850); QTest::qWait(350);
        music.setReducedMotion(false);
        auto* idlePage = find("playerPage"); auto* idleToolbar = find("playerToolbar");
        if (auto* button = qobject_cast<QQuickItem*>(find("playerShelfButton")))
            QTest::mouseMove(window, button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint());
        check("stationary_pointer_allows_toolbar_idle", waitFor([&] {
            return idlePage && idleToolbar && !idlePage->property("toolsAwake").toBool()
                && idleToolbar->property("opacity").toDouble() < 0.001;
        }, 7000));
        // The hidden buttons finish their 120ms hover-color transition too.
        QTest::qWait(250);
        int idleFrames = 0;
        auto* idleList = find("lyricList");
        QSignalSpy scrollEvents(idleList, SIGNAL(contentYChanged()));
        QSignalSpy layoutEvents(idleList, SIGNAL(contentHeightChanged()));
        const double idleStartY = idleList ? idleList->property("contentY").toDouble() : 0;
        QSignalSpy positionEvents(&music, &MusicController::positionChanged);
        QSignalSpy playbackEvents(&music, &MusicController::playbackChanged);
        auto* focusItem = window->activeFocusItem();
        QJsonObject idleState{{"playing", music.playing()}, {"focus_name", focusItem ? focusItem->objectName() : QString()},
            {"focus_type", focusItem ? QString::fromLatin1(focusItem->metaObject()->className()) : QString()},
            {"tools_awake", idlePage && idlePage->property("toolsAwake").toBool()},
            {"toolbar_opacity", idleToolbar ? idleToolbar->property("opacity").toDouble() : 0}};
        QElapsedTimer frameClock; frameClock.start(); QJsonArray frameTimes;
        const auto idleConnection = QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&] { ++idleFrames; frameTimes.append(frameClock.elapsed()); });
        QTest::qWait(1000); QObject::disconnect(idleConnection);
        report.insert("idle_list_events", QJsonObject{{"scroll", scrollEvents.count()}, {"layout", layoutEvents.count()},
            {"start_y", idleStartY}, {"end_y", idleList ? idleList->property("contentY").toDouble() : 0}});
        idleState.insert("position_events", positionEvents.count()); idleState.insert("playback_events", playbackEvents.count());
        idleState.insert("frame_times_ms", frameTimes); report.insert("idle_state", idleState);
        screenshot("qt-player-after-settings");
        report.insert("paused_frames_in_one_second", idleFrames);
        report.insert("window_exposed", window->isExposed());
        check("stationary_pointer_keeps_tools_idle", idlePage && !idlePage->property("toolsAwake").toBool());
        check("paused_render_settles", window->isExposed() && idleFrames <= 2);
        QTest::mouseMove(window, QPoint(1000, 300));
        check("moving_pointer_wakes_toolbar", waitFor([&] {
            return idlePage && idleToolbar && idlePage->property("toolsAwake").toBool()
                && idleToolbar->property("opacity").toDouble() > 0.99;
        }, 1500));
        screenshot("qt-player-final");
        report.insert("final_lyric_rows", captureLyricRows());
        const auto finalRow = geometry("lyricLine_3"), finalNext = geometry("lyricLine_4");
        check("track_switch_replaces_lyric_rows", !finalNext.isEmpty() && finalNext.value("y").toDouble()
            >= finalRow.value("y").toDouble() + finalRow.value("height").toDouble() - 0.1
            && !geometry("activeLyricTranslation").value("visible").toBool());
        report.insert("dpr", window->devicePixelRatio());
        report.insert("checks", checks); report.insert("qml_warnings", QJsonArray::fromStringList(warnings));
        bool passed = warnings.isEmpty(); for (const auto& item : checks) passed = passed && item.toObject().value("ok").toBool();
        report.insert("ok", passed); writeReport(); window->close(); return passed ? 0 : 1;
    }
    music.initialize();
    check("native_initialization", waitFor([&] { return music.ready(); }));
    check("real_public_feed", waitFor([&] { return music.feed().size() >= 20; }, 40000));
    report.insert("native_fingerprint", music.fingerprint());
    check("paper_home_default", find("paperPage")
        && find("paperSidebar")->property("visible").toBool());
    QMetaObject::invokeMethod(window,"navigate",Q_ARG(QVariant,QStringLiteral("home"))); QTest::qWait(500);
    check("paper_render", screenshot("paper"));
    auto* searchInput = find("searchInput");
    if (searchInput) { searchInput->setProperty("text", QStringLiteral("隔岸")); QMetaObject::invokeMethod(searchInput, "accepted"); }
    check("search_page_one", waitFor([&] { return !music.searching() && music.searchResults().size() >= 25; }, 30000));
    const auto firstPage = music.searchResults().size();
    music.search(QStringLiteral("隔岸"), true);
    check("search_pagination", waitFor([&] { return !music.searching() && music.searchResults().size() > firstPage; }, 30000));
    report.insert("search_loaded", music.searchResults().size()); report.insert("search_total", music.searchTotal());
    check("search_render", screenshot("search"));
    qint64 decodedFrames = 0;
    QAudioBufferOutput fallbackBuffers;
    auto* buffers = music.mediaPlayer()->audioBufferOutput();
    if (!buffers) { buffers = &fallbackBuffers; music.mediaPlayer()->setAudioBufferOutput(buffers); }
    QObject::connect(buffers, &QAudioBufferOutput::audioBufferReceived, &app, [&decodedFrames](const QAudioBuffer& buffer) { decodedFrames += buffer.frameCount(); });
    if (music.searchResults().size()) music.playFrom("search", 0);
    check("real_song_audio_decodes", waitFor([&] { return decodedFrames > 1000 && music.position() > 500; }, 40000));
    report.insert("decoded_frames", decodedFrames);
    check("lyrics_loaded", waitFor([&] { return !music.lyrics().isEmpty(); }, 20000));
    music.toggle(); QTest::qWait(200);
    const auto pausedAt = music.position(); QTest::qWait(450);
    check("pause_keeps_audio_clock", !music.playing() && std::abs(music.position() - pausedAt) < 100);
    music.seek(20000); check("seek_uses_audio_clock", waitFor([&] { return music.position() >= 19500; }, 5000));
    check("retired_presentation_entries_absent", !find("stageEntry") && !find("settingsOpenCurtain"));
    window->setProperty("listeningOpen",true); QTest::qWait(200);
    check("one_queue_panel",find("queuePanel") && find("queueSearch") && !find("listeningPanel"));
    screenshot("queue"); window->setProperty("listeningOpen",false);
    window->resize(900, 620); QTest::qWait(400); check("minimum_window_size", window->size() == QSize(900, 620)); check("minimum_window_render", screenshot("minimum"));
    window->resize(1600, 1000); QTest::qWait(400); check("wide_window_size", window->size() == QSize(1600, 1000)); check("wide_window_render", screenshot("wide"));
    window->setWidth(1280); window->setHeight(850);
    for (int i=0;i<20;i++) { window->setProperty("route", "home"); QTest::qWait(30); window->setProperty("route", "player"); QTest::qWait(30); }
    check("player_exit_unmounts", (window->setProperty("route", "home"), QTest::qWait(100), find("playerPage") == nullptr));
    window->setProperty("route", "player"); music.setLyricSize(38);
    check("player_render", screenshot("player"));
    music.setVinyl(true); check("vinyl_render", screenshot("vinyl"));
    music.setLyricSize(32); music.setVinyl(false);
    QMetaObject::invokeMethod(window, "openSettings", Q_ARG(QVariant, QStringLiteral("appearance")));
    QTest::qWait(400);
    window->setProperty("route","home");
    check("paper_mode_returns", find("paperSidebar")->property("visible").toBool());
    click(window, find("playerCoverButton")); check("paper_player_render", screenshot("paper-player"));
    click(window, find("settingsButton"));
    check("settings_route", window->property("route").toString() == "settings" && find("settingsPage") != nullptr);
    check("settings_render", screenshot("settings"));
    click(window, find("settingsSection_playback"));
    check("settings_section_nav", window->property("settingsSection").toString() == "playback");
    check("settings_playback_render", screenshot("settings-playback"));
    click(window, find("settingsClose"));
    check("settings_close_returns", window->property("route").toString() == "player");
    click(window, find("homeEntry")); check("paper_home_after_settings", window->property("route").toString() == "home");
    // Player bar side keys: loop cycles list → single → random → list with
    // a 2s notice; the heart keeps a local favourite for the current track.
    click(window, find("loopButton"));
    check("loop_cycles_to_single", music.loopMode() == QLatin1String("single")
        && window->property("notice").toString().contains(QStringLiteral("单曲循环")));
    click(window, find("favoriteButton"));
    check("favorite_toggles_locally", music.favorite() && window->property("notice").toString().contains(QStringLiteral("收藏")));
    check("player_states_render", screenshot("player-states"));
    click(window, find("loopButton")); click(window, find("loopButton"));
    check("loop_cycles_back_to_list", music.loopMode() == QLatin1String("list"));
    click(window, find("favoriteButton"));
    check("favorite_untoggles", !music.favorite());
    click(window, find("playerCoverButton"));
    for (const auto& line : music.lyrics()) {
        const auto cue = line.toMap();
        if (cue.value("time").toLongLong() >= 40000 && cue.value("text").toString().size() < 60) {
            music.seek(cue.value("time").toLongLong()); QTest::qWait(200); break;
        }
    }
    const auto routeBeforeFullscreen = window->property("route").toString();
    const auto trackBeforeFullscreen = music.current().value("hash");
    const auto clockBeforeFullscreen = music.position();
    const auto visibilityBeforeFullscreen = window->visibility();
    click(window, find("playerFullscreenButton"));
    check("fullscreen_entry", window->property("immersive").toBool() && window->visibility() == visibilityBeforeFullscreen);
    check("fullscreen_preserves_playback", music.current().value("hash") == trackBeforeFullscreen
        && !music.playing() && std::abs(music.position() - clockBeforeFullscreen) < 100);
    auto* immersive = qobject_cast<QQuickItem*>(find("immersivePlayerPage"));
    const auto immersiveFind = [&immersive](const char* name) { return findItem(immersive, QLatin1String(name)); };
    check("fullscreen_hides_shell", !find("paperSidebar")->property("visible").toBool()
        && !find("paperPlayerDock")->property("visible").toBool()
        && !find("windowTitlebar")->property("visible").toBool() && !find("playerPage"));
    check("immersive_render", screenshot("immersive-fullscreen"));
    auto* background = immersiveFind("immersiveBackground");
    check("paused_background_stops", background && !background->property("flowing").toBool());
    QTest::keyClick(window, Qt::Key_F11); QTest::qWait(350);
    check("f11_windowed_immersive", window->property("immersive").toBool() && window->visibility() == QWindow::Windowed);
    check("f11_restores_window_geometry", window->size() == QSize(1280, 850));
    check("immersive_window_render", screenshot("immersive-window"));
    auto* lyricList = qobject_cast<QQuickItem*>(immersiveFind("immersiveLyricList"));
    if (lyricList) {
        const auto origin = lyricList->mapToScene(QPointF(lyricList->width() / 2, lyricList->height() / 2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, origin);
        for (int i = 1; i <= 8; ++i) QTest::mouseMove(window, origin + QPoint(0, i * 12), 20);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, origin + QPoint(0, 96));
        QTest::qWait(200);
    }
    auto* lyricView = immersiveFind("immersiveLyricView");
    check("manual_lyrics_suspend_follow", lyricView && !lyricView->property("followPlayback").toBool());
    click(window, immersiveFind("lyricResumeButton"));
    check("lyrics_resume_follow", lyricView && lyricView->property("followPlayback").toBool());
    QTest::keyClick(window, Qt::Key_F11); QTest::qWait(250);
    click(window, immersiveFind("immersivePlayButton"));
    check("immersive_play_control", music.playing());
    music.setReducedMotion(true); QTest::qWait(150);
    check("reduced_motion_stops_background", background && !background->property("flowing").toBool());
    music.setReducedMotion(false);
    QTest::mouseMove(window, QPoint(window->width() * 0.43, window->height() * 0.7));
    QTest::qWait(3250);
    report.insert("fullscreen_idle_state", QJsonObject{
        {"playing", music.playing()}, {"visibility", int(window->visibility())},
        {"awake", immersive && immersive->property("controlsAwake").toBool()}});
    check("fullscreen_idle_hides_controls", immersive && !immersive->property("controlsVisible").toBool());
    check("immersive_clean_render", screenshot("immersive-clean"));
    QTest::mouseMove(window, QPoint(window->width() * 0.43 + 40, window->height() * 0.7));
    check("pointer_restores_controls", immersive && immersive->property("controlsVisible").toBool());
    click(window, immersiveFind("immersivePlayButton"));
    check("immersive_pause_control", !music.playing());
    QTest::keyClick(window, Qt::Key_Escape); QTest::qWait(350);
    immersive = nullptr;
    check("escape_restores_page_and_window", !window->property("immersive").toBool()
        && window->visibility() == QWindow::Windowed && window->size() == QSize(1280, 850)
        && window->property("route").toString() == routeBeforeFullscreen);
    check("immersive_exit_unmounts", !find("immersivePlayerPage") && find("playerPage"));
    click(window, find("dockFullscreenButton"));
    check("dock_fullscreen_entry", window->property("immersive").toBool());
    immersive = qobject_cast<QQuickItem*>(find("immersivePlayerPage"));
    click(window, immersiveFind("immersiveCloseButton"));
    immersive = nullptr;
    check("collapse_restores_playback", !window->property("immersive").toBool()
        && !music.playing() && music.current().value("hash") == trackBeforeFullscreen);
    window->showMaximized(); QTest::qWait(250);
    QMetaObject::invokeMethod(window, "enterImmersive", Q_ARG(QVariant, true)); QTest::qWait(250);
    QTest::keyClick(window, Qt::Key_Escape); QTest::qWait(250);
    check("fullscreen_restores_maximized", window->visibility() == QWindow::Maximized && !window->property("immersive").toBool());
    window->showNormal(); QTest::qWait(350); window->resize(900, 620); QTest::qWait(350);
    QMetaObject::invokeMethod(window, "enterImmersive", Q_ARG(QVariant, false)); QTest::qWait(250);
    check("immersive_minimum_geometry", window->size() == QSize(900, 620));
    check("immersive_minimum_render", screenshot("immersive-minimum"));
    QTest::keyClick(window, Qt::Key_Escape); QTest::qWait(250);
    window->resize(1280, 850);
    // A local, generated silence clip exercises sidecar lyrics without a
    // network service or licensed recording. These are fixture sentences.
    const QString fixtureBase = QDir(args[2]).filePath("Quiet Morning");
    QByteArray wav(44 + 44100 * 2 * 60, '\0');
    memcpy(wav.data(), "RIFF", 4); memcpy(wav.data() + 8, "WAVEfmt ", 8); memcpy(wav.data() + 36, "data", 4);
    qToLittleEndian<quint32>(quint32(wav.size() - 8), wav.data() + 4);
    qToLittleEndian<quint32>(16, wav.data() + 16);
    qToLittleEndian<quint16>(1, wav.data() + 20); qToLittleEndian<quint16>(1, wav.data() + 22);
    qToLittleEndian<quint32>(44100, wav.data() + 24); qToLittleEndian<quint32>(88200, wav.data() + 28);
    qToLittleEndian<quint16>(2, wav.data() + 32); qToLittleEndian<quint16>(16, wav.data() + 34);
    qToLittleEndian<quint32>(quint32(wav.size() - 44), wav.data() + 40);
    QFile localAudio(fixtureBase + ".wav");
    const bool audioFixture = localAudio.open(QIODevice::WriteOnly) && localAudio.write(wav) == wav.size();
    localAudio.close();
    QFile localLyrics(fixtureBase + ".lrc");
    const bool lyricFixture = localLyrics.open(QIODevice::WriteOnly);
    if (lyricFixture) localLyrics.write(QStringLiteral(
        "[00:00.000]A quiet morning\n[00:00.000]安静的清晨\n"
        "[00:05.000]The light comes through the window\n[00:05.000]光线照进窗里\n"
        "[00:10.000]We take our time\n[00:10.000]让时间慢一点\n"
        "[00:15.000]And listen to the world outside\n[00:15.000]听一听窗外的世界\n"
        "[00:20.000]A new day is waiting\n[00:20.000]新的一天正在等待\n"
        "[00:25.000]Walk beside me\n[00:25.000]陪我慢慢走\n").toUtf8());
    localLyrics.close();
    QImage localCover(48, 48, QImage::Format_RGB32); localCover.fill(QColor("#5a7ab4"));
    const bool coverFixture = localCover.save(fixtureBase + ".png");
    check("local_fixtures_created", audioFixture && lyricFixture && coverFixture);
    music.openLocal(QUrl::fromLocalFile(fixtureBase + ".wav"));
    check("local_bilingual_sidecar", waitFor([&] { return music.duration() > 50000 && music.lyrics().size() == 6; }, 5000)
        && music.lyrics().first().toMap().value("translation").toString() == QStringLiteral("安静的清晨"));
    check("local_cover_sidecar", music.current().value("image").toString().endsWith("Quiet Morning.png"));
    music.toggle(); music.seek(15000);
    waitFor([&] { return music.lyricIndex() == 3; }, 3000);
    QMetaObject::invokeMethod(window, "enterImmersive", Q_ARG(QVariant, false)); QTest::qWait(600);
    immersive = qobject_cast<QQuickItem*>(find("immersivePlayerPage"));
    check("bilingual_translation_renders", immersiveFind("activeLyricTranslation")
        && immersiveFind("activeLyricTranslation")->property("text").toString() == QStringLiteral("听一听窗外的世界"));
    auto* backLine = immersiveFind("lyricLine_2");
    click(window, backLine);
    check("lyric_click_seeks_backward", waitFor([&] { return music.lyricIndex() == 2 && music.position() < 11000; }, 3000));
    check("bilingual_render", screenshot("immersive-bilingual"));
    QTest::keyClick(window, Qt::Key_Escape); QTest::qWait(200);
    QFile noLyrics(fixtureBase + " empty.wav");
    if (noLyrics.open(QIODevice::WriteOnly)) noLyrics.write(wav);
    noLyrics.close();
    music.openLocal(QUrl::fromLocalFile(fixtureBase + " empty.wav")); QTest::qWait(300); music.toggle();
    QMetaObject::invokeMethod(window, "enterImmersive", Q_ARG(QVariant, false)); QTest::qWait(500);
    check("track_change_clears_lyrics_and_cover", music.lyrics().isEmpty() && music.current().value("image").toString().isEmpty());
    check("no_lyrics_render", screenshot("immersive-empty"));
    QTest::keyClick(window, Qt::Key_Escape); QTest::qWait(250);
    if (args[3] == "hold") {
        window->setProperty("route", "home");
        QTimer::singleShot(90000, &app, &QCoreApplication::quit); app.exec();
    }
    report.insert("qml_warnings", QJsonArray::fromStringList(warnings)); report.insert("checks", checks);
    report.insert("qt_version", qVersion());
    bool passed = warnings.isEmpty(); for (auto item : checks) passed = passed && item.toObject().value("ok").toBool();
    report.insert("ok", passed);
    writeReport();
    window->close(); music.mediaPlayer()->setAudioBufferOutput(nullptr);
    return passed ? 0 : 1;
}
