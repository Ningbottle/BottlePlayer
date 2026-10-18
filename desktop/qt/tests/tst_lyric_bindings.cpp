#include <QGuiApplication>
#include <QFileInfo>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>
#include <memory>

class LyricMusicFixture final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList lyrics READ lyrics NOTIFY lyricsChanged)
    Q_PROPERTY(qint64 position MEMBER position_ NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration MEMBER duration_ NOTIFY durationChanged)
    Q_PROPERTY(int lyricIndex MEMBER index_ NOTIFY lyricIndexChanged)
    Q_PROPERTY(int lyricSize MEMBER size_ CONSTANT)
    Q_PROPERTY(bool playing MEMBER playing_ NOTIFY playbackChanged)
    Q_PROPERTY(bool loading MEMBER loading_ NOTIFY playbackChanged)
    Q_PROPERTY(bool reducedMotion MEMBER reducedMotion_ NOTIFY appearanceChanged)
public:
    QVariantList lyrics() const { ++reads; return rows; }
    Q_INVOKABLE void seek(qint64 position) { emit seekRequested(position); }
    void position(qint64 value) { position_ = value; emit positionChanged(); }
    void index(int value) { index_ = value; emit lyricIndexChanged(); }
    QVariantList rows;
    mutable int reads = 0;
    qint64 position_ = 1000, duration_ = 17000;
    int index_ = 0, size_ = 32;
    bool playing_ = false, loading_ = false, reducedMotion_ = false;
signals:
    void lyricsChanged();
    void positionChanged();
    void durationChanged();
    void lyricIndexChanged();
    void playbackChanged();
    void appearanceChanged();
    void seekRequested(qint64 position);
};

class LyricAppearanceFixture final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int sidebarWidth READ sidebarWidth CONSTANT)
public:
    int sidebarWidth() const { return 232; }
};

namespace {
class TestIcons final : public QQuickImageProvider {
public:
    TestIcons() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString&, QSize* size, const QSize&) override {
        QImage image(16, 16, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        if (size) *size = image.size();
        return image;
    }
};
QVariantMap cue(int time, const QString& text, const QString& translation = {}, const QVariantList& words = {}) {
    return {{"time", time}, {"text", text}, {"translation", translation}, {"words", words}};
}
QObject* findItem(QQuickItem* item, const QString& name) {
    if (!item) return nullptr;
    if (item->objectName() == name) return item;
    if (auto* object = item->findChild<QObject*>(name)) return object;
    for (auto* child : item->childItems())
        if (auto* object = findItem(child, name)) return object;
    return nullptr;
}
struct LyricScene {
    LyricMusicFixture music;
    LyricAppearanceFixture appearance;
    QQmlEngine engine;
    std::unique_ptr<QObject> object;
    QQuickWindow* window = nullptr;
    QObject* view = nullptr;
    QString error;

    bool create() {
        engine.addImageProvider("icon", new TestIcons);
        engine.rootContext()->setContextProperty("music", &music);
        engine.rootContext()->setContextProperty("appearanceState", &appearance);
        QQmlComponent component(&engine);
        // A test-only override lets the same getter-count regression execute
        // against a saved pre-change component without replacing source files.
        const QString source = qEnvironmentVariable("BOTTLE_LYRIC_TEST_SOURCE");
        if (source.isEmpty()) {
            component.setData(R"(
            import QtQuick
            import BottleMusic
            Window {
                width: 640; height: 480; visible: true
                LyricView { anchors.fill: parent }
            }
        )", QUrl("qrc:/lyric-binding-test.qml"));
        } else {
            if (!QFileInfo::exists(source)) { error = "Missing lyric test source: " + source; return false; }
            engine.rootContext()->setContextProperty("lyricTestSource", QUrl::fromLocalFile(QFileInfo(source).absoluteFilePath()));
            component.setData(R"(
            import QtQuick
            Window {
                width: 640; height: 480; visible: true
                Loader { anchors.fill: parent; source: lyricTestSource }
            }
        )", QUrl("qrc:/lyric-binding-test.qml"));
        }
        object.reset(component.create());
        error = component.errorString();
        window = qobject_cast<QQuickWindow*>(object.get());
        view = find("lyricView");
        if (window) window->requestActivate();
        return window && view;
    }
    QObject* find(const QString& name) const { return window ? findItem(window->contentItem(), name) : nullptr; }
    QObject* fill(int index) const { return find("lyricFill_" + QString::number(index)); }
};
}

