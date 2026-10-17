#include "PixelSnap.h"
#include <QQuickWindow>
#include <QTransform>
#include <cmath>

// Translate (and, for fit, scale) appended after the item's own QML
// transforms, so it acts in parent coordinates right next to x/y.
class PixelSnapShift final : public QQuickTransform {
public:
    explicit PixelSnapShift(QObject* parent) : QQuickTransform(parent) {}
    void applyTo(QMatrix4x4* matrix) const override {
        if (!offset_.isNull()) matrix->translate(float(offset_.x()), float(offset_.y()));
        if (scale_ != QPointF(1, 1)) matrix->scale(float(scale_.x()), float(scale_.y()));
    }
    void set(QPointF offset, QPointF scale) {
        if (offset == offset_ && scale == scale_) return;
        offset_ = offset;
        scale_ = scale;
        update();
    }

private:
    QPointF offset_;
    QPointF scale_{1, 1};
};

PixelSnap::PixelSnap(QObject* parent) : QObject(parent), item_(qobject_cast<QQuickItem*>(parent)) {
    if (!item_) qWarning("PixelSnap must be attached to an Item");
}

PixelSnap* PixelSnap::qmlAttachedProperties(QObject* object) {
    return new PixelSnap(object);
}

void PixelSnap::setEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    refresh();
    emit enabledChanged();
}

void PixelSnap::setFit(bool fit) {
    if (fit_ == fit) return;
    fit_ = fit;
    refresh();
    emit fitChanged();
}

void PixelSnap::refresh() {
    if (!item_) return;
    if (active()) connect(item_, &QQuickItem::windowChanged, this, &PixelSnap::hookWindow, Qt::UniqueConnection);
    if (shift_ && !active()) shift_->set({}, {1, 1});
    hookWindow(item_->window());
}

void PixelSnap::hookWindow(QQuickWindow* window) {
    disconnect(frame_);
    if (!active() || !window) return;
    // afterAnimating runs on the GUI thread once layouts are polished and
    // before the scene graph syncs, so the shift lands in the same frame.
    frame_ = connect(window, &QQuickWindow::afterAnimating, this, &PixelSnap::snap);
    window->update();
}

void PixelSnap::snap() {
    if (!active() || !item_ || !item_->isVisible()) return;
    QQuickWindow* window = item_->window();
    const QQuickItem* parent = item_->parentItem();
    if (!window || !parent) return;
    // Appended on the first frame, after QML has populated `transform`.
    if (!shift_) {
        shift_ = new PixelSnapShift(this);
        shift_->appendToItem(item_);
    }
    const qreal dpr = window->effectiveDevicePixelRatio();
    const auto grid = [dpr](QPointF p) { return QPointF(std::round(p.x() * dpr) / dpr, std::round(p.y() * dpr) / dpr); };
    // Work from the layout position, not the shifted one, so the result is
    // a pure function of layout and cannot oscillate between frames.
    const QTransform toScene = parent->itemTransform(nullptr, nullptr);
    const QPointF near = toScene.map(item_->position());
    const QPointF nearSnapped = grid(near);
    const QTransform linear(toScene.m11(), toScene.m12(), toScene.m21(), toScene.m22(), 0, 0);
    bool invertible = false;
    const QTransform toParent = linear.inverted(&invertible);
    if (!invertible) return;
    QPointF scale(1, 1);
    if (fit_) {
        const QPointF far = toScene.map(item_->position() + QPointF(item_->width(), item_->height()));
        const QPointF farSnapped = grid(far);
        if (std::abs(far.x() - near.x()) > 1e-6) scale.setX((farSnapped.x() - nearSnapped.x()) / (far.x() - near.x()));
        if (std::abs(far.y() - near.y()) > 1e-6) scale.setY((farSnapped.y() - nearSnapped.y()) / (far.y() - near.y()));
    }
    shift_->set(toParent.map(nearSnapped - near), scale);
}
