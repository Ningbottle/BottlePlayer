#pragma once
#include <QObject>
#include <QFont>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Shape once per cue/font/width, then let the active cue clip native text.
// Media-clock updates change rectangles only, never reflow the lyric list.
class LyricGeometry : public QObject {
    Q_OBJECT
    QML_ELEMENT
public:
    using QObject::QObject;
    Q_INVOKABLE QVariantList shape(const QString& text, const QFont& font, qreal width, qreal lineHeight) const;
};