class LyricBindingsTests final : public QObject {
    Q_OBJECT
private slots:
    void clockDoesNotRereadWholeLyricProjection_data() {
        QTest::addColumn<int>("cueCount");
        QTest::newRow("three-cues") << 3;
        QTest::newRow("six-hundred-cues") << 600;
    }
    void clockDoesNotRereadWholeLyricProjection() {
        QFETCH(int, cueCount);
        LyricScene scene;
        scene.music.rows.reserve(cueCount);
        for (int index = 0; index < cueCount; ++index)
            scene.music.rows.append(cue(1000 + index * 4000, "line " + QString::number(index)));
        QVERIFY2(scene.create(), qPrintable(scene.error));
        QTRY_VERIFY(scene.view->property("hasLyrics").toBool());
        QCoreApplication::processEvents();
        const int reads = scene.music.reads;
        for (int tick = 0; tick <= 40; ++tick) {
            scene.music.position(1000 + tick * 50);
            scene.view->setProperty("displayPosition", 1000 + tick * 50 + 20);
            QCoreApplication::processEvents();
        }
        // This slot deliberately uses object names present in the old view,
        // so the pre-change negative run fails on redundant getter reads.
        QCOMPARE(scene.music.reads, reads);
        // Qt reads each item while slice() detaches its property reference
        // sequence. Those reads are allowed only when content changes.
        qInfo() << "lyric cues:" << cueCount << "snapshot getter reads:" << reads << "clock updates:" << 41;
        QVERIFY(reads > 0);
        QVERIFY2(reads <= cueCount + 2, qPrintable(QString("Snapshot getter reads %1 exceed cue count %2 + 2").arg(reads).arg(cueCount)));
    }
    void clockAndSeekUseCachedCueBoundaries() {
        LyricScene scene;
        scene.music.rows = {cue(1000, "first line", "第一行"), cue(5000, "second row", "第二行"), cue(9000, "last line", "最后一行")};
        QVERIFY2(scene.create(), qPrintable(scene.error));
        QTRY_VERIFY(scene.fill(0));
        QTRY_VERIFY(scene.fill(1));
        const int reads = scene.music.reads;
        scene.music.position(3000);
        QTRY_VERIFY(qAbs(scene.fill(0)->property("fillCursor").toDouble() - 5.0) < 0.01);
        auto* translation = scene.find("activeLyricTranslation");
        QVERIFY(translation);
        QCOMPARE(translation->property("text").toString(), QStringLiteral("第一行"));

        scene.music.index(2);
        scene.music.position(13000);
        QTRY_VERIFY(scene.fill(2));
        // Last cue uses decoded duration when it extends beyond the default 4 s.
        QTRY_VERIFY(qAbs(scene.fill(2)->property("fillCursor").toDouble() - 4.5) < 0.01);
        scene.music.duration_ = 0; emit scene.music.durationChanged();
        scene.music.position(11000);
        QTRY_VERIFY(qAbs(scene.fill(2)->property("fillCursor").toDouble() - 4.5) < 0.01);
        QCOMPARE(scene.music.reads, reads);

        QSignalSpy seeks(&scene.music, &LyricMusicFixture::seekRequested);
        auto* line = qobject_cast<QQuickItem*>(scene.find("lyricLine_2"));
        QVERIFY(line);
        QTest::qWait(30);
        const QPoint click = line->mapToScene(QPointF(line->width() / 2, line->height() / 2)).toPoint();
        QTest::mouseClick(scene.window, Qt::LeftButton, Qt::NoModifier, click);
        QTRY_COMPARE(seeks.size(), 1);
        QCOMPARE(seeks.first().first().toLongLong(), 9000);

        scene.music.rows = {cue(0, "replacement", "替换")};
        emit scene.music.lyricsChanged();
        QTRY_VERIFY(scene.music.reads > reads);
        QVERIFY(scene.music.reads <= reads + 3);
        QVERIFY(scene.view->property("hasLyrics").toBool());
        const int replacementReads = scene.music.reads;
        scene.music.rows.clear(); emit scene.music.lyricsChanged();
        QTRY_VERIFY(!scene.view->property("hasLyrics").toBool());
        QVERIFY(scene.music.reads > replacementReads);
        QVERIFY(scene.music.reads <= replacementReads + 2);
    }
    void timedWordsAndClockGatesPreservePlaybackBehavior() {
        LyricScene scene;
        const QVariantList words{QVariantMap{{"time", 1000}, {"end", 2000}, {"start", 0}, {"length", 5}},
            QVariantMap{{"time", 2000}, {"end", -1}, {"start", 5}, {"length", 5}}};
        scene.music.rows = {cue(1000, "first line", {}, words), cue(5000, "second row")};
        QVERIFY2(scene.create(), qPrintable(scene.error));
        QTRY_VERIFY(scene.fill(0));
        const int reads = scene.music.reads;
        scene.music.position(3000);
        QTRY_VERIFY(qAbs(scene.fill(0)->property("fillCursor").toDouble() - (5.0 + 5.0 / 3.0)) < 0.01);
        auto* timer = scene.find("lyricFillClock");
        QVERIFY(timer);
        QVERIFY(!timer->property("running").toBool());
        QTRY_VERIFY(scene.window->isActive());
        scene.music.playing_ = true; emit scene.music.playbackChanged();
        QTRY_VERIFY(timer->property("running").toBool());
        const auto sampled = scene.view->property("displayPosition").toDouble();
        QTRY_VERIFY(scene.view->property("displayPosition").toDouble() > sampled);
        QCOMPARE(scene.music.reads, reads);
        scene.music.reducedMotion_ = true; emit scene.music.appearanceChanged();
        QTRY_VERIFY(!timer->property("running").toBool());
        scene.music.reducedMotion_ = false; emit scene.music.appearanceChanged();
        QTRY_VERIFY(timer->property("running").toBool());
        scene.view->setProperty("visible", false);
        QTRY_VERIFY(!timer->property("running").toBool());
        scene.view->setProperty("visible", true);
        QTRY_VERIFY(timer->property("running").toBool());
        scene.music.loading_ = true; emit scene.music.playbackChanged();
        QTRY_VERIFY(!timer->property("running").toBool());
        scene.music.loading_ = false; scene.music.playing_ = false; emit scene.music.playbackChanged();
        QTRY_VERIFY(!timer->property("running").toBool());
        QCOMPARE(scene.music.reads, reads);
    }
};
QTEST_MAIN(LyricBindingsTests)
#include "tst_lyric_bindings.moc"
