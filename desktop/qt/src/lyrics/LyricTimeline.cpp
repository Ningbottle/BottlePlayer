#include "LyricTimeline.h"
#include <QRegularExpression>
#include <algorithm>
namespace {
qint64 stampTime(const QRegularExpressionMatch& match, int begin) {
    return match.captured(begin).toLongLong() * 60000 + match.captured(begin + 1).toLongLong() * 1000
        + match.captured(begin + 2).leftJustified(3, '0').left(3).toInt();
}
QVector<LyricWord> wordTimes(const QString& body, qint64 lineTime, qint64 offset) {
    static const QRegularExpression tag(R"(<(/?)(\d{1,3}):(\d{1,2})(?:[.:](\d{1,3}))?>)");
    if (!body.contains(tag)) return {};
    QVector<LyricWord> words;
    auto matches = tag.globalMatch(body);
    int cursor = 0, plainCursor = 0;
    qint64 previous = lineTime;
    QString plain = body; plain.remove(tag);
    int leadingSpace = 0; while (leadingSpace < plain.size() && plain[leadingSpace].isSpace()) ++leadingSpace;
    const auto append = [&](const QString& segment, qint64 end) {
        int first = 0, last = int(segment.size());
        while (first < last && segment[first].isSpace()) ++first;
        while (last > first && segment[last - 1].isSpace()) --last;
        if (last > first) words.append({std::max<qint64>(0, previous), end > previous ? std::max<qint64>(0, end) : -1,
            plainCursor + first - leadingSpace, last - first});
        plainCursor += int(segment.size());
    };
    while (matches.hasNext()) {
        const auto match = matches.next(); const auto time = stampTime(match, 2) + offset;
        append(body.mid(cursor, match.capturedStart() - cursor), match.captured(1) == "/" ? time : -1);
        previous = time; cursor = int(match.capturedEnd());
    }
    append(body.mid(cursor), -1);
    for (int i = 0; i + 1 < words.size(); ++i)
        if (words[i].end < 0 && words[i + 1].time > words[i].time) words[i].end = words[i + 1].time;
    return words;
}
}
QVector<LyricCue> LyricTimeline::parse(const QString& source) {
    QString text = source;
    text.replace("\r\n", "\n"); text.replace('\r', '\n'); text.remove(QChar(0xfeff));
    const QRegularExpression stamp(R"(\[(\d{1,3}):(\d{1,2})(?:[.:](\d{1,3}))?\])");
    if (!text.contains(stamp)) text = QString::fromUtf8(QByteArray::fromBase64(source.toUtf8()));
    const auto offsetTag = QRegularExpression(R"(\[offset:\s*([+-]?\d+)\s*\])", QRegularExpression::CaseInsensitiveOption).match(text);
    const qint64 offset = offsetTag.hasMatch() ? offsetTag.captured(1).toLongLong() : 0;
    QVector<LyricCue> cues;
    for (const auto& line : text.split('\n')) {
        auto body = line;
        body.remove(stamp);
        body.remove(QRegularExpression(R"(\{[^}]*\}|\[offset:[^\]]*\])"));
        QString display = body; display.remove(QRegularExpression(R"(<\/?\d{1,3}:\d{1,2}(?:[.:]\d{1,3})?>)")); display = display.trimmed();
        if (display.isEmpty() || QRegularExpression(R"(^[/\-–—·.\s]+$)").match(display).hasMatch()) continue;
        auto matches = stamp.globalMatch(line);
        while (matches.hasNext()) {
            const auto m = matches.next();
            const auto time = std::max<qint64>(0, stampTime(m, 1) + offset);
            cues.append({time, display, {}, wordTimes(body, time, offset)});
        }
    }
    std::stable_sort(cues.begin(), cues.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    return cues;
}
QVector<LyricCue> LyricTimeline::parseBilingual(const QString& source) {
    QVector<LyricCue> result;
    for (const auto& cue : parse(source)) {
        if (result.isEmpty() || result.last().time != cue.time) {
            result.append(cue);
        } else if (result.last().text != cue.text) {
            auto& translation = result.last().translation;
            if (!translation.split('\n').contains(cue.text)) {
                if (!translation.isEmpty()) translation += '\n';
                translation += cue.text;
            }
        }
    }
    return result;
}
int LyricTimeline::indexAt(const QVector<LyricCue>& cues, qint64 position) {
    const auto upper = std::upper_bound(cues.cbegin(), cues.cend(), position,
        [](qint64 time, const LyricCue& cue) { return time < cue.time; });
    return static_cast<int>(upper - cues.cbegin()) - 1;
}
