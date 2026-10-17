#include <QtTest>
#include "lyrics/LyricTimeline.h"
#include "music/Track.h"

class Contracts final : public QObject {
    Q_OBJECT
private slots:
    void providerAliasesAndUnits() {
        const auto track = Track::fromJson({{"hash", "abcd"}, {"filename", "歌手 - 标题"}, {"timelen", 215000},
            {"album_sizable_cover", "https://img.example/{size}/cover.jpg"}, {"mixsongid", 12345}});
        QCOMPARE(track.artist, QStringLiteral("歌手")); QCOMPARE(track.title, QStringLiteral("标题"));
        QCOMPARE(track.duration, 215); QCOMPARE(track.audioId, QString("12345"));
        QVERIFY(track.image.contains("/400/"));
    }
    void duplicateQueueOccurrencesSurvive() {
        const QJsonObject song{{"hash", "same"}, {"songname", "Song"}};
        const auto tracks = parseTracks({{"status", 1}, {"data", QJsonObject{{"data", QJsonObject{{"info", QJsonArray{song, song}}}}}}});
        QCOMPARE(tracks.size(), 2); QCOMPARE(tracks[0].hash, tracks[1].hash);
    }
    void businessFailuresDoNotBecomeMusic() {
        const auto tracks = parseTracks({{"status", 0}, {"data", QJsonArray{QJsonObject{{"hash", "invalid"}}}}});
        QVERIFY(tracks.isEmpty());
    }
    void nativeSongUrlMetadataKeepsArtistAndTitle() {
        const auto track = Track::fromJson({{"hash", "abc"}, {"songName", "Title"}, {"singerName", "Artist"}, {"timeLength", 294}, {"FileID", "77"}});
        QCOMPARE(track.title, QString("Title")); QCOMPARE(track.artist, QString("Artist"));
        QCOMPARE(track.duration, 294); QCOMPARE(track.fileId, QString("77"));
        const auto underscored = Track::fromJson({{"hash", "abc"}, {"file_name", "Artist - Title"}, {"singer_name", "Artist"}});
        QCOMPARE(underscored.title, QString("Title")); QCOMPARE(underscored.artist, QString("Artist"));
    }
    void lyricClockSeeksAndOffset() {
        const auto cues = LyricTimeline::parse(QStringLiteral("[offset:250]\n[00:01.2]第一行\n[00:03:045]第二行\n[00:05.000]第三行"));
        QCOMPARE(cues.size(), 3); QCOMPARE(cues[0].time, 1450); QCOMPARE(cues[1].time, 3295);
        QCOMPARE(LyricTimeline::indexAt(cues, 1449), -1);
        QCOMPARE(LyricTimeline::indexAt(cues, 1450), 0);
        QCOMPARE(LyricTimeline::indexAt(cues, 6000), 2);
        QCOMPARE(LyricTimeline::indexAt(cues, 2000), 0); // Backward seeks use actual position, not an advancing timer.
    }
    void base64AndMultipleTimestamps() {
        const auto raw = QByteArray("[00:01.00][00:04.00]repeat").toBase64();
        const auto cues = LyricTimeline::parse(QString::fromLatin1(raw));
        QCOMPARE(cues.size(), 2); QCOMPARE(cues[1].time, 4000);
    }
    void bilingualLrcKeepsTranslationAndClock() {
        const auto cues = LyricTimeline::parseBilingual(QStringLiteral(
            "[offset:-100]\n[00:01.000]A quiet morning\n[00:01.000]安静的清晨\n"
            "[00:01.000]安静的清晨\n[00:04.000]Walk beside me"));
        QCOMPARE(cues.size(), 2);
        QCOMPARE(cues[0].time, 900);
        QCOMPARE(cues[0].text, QStringLiteral("A quiet morning"));
        QCOMPARE(cues[0].translation, QStringLiteral("安静的清晨"));
        QVERIFY(cues[1].translation.isEmpty());
        QCOMPARE(LyricTimeline::indexAt(cues, 3899), 0);
        QCOMPARE(LyricTimeline::indexAt(cues, 3900), 1);
        QCOMPARE(LyricTimeline::indexAt(cues, 950), 0);
    }
    void enhancedWordTimesKeepOffsetsAndClosingTags() {
        const auto cues = LyricTimeline::parseBilingual(QStringLiteral("[offset:250]\n[00:01] <00:01.0>你好 </00:02.0><00:02.5>世界</00:03.0>\n[00:01]Hello world\n[00:04]普通歌词"));
        QCOMPARE(cues.size(), 2); QCOMPARE(cues[0].text, QStringLiteral("你好 世界"));
        QCOMPARE(cues[0].words.size(), 2); QCOMPARE(cues[0].words[0].time, 1250);
        QCOMPARE(cues[0].words[0].end, 2250); QCOMPARE(cues[0].words[0].start, 0); QCOMPARE(cues[0].words[0].length, 2);
        QCOMPARE(cues[0].words[1].time, 2750); QCOMPARE(cues[0].words[1].end, 3250); QCOMPARE(cues[0].words[1].start, 3);
        QCOMPARE(cues[0].translation, QString("Hello world")); QVERIFY(cues[1].words.isEmpty());
    }
};
QTEST_GUILESS_MAIN(Contracts)
#include "tst_contracts.moc"
