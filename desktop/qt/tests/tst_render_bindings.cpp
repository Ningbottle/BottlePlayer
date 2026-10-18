#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>
#include <memory>

class RenderMusicFixture final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool reducedMotion READ reducedMotion CONSTANT)
public:
    bool reducedMotion() const { return true; }
};

class RenderAppearanceFixture final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int sidebarWidth READ sidebarWidth CONSTANT)
public:
    int sidebarWidth() const { return 232; }
};

#ifndef Q_MOC_RUN
#include "render_binding_fixtures.h"
#endif

class RenderBindingsTests final : public QObject {
    Q_OBJECT
private slots:
    void coverRequestsDevicePixelBox_data() {
        QTest::addColumn<qreal>("width");
        QTest::addColumn<qreal>("height");
        QTest::addColumn<qreal>("dpr");
        QTest::addColumn<int>("pixels");
        QTest::addColumn<int>("bucket");
        QTest::newRow("smallest-service-bucket") << qreal(10) << qreal(20) << qreal(1) << 0 << 80;
        QTest::newRow("exact-device-boundary") << qreal(320) << qreal(100) << qreal(1.25) << 80 << 400;
        QTest::newRow("fractional-device-boundary") << qreal(320.01) << qreal(100) << qreal(1.25) << 80 << 512;
        QTest::newRow("portrait-device-box") << qreal(100) << qreal(352) << qreal(1.25) << 80 << 512;
        QTest::newRow("landscape-device-box") << qreal(352) << qreal(100) << qreal(1.25) << 80 << 512;
        QTest::newRow("minimum-request-wins") << qreal(40) << qreal(60) << qreal(2) << 300 << 300;
        QTest::newRow("minimum-rounds-up") << qreal(40) << qreal(60) << qreal(1) << 121 << 200;
        QTest::newRow("small-bucket-rounds-up") << qreal(81) << qreal(40) << qreal(1) << 0 << 100;
        QTest::newRow("high-density-box") << qreal(300) << qreal(200) << qreal(2) << 400 << 800;
        QTest::newRow("device-box-ceiling") << qreal(900) << qreal(1500) << qreal(1.5) << 80 << 1000;
        QTest::newRow("minimum-request-ceiling") << qreal(10) << qreal(20) << qreal(1) << 1201 << 1000;
    }

    void coverRequestsDevicePixelBox() {
        QFETCH(qreal, width);
        QFETCH(qreal, height);
        QFETCH(qreal, dpr);
        QFETCH(int, pixels);
        QFETCH(int, bucket);
        RenderScene scene;
        QVERIFY2(scene.create(coverScene), qPrintable(scene.error));
        auto* cover = qobject_cast<QQuickItem*>(scene.find("cover"));
        QVERIFY(cover);
        QVERIFY(scene.object->setProperty("testDpr", dpr));
        cover->setWidth(width);
        cover->setHeight(height);
        QVERIFY(cover->setProperty("pixels", pixels));
        QTRY_COMPARE(cover->property("bucket").toInt(), bucket);
        auto* image = artworkImage(cover);
        QVERIFY(image);
        QTRY_COMPARE(image->property("source").toUrl(), QUrl(QString("image://artwork/stdmusic/%1/fixture").arg(bucket)));
        QCOMPARE(image->property("sourceSize").toSize(), QSize(bucket, bucket));
        QTRY_VERIFY(cover->property("ready").toBool());
    }

    void coverReactsToGeometryDensityAndMinimum() {
        RenderScene scene;
        QVERIFY2(scene.create(coverScene), qPrintable(scene.error));
        auto* cover = qobject_cast<QQuickItem*>(scene.find("cover"));
        QVERIFY(cover);
        QTRY_COMPARE(cover->property("bucket").toInt(), 100);
        cover->setHeight(352);
        QTRY_COMPARE(cover->property("bucket").toInt(), 400);
        QVERIFY(scene.object->setProperty("testDpr", 1.25));
        QTRY_COMPARE(cover->property("bucket").toInt(), 512);
        QVERIFY(cover->setProperty("pixels", 800));
        QTRY_COMPARE(cover->property("bucket").toInt(), 800);
        cover->setHeight(40);
        QVERIFY(cover->setProperty("pixels", 80));
        QTRY_COMPARE(cover->property("bucket").toInt(), 200);
        auto* image = artworkImage(cover);
        QVERIFY(image);
        QTRY_COMPARE(image->property("sourceSize").toSize(), QSize(200, 200));
    }

    void scrollFadeTracksListViewEdgesAfterOriginMoves() {
        RenderScene scene;
        QVERIFY2(scene.create(scrollScene), qPrintable(scene.error));
        auto* list = scene.find("list");
        auto* fade = scene.find("fade");
        auto* top = scene.find("scrollFadeTop");
        auto* bottom = scene.find("scrollFadeBottom");
        QVERIFY(list && fade && top && bottom);
        const auto tracePosition = [list](const char* step) {
            qInfo() << step << "contentY" << list->property("contentY") << "originY" << list->property("originY")
                    << "height" << list->property("height") << "contentHeight" << list->property("contentHeight")
                    << "topMargin" << list->property("topMargin") << "bottomMargin" << list->property("bottomMargin")
                    << "atYBeginning" << list->property("atYBeginning") << "atYEnd" << list->property("atYEnd");
        };
        QTRY_COMPARE(list->property("count").toInt(), 40);
        QTRY_VERIFY(fade->property("overflows").toBool());
        QVERIFY(QMetaObject::invokeMethod(scene.object.get(), "shiftOrigin"));
        QTRY_VERIFY(qAbs(list->property("originY").toDouble()) > 1);
        qInfo() << "ListView origin after deleting rows above the viewport:" << list->property("originY");

        QVERIFY(QMetaObject::invokeMethod(scene.object.get(), "begin"));
        tracePosition("begin-immediate");
        QTest::qWait(100);
        tracePosition("begin-settled");
        QTRY_VERIFY(list->property("atYBeginning").toBool());
        QTRY_COMPARE(top->property("opacity").toDouble(), 0.0);
        QTRY_COMPARE(bottom->property("opacity").toDouble(), 1.0);

        QVERIFY(QMetaObject::invokeMethod(scene.object.get(), "end"));
        tracePosition("end-immediate");
        QTRY_VERIFY(list->property("atYEnd").toBool());
        QTRY_COMPARE(top->property("opacity").toDouble(), 1.0);
        QTRY_COMPARE(bottom->property("opacity").toDouble(), 0.0);

        QVERIFY(QMetaObject::invokeMethod(scene.object.get(), "leaveEnd"));
        QTRY_VERIFY(!list->property("atYEnd").toBool());
        QTRY_COMPARE(bottom->property("opacity").toDouble(), 1.0);

        QVERIFY(QMetaObject::invokeMethod(scene.object.get(), "shrink"));
        QTRY_COMPARE(list->property("count").toInt(), 1);
        QTRY_VERIFY(!fade->property("overflows").toBool());
        QVERIFY(!top->property("visible").toBool());
        QVERIFY(!bottom->property("visible").toBool());
    }
};

QTEST_MAIN(RenderBindingsTests)
#include "tst_render_bindings.moc"
