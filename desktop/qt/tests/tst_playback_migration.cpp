#include "playback/PlaySession.h"
#include "stats/StatsController.h"
#include "library/HistoryController.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QLibrary>
#include <QJsonDocument>
#include <QSignalSpy>

class PlaybackMigrationTests : public QObject {
    Q_OBJECT
    Track track() { Track value; value.hash = QString(32, 'a'); value.title = "Test"; value.artist = "Artist"; return value; }
    static void listen(PlaySession& session, qint64 from, int seconds) {
        for (int i = 1; i <= seconds * 2; ++i) session.position(from + i * 500, 120000);
    }
private slots:
    void seeksPausesAndStallsDoNotCount() {
        PlaySession session; session.intend(track()); session.setPlaying(true, 0);
        listen(session, 0, 30); session.seek(); session.position(80000, 120000); listen(session, 80000, 31);
        session.setPlaying(false, 111000); session.position(115000, 120000);
        QCOMPARE(session.listenedMilliseconds(), 61000);
        const auto record = session.finish(false, "320");
        QCOMPARE(record.value("listened_seconds").toInt(), 61);
        QCOMPARE(record.value("duration_seconds").toInt(), 120);
        QCOMPARE(record.value("quality").toString(), QString("320"));
        QVERIFY(!record.value("completed").toBool()); QVERIFY(session.finish(true, "320").isEmpty());
    }
    void failedAndShortPlaysProduceNoRecord() {
        PlaySession session; session.intend(track()); QVERIFY(session.finish(false, "128").isEmpty());
        session.intend(track()); session.setPlaying(true, 0); listen(session, 0, 60);
        QVERIFY(session.finish(true, "128").isEmpty());
    }
    void repeatedListenGetsNewSession() {
        PlaySession session; session.intend(track()); session.setPlaying(true, 0); listen(session, 0, 61);
        const auto first = session.finish(true, "128"); QVERIFY(first.value("completed").toBool());
        session.intend(track()); session.setPlaying(true, 0); listen(session, 0, 61);
        const auto second = session.finish(true, "128");
        QVERIFY(first.value("session_id").toString() != second.value("session_id").toString());
    }
    void nativeStatsAndHistoryPersistence() {
        QTemporaryDir profile(qEnvironmentVariable("BOTTLE_TEST_TEMP_ROOT", QDir::tempPath()) + "/bottle-playback-XXXXXX"); QVERIFY(profile.isValid());
        const auto path = qEnvironmentVariable("BOTTLE_ACCOUNT_TEST_DLL"); QVERIFY(!path.isEmpty());
        QLibrary fixture(path); fixture.setLoadHints(QLibrary::PreventUnloadHint); QVERIFY(fixture.load());
        const auto configure = reinterpret_cast<void(*)(const char*)>(fixture.resolve("EchoTestConfigure")); QVERIFY(configure);
        configure("{\"logged\":false}");
        NativeGateway gateway(path, profile.path()); bool ready = false;
        gateway.initialize([&ready](const QJsonObject& response) { ready = response.value("status").toInt() == 1; }); QTRY_VERIFY(ready);
        StatsController stats(gateway); stats.setActive(true); QTRY_VERIFY(!stats.loading());
        QCOMPARE(stats.summary().value("unique_songs").toInt(), 3); QCOMPARE(stats.timeline().size(), 3); QCOMPARE(stats.songs().size(), 1);
        stats.setRange("7d"); QTRY_VERIFY(!stats.loading()); QCOMPARE(stats.range(), QString("7d")); stats.setRange("bad"); QCOMPARE(stats.range(), QString("7d"));
        QSignalSpy committed(&stats, &StatsController::recordCommitted);
        PlaySession session; session.intend(track()); session.setPlaying(true, 0); listen(session, 0, 61);
        const auto record = session.finish(false, "128"); stats.record(record); QTRY_COMPARE(committed.count(), 1);
        stats.record(record); QTRY_COMPARE(committed.count(), 2); // Native idempotency acknowledgement, one stored record.
        stats.refresh(); QTRY_VERIFY(!stats.loading()); QCOMPARE(stats.summary().value("total_plays").toInt(), 1);
        HistoryController history(gateway, profile.path()); history.started(track().toVariant()); history.started(track().toVariant()); QCOMPARE(history.tracks().size(), 1);
        HistoryController restored(gateway, profile.path()); QCOMPARE(restored.tracks().size(), 1); QCOMPARE(restored.tracks().first().toMap().value("title").toString(), QString("Test"));
        restored.setScope("week"); QCOMPARE(restored.tracks().size(),1); restored.setScope("bad"); QCOMPARE(restored.scope(),QString("week")); restored.setScope("month"); QCOMPARE(restored.tracks().size(),1);
        configure("{\"stats_degraded\":true}"); stats.refresh(); QTRY_VERIFY(!stats.loading()); QVERIFY(!stats.error().isEmpty());
        stats.setActive(false);
    }
};
QTEST_GUILESS_MAIN(PlaybackMigrationTests)
#include "tst_playback_migration.moc"
