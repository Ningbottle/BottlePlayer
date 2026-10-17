#pragma once
#include <QQuickImageProvider>

class QQmlEngine;

// image://icon/<name>/<rgb or argb hex>/<stroke width>[/fill]: library SVGs tinted and
// rasterised at the item's device-pixel size, so strokes stay crisp at 125%.
class IconProvider final : public QQuickImageProvider {
public:
    IconProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

// Fonts from <app dir>/fonts (bundled in assets/fonts), native text
// rendering, and the icon provider. Call before loading Main.qml.
void installUiAssets(QQmlEngine& engine);
