#pragma once
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

class PixelSnapShift;

// Chromium (zoom-for-DSF) lays boxes out on fractional device pixels but
// paints each one with its edges rounded to whole device pixels, so a 1px
// border is one crisp row and a 16px icon is never resampled. Qt Quick draws
// an item exactly where layout leaves it: at 125% a rounded 1px frame or an
// icon at a fractional position is antialiased or filtered across two rows.
//
// `PixelSnap.enabled: true` shifts the item (and its children) so its origin
// lands on the device-pixel grid; `PixelSnap.fit: true` (implies enabled)
// also stretches it so the far edges land there too, like Blink's snapped
// paint rect. Use fit on childless backgrounds: the stretch is under a pixel
// but would scale children. Both are recomputed before every frame from the
// item's layout position and ignore the item's own `transform` list.
class PixelSnap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("PixelSnap is an attached property")
    QML_ATTACHED(PixelSnap)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool fit READ fit WRITE setFit NOTIFY fitChanged FINAL)

public:
    explicit PixelSnap(QObject* parent = nullptr);
    static PixelSnap* qmlAttachedProperties(QObject* object);

    bool enabled() const { return enabled_; }
    void setEnabled(bool enabled);
    bool fit() const { return fit_; }
    void setFit(bool fit);

signals:
    void enabledChanged();
    void fitChanged();

private:
    bool active() const { return enabled_ || fit_; }
    void refresh();
    void hookWindow(QQuickWindow* window);
    void snap();

    QPointer<QQuickItem> item_;
    PixelSnapShift* shift_ = nullptr;
    QMetaObject::Connection frame_;
    bool enabled_ = false;
    bool fit_ = false;
};
