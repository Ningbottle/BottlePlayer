#pragma once
#include <QStringList>

// Shared by the picker, playback preferences and data import. Old profiles
// used "hires" for the wire token "high"; keep that alias at the input boundary.
namespace PlaybackQuality {
inline const QStringList& catalog() {
    static const QStringList values{QStringLiteral("128"), QStringLiteral("320"),
        QStringLiteral("flac"), QStringLiteral("high"), QStringLiteral("master")};
    return values;
}
inline QString canonical(QString value) {
    if (value == QLatin1String("hires")) value = QStringLiteral("high");
    return catalog().contains(value) ? value : QString{};
}
}
