#include "music/MusicController.h"
#include "settings/DataMigration.h"
#include "settings/PlaybackStore.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QSemaphore>
#include <QScopeGuard>
#include <algorithm>
#include <atomic>
#include <stdexcept>

class PlaybackStorageTests final : public QObject {
    Q_OBJECT
    static QString root() {
        return qEnvironmentVariable("BOTTLE_TEST_TEMP_ROOT", QDir::tempPath()) + "/bottle-playback-storage-XXXXXX";
    }
    static QJsonObject track(int index) {
        return {{"hash", QString::number(index, 16).rightJustified(32, '0')},
            {"title", QStringLiteral("Queue measurement song %1").arg(index)},
            {"artist", "Fixture artist"}, {"album", "Fixture album"},
            {"image", "https://example.test/cover.jpg"}, {"duration", 180}};
    }
    static QJsonObject readJson(const QString& path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
    }
    static bool writeJson(const QString& path, const QJsonObject& object) {
        QFile file(path); const auto bytes = QJsonDocument(object).toJson();
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static QJsonObject queueSnapshot(const QJsonObject& preferences) {
        return QJsonDocument::fromJson(preferences.value("queueSnapshot").toString().toUtf8()).object();
    }
private slots:
    void legacyQualityUsesCanonicalToken_data() {
        QTest::addColumn<QString>("source");
        QTest::newRow("profile") << QString("profile");
        QTest::newRow("qt-export") << QString("qt-export");
        QTest::newRow("legacy-export") << QString("legacy-export");
    }
    void legacyQualityUsesCanonicalToken() {
        QFETCH(QString, source);
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        const auto preferences = profile.filePath("preferences.ini");
        if (source == "profile") {
            QSettings saved(preferences, QSettings::IniFormat);
            saved.setValue("quality", "hires"); saved.sync();
            QCOMPARE(saved.status(), QSettings::NoError);
        } else {
            const QJsonObject payload = source == "qt-export"
                ? QJsonObject{{"schema", "bottlemusic.qt-data-export"}, {"version", 1},
                    {"preferences", QJsonObject{{"quality", "hires"}}}}
                : QJsonObject{{"schema", "bottlemusic.user-data-export"}, {"version", 1},
                    {"localStorage", QJsonObject{{"player_quality", "hires"}}}};
            QVERIFY(writeJson(profile.filePath("input.json"), payload));
            DataMigration migration(profile.path());
            migration.importData(QUrl::fromLocalFile(profile.filePath("input.json")));
            QVERIFY(migration.restartNeeded());
        }
        MusicController music({}, profile.path());
        QCOMPARE(music.quality(), QString("high"));
        QVERIFY(music.flushStorage());
        QSettings saved(preferences, QSettings::IniFormat);
        QCOMPARE(saved.value("quality").toString(), QString("high"));
    }
    void blockedWriterCoalescesAndOwnsImmutableSnapshots() {
        QSemaphore entered, release;
        std::atomic_int writes = 0;
        const auto caller = std::this_thread::get_id();
        bool onWorker = false;
        QVector<PlaybackStore::Update> batches;
        PlaybackStore store({}, [&](const PlaybackStore::Update& update) {
            onWorker = std::this_thread::get_id() != caller;
            if (++writes == 1) { entered.release(); if (!release.tryAcquire(1, 10000)) return false; }
            batches.append(update); return true;
        });
        const auto unblock = qScopeGuard([&] { release.release(); });
        QVariantList rows{track(1).toVariantMap()};
        QVERIFY(store.saveQueue(rows, 0, 1000, false));
        QVERIFY(entered.tryAcquire(1, 5000));
        // All caller operations finish while I/O remains deliberately blocked.
        for (int i = 2; i <= 200; ++i) {
            rows[0] = track(i).toVariantMap();
            QVERIFY(store.saveQueue(rows, 0, i * 1000, true));
            QVERIFY(store.setValue("volume", i / 200.0));
        }
        QCOMPARE(writes.load(), 1);
        release.release(); QVERIFY(store.stop());
        QVERIFY(onWorker); QCOMPARE(writes.load(), 2); QCOMPARE(batches.size(), 2);
        QCOMPARE(batches[0].queue->tracks[0].toMap().value("hash"), track(1).value("hash").toVariant());
        QCOMPARE(batches[1].queue->tracks[0].toMap().value("hash"), track(200).value("hash").toVariant());
        QCOMPARE(batches[1].queue->position, 200000); QVERIFY(batches[1].queue->radio);
        QCOMPARE(batches[1].values.value("volume").toDouble(), 1.0);
        QVERIFY(store.flush());
        QVERIFY(!store.setValue("volume", .1)); QVERIFY(!store.saveQueue({}, -1, 0, false));
        QCOMPARE(writes.load(), 2);
    }
    void failedBatchRetainsKeysAndRetriesWithoutNewInput() {
        std::atomic_bool failing = true;
        QVariantMap saved;
        PlaybackStore store({}, [&](const PlaybackStore::Update& update) {
            if (failing.load()) throw std::runtime_error("fixture write failure");
            for (auto it = update.values.cbegin(); it != update.values.cend(); ++it) saved.insert(it.key(), it.value());
            return true;
        });
        QVERIFY(store.setValue("lyricSize", 40)); QVERIFY(!store.flush());
        failing = false; QVERIFY(store.flush());
        QCOMPARE(saved.value("lyricSize").toInt(), 40);
    }
    void failedInFlightBatchCannotReplaceNewerValues_data() {
        QTest::addColumn<bool>("queueFirst");
        QTest::newRow("queue") << true;
        QTest::newRow("preference") << false;
    }
    void failedInFlightBatchCannotReplaceNewerValues() {
        QFETCH(bool, queueFirst);
        QSemaphore entered, release;
        std::atomic_int calls = 0;
        PlaybackStore::Update saved;
        PlaybackStore store({}, [&](const PlaybackStore::Update& update) {
            if (++calls == 1) { entered.release(); release.tryAcquire(1, 10000); return false; }
            saved = update; return true;
        });
        const auto unblock = qScopeGuard([&] { release.release(); });
        if (queueFirst) QVERIFY(store.saveQueue({track(1).toVariantMap()}, 0, 1000, false));
        else QVERIFY(store.setValue("volume", .2));
        QVERIFY(entered.tryAcquire(1, 5000));
        QVERIFY(store.saveQueue({track(2).toVariantMap()}, 0, 2000, true));
        QVERIFY(store.setValue("volume", .8));
        release.release(); QVERIFY(store.flush());
        QCOMPARE(saved.queue->tracks[0].toMap().value("hash"), track(2).value("hash").toVariant());
        QCOMPARE(saved.queue->position, 2000); QVERIFY(saved.queue->radio);
        QCOMPARE(saved.values.value("volume").toDouble(), .8);
    }
    void saveFailureStopsExportAndImportThenRecovers() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        QVERIFY(QDir().mkdir(profile.filePath("preferences.ini")));
        MusicController music({}, profile.path()); music.setVolume(.75);
        QVERIFY(music.enqueueTrack(track(1).toVariantMap()));
        DataMigration migration(profile.path());
        migration.setPrepareSnapshot([&] { return music.flushStorage(); });
        connect(&migration, &DataMigration::importing, &music, &MusicController::holdPersistenceForImport);
        migration.exportData(QUrl::fromLocalFile(profile.filePath("blocked.json")));
        QVERIFY(!QFileInfo::exists(profile.filePath("blocked.json")));
        QVERIFY(migration.message().contains(QStringLiteral("未能保存")));
        const QJsonObject payload{{"schema", "bottlemusic.qt-data-export"}, {"version", 1},
            {"preferences", QJsonObject{{"volume", .2}}}};
        QVERIFY(writeJson(profile.filePath("input.json"), payload));
        QSignalSpy importing(&migration, &DataMigration::importing);
        migration.importData(QUrl::fromLocalFile(profile.filePath("input.json")));
        QCOMPARE(importing.count(), 0); QVERIFY(!migration.restartNeeded());
        QVERIFY(QDir(profile.path()).entryList({"import-backup-*.json"}, QDir::Files).isEmpty());
        QVERIFY(QDir().rmdir(profile.filePath("preferences.ini")));
        migration.exportData(QUrl::fromLocalFile(profile.filePath("recovered.json")));
        const auto saved = readJson(profile.filePath("recovered.json")).value("preferences").toObject();
        QCOMPARE(saved.value("volume").toDouble(), .75);
        QCOMPARE(queueSnapshot(saved).value("queue").toArray().size(), 1);
    }
    void snapshotsFlushLatestStateAndImportStopsOldWriter() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        {
            MusicController music({}, profile.path());
            music.setQuality("master"); music.setVolume(.6); music.setLyricSize(44);
            music.setVinyl(true); music.setReducedMotion(true); music.cycleLoopMode();
            music.playTracks({track(1).toVariantMap(), track(2).toVariantMap(), track(3).toVariantMap()}, 0);
            music.playQueue(1);
            DataMigration migration(profile.path());
            migration.setPrepareSnapshot([&] { return music.flushStorage(); });
            connect(&migration, &DataMigration::importing, &music, &MusicController::holdPersistenceForImport);
            migration.exportData(QUrl::fromLocalFile(profile.filePath("export.json")));
            const auto exported = readJson(profile.filePath("export.json"));
            const auto preferences = exported.value("preferences").toObject();
            QCOMPARE(preferences.value("quality").toString(), QString("master"));
            QCOMPARE(preferences.value("volume").toDouble(), .6);
            QCOMPARE(preferences.value("loopMode").toString(), QString("single"));
            QCOMPARE(preferences.value("lyricSize").toInt(), 44);
            QVERIFY(preferences.value("vinyl").toBool()); QVERIFY(preferences.value("reducedMotion").toBool());
            QCOMPARE(queueSnapshot(preferences).value("index").toInt(), 1);
            auto imported = exported;
            const QJsonObject snapshot{{"version", 1}, {"queue", QJsonArray{track(99)}},
                {"index", 0}, {"position", 8000}, {"radio", false}};
            imported.insert("preferences", QJsonObject{{"volume", .2}, {"quality", "high"},
                {"queueSnapshot", QString::fromUtf8(QJsonDocument(snapshot).toJson(QJsonDocument::Compact))}});
            QVERIFY(writeJson(profile.filePath("import.json"), imported));
            music.playQueue(2); music.setVolume(.85);
            migration.importData(QUrl::fromLocalFile(profile.filePath("import.json")));
            QVERIFY(migration.restartNeeded());
            const auto backups = QDir(profile.path()).entryList({"import-backup-*.json"}, QDir::Files);
            QCOMPARE(backups.size(), 1);
            const auto backup = readJson(profile.filePath(backups.first())).value("preferences").toObject();
            QCOMPARE(backup.value("volume").toDouble(), .85);
            QCOMPARE(queueSnapshot(backup).value("index").toInt(), 2);
            // A late producer and normal shutdown cannot replay pre-import state.
            music.setVolume(.99); music.clearQueue(); music.finishSession();
        }
        QSettings saved(profile.filePath("preferences.ini"), QSettings::IniFormat);
        QCOMPARE(saved.value("volume").toDouble(), .2); QCOMPARE(saved.value("quality").toString(), QString("high"));
        const auto snapshot = QJsonDocument::fromJson(saved.value("queueSnapshot").toByteArray()).object();
        QCOMPARE(snapshot.value("position").toInt(), 8000);
        QCOMPARE(snapshot.value("queue").toArray()[0].toObject().value("hash"), track(99).value("hash"));
        MusicController restored({}, profile.path());
        QCOMPARE(restored.queueIndex(), 0); QCOMPARE(restored.position(), 8000);
        QVERIFY(!restored.playing()); QVERIFY(restored.mediaPlayer()->source().isEmpty());
    }
    void destructionFlushesWithoutAnEventLoop() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        {
            MusicController music({}, profile.path());
            music.setVolume(.7); music.setLyricSize(38);
            music.playTracks({track(1).toVariantMap(), track(2).toVariantMap()}, 0);
            music.playQueue(1);
        }
        QSettings saved(profile.filePath("preferences.ini"), QSettings::IniFormat);
        QCOMPARE(saved.value("volume").toDouble(), .7); QCOMPARE(saved.value("lyricSize").toInt(), 38);
        const auto snapshot = QJsonDocument::fromJson(saved.value("queueSnapshot").toByteArray()).object();
        QCOMPARE(snapshot.value("index").toInt(), 1); QCOMPARE(snapshot.value("queue").toArray().size(), 2);
    }
    void selectionPersistence2000Tracks() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        QJsonArray rows;
        for (int i = 1; i <= 2000; ++i) rows.append(track(i));
        {
            QSettings initial(profile.filePath("preferences.ini"), QSettings::IniFormat);
            initial.setValue("queueSnapshot", QJsonDocument(QJsonObject{{"version", 1}, {"queue", rows},
                {"index", 0}, {"position", 12000}, {"radio", false}}).toJson(QJsonDocument::Compact));
            initial.sync(); QCOMPARE(initial.status(), QSettings::NoError);
        }
        MusicController music({}, profile.path());
        QCOMPARE(music.queue().size(), 2000);
        // No initialized gateway or media: isolate selection bookkeeping and
        // persistence from HTTP, decoder, output-device and audible playback.
        QVector<qint64> samples;
        QElapsedTimer total; total.start();
        for (int i = 1; i <= 80; ++i) {
            QElapsedTimer sample; sample.start();
            music.playQueue(i);
            samples.append(sample.nsecsElapsed());
        }
        const auto totalUs = total.nsecsElapsed() / 1000;
        std::sort(samples.begin(), samples.end());
        qInfo().nospace() << "2000-track / 80 selections: GUI total_us=" << totalUs
            << " median_us=" << samples[40] / 1000 << " p95_us=" << samples[75] / 1000
            << " max_us=" << samples.last() / 1000;
        QElapsedTimer barrier; barrier.start(); music.finishSession();
        qInfo() << "Final persistence barrier_us=" << barrier.nsecsElapsed() / 1000;
        QSettings saved(profile.filePath("preferences.ini"), QSettings::IniFormat);
        const auto snapshot = QJsonDocument::fromJson(saved.value("queueSnapshot").toByteArray()).object();
        QCOMPARE(snapshot.value("index").toInt(), 80);
        QCOMPARE(snapshot.value("queue").toArray().size(), 2000);
        QCOMPARE(snapshot.value("queue").toArray()[80].toObject().value("title"), track(81).value("title"));
        MusicController restored({}, profile.path());
        QCOMPARE(restored.queueIndex(), 80);
        QVERIFY(!restored.playing()); QVERIFY(restored.mediaPlayer()->source().isEmpty());
        restored.finishSession();
    }
};
QTEST_MAIN(PlaybackStorageTests)
#include "tst_playback_storage.moc"
