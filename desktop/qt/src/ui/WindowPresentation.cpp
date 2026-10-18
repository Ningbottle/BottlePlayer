#include "WindowPresentation.h"
#include <QQuickWindow>

void configureWindowPresentation() {
#ifdef Q_OS_WIN
    // Qt 6.8's opaque D3D11 HWND swapchain uses DXGI_SCALING_NONE: a growing
    // window exposes black strips until the render thread presents its new
    // size. Requesting an alpha buffer selects Qt's DirectComposition path,
    // which retains and stretches the last frame during that interval.
    // Main.qml still clears to an opaque Theme.bg. GPU rendering and the
    // threaded render loop remain enabled; no private graphics API is used.
    // ClearType text composites correctly on this alpha surface (measured
    // pixel-identical to an opaque swapchain), so no opaque-mode trade is
    // needed.
    QQuickWindow::setDefaultAlphaBuffer(true);
#endif
}
