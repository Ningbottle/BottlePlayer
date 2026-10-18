#pragma once

namespace {
class FixtureImages final : public QQuickImageProvider {
public:
    FixtureImages() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString&, QSize* size, const QSize& requestedSize) override {
        const QSize dimensions = requestedSize.isValid() ? requestedSize : QSize(16, 16);
        QImage image(dimensions, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::darkCyan);
        if (size) *size = dimensions;
        return image;
    }
};

QObject* findItem(QQuickItem* item, const QString& name) {
    if (!item) return nullptr;
    if (item->objectName() == name) return item;
    if (auto* object = item->findChild<QObject*>(name)) return object;
    for (auto* child : item->childItems())
        if (auto* object = findItem(child, name)) return object;
    return nullptr;
}

QObject* artworkImage(QQuickItem* item) {
    if (!item) return nullptr;
    if (item->property("source").toUrl().scheme() == QStringLiteral("image")
        && item->property("source").toUrl().host() == QStringLiteral("artwork"))
        return item;
    for (auto* child : item->childItems())
        if (auto* image = artworkImage(child)) return image;
    return nullptr;
}

struct RenderScene {
    RenderMusicFixture music;
    RenderAppearanceFixture appearance;
    QQmlEngine engine;
    std::unique_ptr<QObject> object;
    QQuickWindow* window = nullptr;
    QString error;

    bool create(const QByteArray& source) {
        engine.addImageProvider("icon", new FixtureImages);
        engine.addImageProvider("artwork", new FixtureImages);
        engine.rootContext()->setContextProperty("music", &music);
        engine.rootContext()->setContextProperty("appearanceState", &appearance);
        QQmlComponent component(&engine);
        component.setData(source, QUrl("qrc:/render-binding-test.qml"));
        object.reset(component.create());
        error = component.errorString();
        window = qobject_cast<QQuickWindow*>(object.get());
        return window;
    }

    QObject* find(const QString& name) const {
        return window ? findItem(window->contentItem(), name) : nullptr;
    }
};

const auto coverScene = R"(
    import QtQuick
    import BottleMusic
    Window {
        id: scene
        width: 640; height: 480; visible: true
        property real testDpr: 1
        Binding { target: Theme; property: "dpr"; value: scene.testDpr }
        Cover {
            objectName: "cover"
            width: 100; height: 100; pixels: 80
            url: "image://artwork/stdmusic/400/fixture"
            title: "Fixture"
        }
    }
)";

const auto scrollScene = R"(
    import QtQuick
    import BottleMusic
    Window {
        width: 320; height: 240; visible: true
        ListModel {
            id: rows
            Component.onCompleted: {
                for (let i = 0; i < 40; ++i) append({ label: "row " + i })
            }
        }
        ListView {
            id: view
            objectName: "list"
            width: 220; height: 120
            model: rows
            topMargin: 12; bottomMargin: 18
            cacheBuffer: 0
            delegate: Item { width: 220; height: 30 }
        }
        ScrollFade {
            objectName: "fade"
            width: view.width; height: view.height
            flickable: view
        }
        function shiftOrigin() {
            view.positionViewAtIndex(10, ListView.Beginning)
            view.forceLayout()
            rows.remove(0, 3)
            view.forceLayout()
        }
        // atYBeginning/atYEnd include the flickable margins, but
        // positionViewAtBeginning/End only align the first/last item edge.
        // Position the content at the real extents so the flags engage.
        function begin() { view.contentY = view.originY - view.topMargin; view.forceLayout() }
        function end() { view.contentY = view.originY + view.contentHeight - view.height + view.bottomMargin; view.forceLayout() }
        function leaveEnd() { view.contentY -= 20; view.forceLayout() }
        function shrink() { rows.remove(1, rows.count - 1); view.forceLayout(); view.positionViewAtBeginning() }
    }
)";
}
