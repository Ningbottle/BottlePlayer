#pragma once
#include <QString>
#include <QVector>
struct LyricWord { qint64 time; qint64 end = -1; int start; int length; };
struct LyricCue { qint64 time; QString text; QString translation = {}; QVector<LyricWord> words = {}; };
class LyricTimeline {
public:
    static QVector<LyricCue> parse(const QString& source);
    // Common bilingual LRC stores original and translation at the same stamp.
    static QVector<LyricCue> parseBilingual(const QString& source);
    static int indexAt(const QVector<LyricCue>& cues, qint64 position);
};
