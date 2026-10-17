#include "music/MusicController.h"
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
#include "home/HomeController.h"
#include "ui/UiAssets.h"
#include "ui/WindowPresentation.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QBuffer>
#include <QtTest>
#include <algorithm>
#include <chrono>
#include <mutex>

namespace {
QStringList graphicsMessages;
QStringList renderWarnings;
std::mutex messagesMutex;
void logMessage(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    const QString category = QString::fromLatin1(context.category);
    std::lock_guard lock(messagesMutex);
    if (category.startsWith("qt.rhi") || category == "qt.scenegraph.general") {
        graphicsMessages.append(category + ": " + message);
    }
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
        renderWarnings.append(message);
}
using Clock = std::chrono::steady_clock;
double ms(Clock::duration elapsed) {
    return std::chrono::duration<double, std::milli>(elapsed).count();
}
QJsonObject distribution(QVector<double> values) {
    if (values.isEmpty()) return {{"count", 0}};
    std::sort(values.begin(), values.end());
    return {{"count", values.size()}, {"p50_ms", values[values.size() / 2]},
        {"p95_ms", values[(values.size() - 1) * 95 / 100]}, {"max_ms", values.last()}};
}
// A compositor capture observes the old presented buffer during a resize.
// QQuickWindow::grabWindow() renders a fresh frame and hides that interval.
int blackEdge(const QImage& image) {
    if (image.width() < 2 || image.height() < 2) return -1;
    int right = 0, bottom = 0;
    for (int y = 0; y < image.height(); ++y)
        if ((image.pixel(image.width() - 2, y) & 0x00ffffff) == 0) ++right;
    for (int x = 0; x < image.width(); ++x)
        if ((image.pixel(x, image.height() - 2) & 0x00ffffff) == 0) ++bottom;
    return std::max(right * 100 / image.height(), bottom * 100 / image.width());
}
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    if (!qEnvironmentVariableIntValue("BOTTLE_PROBE_ORIGINAL")) configureWindowPresentation();
    qInstallMessageHandler(logMessage);
    if (app.arguments().size() != 2) {
        fprintf(stderr, "Expected an evidence directory, received %lld arguments.\n", app.arguments().size());
        return 2;
    }
    const QDir evidence(app.arguments()[1]);
    if (!QDir().mkpath(evidence.path())) {
        fprintf(stderr, "Cannot create evidence directory: %s\n", qPrintable(evidence.path()));
        return 2;
    }
    MusicController music(QString(), evidence.filePath("profile")); // No network or real profile.
    AccountController account(music.nativeGateway()); LibraryController library(music.nativeGateway());
    VipController vip(music.nativeGateway());
    HistoryController history(music.nativeGateway(), evidence.filePath("profile")); StatsController stats(music.nativeGateway());
    HomeController home(music.nativeGateway());
    AiAnalyzer analyzer;
    FavoriteController favorite(music.nativeGateway(), evidence.filePath("profile"));
    PlayerLayout playerLayout(evidence.filePath("profile"));
    AppearanceController appearance(evidence.filePath("profile"));
    SettingsController settings(music.nativeGateway(), evidence.filePath("profile"));
    DataMigration migration(evidence.filePath("profile"));
    migration.setNativeGateway(&music.nativeGateway());
    UpdateController updates(evidence.filePath("profile"));
    EqualizerController equalizer(*music.mediaPlayer(), *music.audioOutput(), evidence.filePath("profile"));
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
    engine.loadFromModule("BottleMusic", "Main");
    if (engine.rootObjects().isEmpty()) return 3;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window || !QTest::qWaitForWindowExposed(window)) return 3;
    window->setPosition(50, 50);
    window->raise();
    window->requestActivate();
    if (!QTest::qWaitForWindowActive(window, 3000)) {
        fprintf(stderr, "The test window is obscured or inactive; no compositor capture is valid.\n");
        return 5;
    }
    std::mutex framesMutex;
    QVector<double> frames;
    auto lastFrame = Clock::now();
    const auto frameConnection = QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&] {
        std::lock_guard lock(framesMutex);
        const auto now = Clock::now();
        frames.append(ms(now - lastFrame)); lastFrame = now;
    }, Qt::DirectConnection);

    QJsonArray cases;
    bool passed = true;
    const bool capturesEnabled = !qEnvironmentVariableIntValue("BOTTLE_PROBE_NO_CAPTURE");
    QJsonObject capturedImages;
    const auto captureWindow = [&]() {
        if (!window->isActive()) return QImage();
        const QRect area = window->geometry().adjusted(2, 2, -2, -2);
        for (const auto point : {area.topLeft(), area.topRight(), area.bottomLeft(), area.bottomRight(), area.center()})
            if (QGuiApplication::topLevelAt(point) != window) return QImage();
        const auto screen = window->screen();
        const auto position = window->position() - screen->geometry().topLeft();
        return screen->grabWindow(0, position.x(), position.y(), window->width(), window->height()).toImage();
    };
    const auto saveCapture = [&](const QString& name, const QImage& capture) {
        if (capture.isNull()) return;
        if (capture.save(evidence.filePath(name + ".png"))) return;
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
        if (capture.save(&buffer, "PNG")) capturedImages.insert(name, QString::fromLatin1(bytes.toBase64()));
    };
    for (const QString mode : {QStringLiteral("paper"), QStringLiteral("curtain"), QStringLiteral("immersive")}) {
        if (mode == "immersive")
            QMetaObject::invokeMethod(window, "enterImmersive", Q_ARG(QVariant, false));
        else window->setProperty("presentation", mode);
        window->resize(900, 620);
        QTest::qWait(800);
        { std::lock_guard lock(framesMutex); frames.clear(); lastFrame = Clock::now(); }
        QVector<double> guiGaps;
        int blackCaptures = 0, validCaptures = 0, worstEdge = 0;
        const auto available = window->screen()->availableGeometry();
        const int widthGrowth = std::min(590, available.width() - 100 - 900);
        const int heightGrowth = std::min(295, available.height() - 100 - 620);
        auto lastStep = Clock::now();
        for (int step = 0; step < 120; ++step) {
            const auto now = Clock::now(); guiGaps.append(ms(now - lastStep)); lastStep = now;
            const int ramp = step < 60 ? step : 119 - step;
            window->resize(900 + ramp * std::max(0, widthGrowth) / 59,
                620 + ramp * std::max(0, heightGrowth) / 59);
            if (step % 6 == 0 && window->isActive() && capturesEnabled) {
                const auto capture = captureWindow();
                const int edge = blackEdge(capture);
                if (step == 6) saveCapture(mode + "-resize", capture);
                if (edge >= 0) ++validCaptures;
                if (edge >= 80) {
                    if (blackCaptures == 0) saveCapture(mode + "-resize-black", capture);
                    ++blackCaptures;
                }
                worstEdge = std::max(worstEdge, edge);
            }
            QTest::qWait(16);
        }
        QVector<double> frameCopy;
        { std::lock_guard lock(framesMutex); frameCopy = frames; }
        QTest::qWait(300);
        const auto settled = captureWindow();
        saveCapture(mode + "-settled", settled);
        passed = passed && !frameCopy.isEmpty() && blackEdge(settled) == 0
            && (!capturesEnabled || (validCaptures == 20 && blackCaptures == 0));
        cases.append(QJsonObject{{"mode", mode}, {"frames", distribution(frameCopy)},
            {"gui_steps", distribution(guiGaps)}, {"black_captures", blackCaptures},
            {"valid_captures", validCaptures}, {"worst_edge_percent", worstEdge},
            {"settled_edge_percent", blackEdge(settled)}});
    }
    window->close();
    QTest::qWait(100); // Drain render-thread signals before destroying samples.
    QObject::disconnect(frameConnection);
    std::lock_guard messagesLock(messagesMutex);
    passed = passed && renderWarnings.isEmpty();
    QJsonObject report{{"qt_version", qVersion()}, {"graphics", QJsonArray::fromStringList(graphicsMessages)},
        {"cases", cases}, {"captured_images", capturedImages},
        {"warnings", QJsonArray::fromStringList(renderWarnings)}, {"ok", passed}};
    QFile file(evidence.filePath("resize.json"));
    const auto json = QJsonDocument(report).toJson();
    fwrite(json.constData(), 1, size_t(json.size()), stdout);
    if (!file.open(QIODevice::WriteOnly)) {
        fprintf(stderr, "Cannot write %s: %s\n", qPrintable(file.fileName()), qPrintable(file.errorString()));
        return ferror(stdout) ? 4 : passed ? 0 : 1; // A sandbox may permit stdout but forbid app file writes.
    }
    file.write(json);
    return passed ? 0 : 1;
}
