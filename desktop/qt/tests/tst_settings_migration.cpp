#include "settings/DataMigration.h"
#include "playback/EqProcessor.h"
#include "playback/PcmOutput.h"
#include "settings/StatsArchive.h"
#include "platform/UpdateController.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <QCryptographicHash>
#include <QSet>
#include <cmath>
#include <numbers>

class SettingsMigrationTests : public QObject {
    Q_OBJECT
    QString root() { return qEnvironmentVariable("BOTTLE_TEST_TEMP_ROOT", QDir::tempPath()) + "/bottle-settings-XXXXXX"; }
private slots:
    void snapshotPreservesSelectedSurvivorAndRejectsBadOffset() {
        const auto restored = DataMigration::sanitizeSnapshot({{"queue", QJsonArray{QJsonObject{{"FileHash", "a"}, {"SongName", "First"}}, QJsonObject{}, QJsonObject{{"FileHash", "b"}, {"SongName", "Second"}}}}, {"currentIndex", 2}, {"position", 12}}, true);
        QCOMPARE(restored.value("queue").toArray().size(), 2); QCOMPARE(restored.value("index").toInt(), 1); QCOMPARE(restored.value("position").toInt(), 12000);
        const auto dropped = DataMigration::sanitizeSnapshot({{"queue", QJsonArray{QJsonObject{}}}, {"index", 0}, {"position", -4}}, false);
        QCOMPARE(dropped.value("index").toInt(), -1); QCOMPARE(dropped.value("position").toInt(), 0);
    }
    void exportExcludesCredentialsAndImportLeavesMissingPreferences() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        QSettings preferences(profile.filePath("preferences.ini"), QSettings::IniFormat);
        preferences.setValue("volume", .7); preferences.setValue("token", "must-not-export"); preferences.setValue("apiKey", "must-not-export"); preferences.sync();
        DataMigration migration(profile.path()); migration.exportData(QUrl::fromLocalFile(profile.filePath("export.json")));
        QFile exported(profile.filePath("export.json")); QVERIFY(exported.open(QIODevice::ReadOnly)); const auto bytes = exported.readAll(); QVERIFY(!bytes.contains("must-not-export"));
        QFile imported(profile.filePath("legacy.json")); QVERIFY(imported.open(QIODevice::WriteOnly));
        imported.write(QJsonDocument(QJsonObject{{"schema", "bottlemusic.user-data-export"}, {"version", 1}, {"localStorage", QJsonObject{{"player_loop_mode", "random"}, {"token", "never-import"}}}}).toJson()); imported.close();
        migration.importData(QUrl::fromLocalFile(imported.fileName())); QVERIFY(migration.restartNeeded()); preferences.sync();
        QCOMPARE(preferences.value("volume").toDouble(), .7); QCOMPARE(preferences.value("loopMode").toString(), QString("random")); QCOMPARE(preferences.value("token").toString(), QString("must-not-export"));
        QVERIFY(!QDir(profile.path()).entryList({"import-backup-*.json"}, QDir::Files).isEmpty());
    }
    void qtDataRoundTripAndLegacyAppearanceAreComplete() {
        QTemporaryDir source(root()), target(root()); QVERIFY(source.isValid() && target.isValid());
        QSettings preferences(source.filePath("preferences.ini"), QSettings::IniFormat);
        preferences.setValue("queueSnapshot", QJsonDocument(QJsonObject{{"version",1},{"queue",QJsonArray{QJsonObject{{"hash","abc"},{"title","Song"}}}},{"index",0},{"position",12000}}).toJson(QJsonDocument::Compact));
        preferences.setValue("vinyl", true); preferences.setValue("lyricSize", 38); preferences.sync();
        QSettings appearance(source.filePath("appearance.ini"), QSettings::IniFormat);
        appearance.setValue("mode","dark"); appearance.setValue("presentation","curtain"); appearance.setValue("lyricFont","kai"); appearance.setValue("lyricFocus","stage"); appearance.setValue("accent","#123456"); appearance.setValue("compact",true); appearance.sync();
        QSettings equalizer(source.filePath("equalizer.ini"), QSettings::IniFormat); equalizer.setValue("preset","Vocal"); equalizer.setValue("bands","[-1,-1,0,1,2,3,3,2,1,0]"); equalizer.sync();
        QSettings favorites(source.filePath("favorites.ini"), QSettings::IniFormat); favorites.setValue("anonymous", "[{\"hash\":\"abc\",\"title\":\"Song\"}]"); favorites.sync();
        DataMigration exporter(source.path()), importer(target.path()); exporter.exportData(QUrl::fromLocalFile(source.filePath("transfer.json"))); importer.importData(QUrl::fromLocalFile(source.filePath("transfer.json"))); QVERIFY(importer.restartNeeded());
        QSettings restored(target.filePath("appearance.ini"), QSettings::IniFormat); QCOMPARE(restored.value("presentation").toString(),QString("curtain")); QCOMPARE(restored.value("accent").toString(),QString("#123456")); QCOMPARE(restored.value("lyricFont").toString(),QString("kai")); QVERIFY(restored.value("compact").toBool());
        QSettings queue(target.filePath("preferences.ini"), QSettings::IniFormat); QCOMPARE(QJsonDocument::fromJson(queue.value("queueSnapshot").toByteArray()).object().value("position").toInt(),12000); QVERIFY(queue.value("vinyl").toBool());
        QSettings eq(target.filePath("equalizer.ini"), QSettings::IniFormat); QCOMPARE(eq.value("preset").toString(),QString("Vocal")); QCOMPARE(QJsonDocument::fromJson(eq.value("bands").toByteArray()).array()[5].toInt(),3);
        QFile legacy(target.filePath("legacy.json")); QVERIFY(legacy.open(QIODevice::WriteOnly)); legacy.write(QJsonDocument(QJsonObject{{"schema","bottlemusic.user-data-export"},{"version",1},{"localStorage",QJsonObject{{"tweak_mode","light"},{"tweak_accent","#abcdef"},{"appearance_skin","mint"},{"player_favorite_markers","[\"marker\"]"}}}}).toJson()); legacy.close(); importer.importData(QUrl::fromLocalFile(legacy.fileName())); restored.sync(); QCOMPARE(restored.value("presentation").toString(),QString("paper")); QCOMPARE(restored.value("accent").toString(),QString("#abcdef"));
    }
    void statsReadOnlyImportIsRepeatableAndExcludesSessions() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        const auto database = profile.filePath("旧版统计.db"), name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(database); QVERIFY(db.open());
            { QSqlQuery query(db); QVERIFY(query.exec("CREATE TABLE play_history_v2(id INTEGER PRIMARY KEY,song_hash TEXT,song_name TEXT,duration_seconds REAL,listened_seconds REAL,played_at INTEGER)"));
              QVERIFY(query.exec("INSERT INTO play_history_v2 VALUES(1,'abc','Song',180,61,1700000000000),(2,'abc','Song',180,61,1700000000000)"));
              QVERIFY(query.exec("CREATE TABLE sessions(token TEXT)")); QVERIFY(query.exec("INSERT INTO sessions VALUES('secret-do-not-copy')")); }
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
        QFile original(database); QVERIFY(original.open(QIODevice::ReadOnly)); const auto before = QCryptographicHash::hash(original.readAll(),QCryptographicHash::Sha256); original.close();
        QSet<QString> seen; std::atomic_bool stopped = false;
        const auto commit = [&seen](const QJsonObject& record) { const auto id = record.value("session_id").toString(); if (seen.contains(id)) return 6; seen.insert(id); return 0; };
        const auto first = StatsArchive::restore(database,profile.filePath("new.db"),profile.filePath("backup.json"),commit,stopped); QCOMPARE(first.value("added").toInt(),2); QCOMPARE(seen.size(),2);
        const auto second = StatsArchive::restore(database,profile.filePath("new.db"),profile.filePath("backup2.json"),commit,stopped); QCOMPARE(second.value("duplicate").toInt(),2);
        QJsonArray records; QCOMPARE(StatsArchive::read(database,records).value("status").toInt(),1); QCOMPARE(StatsArchive::write(profile.filePath("stats.json"),records).value("status").toInt(),1);
        QFile archive(profile.filePath("stats.json")); QVERIFY(archive.open(QIODevice::ReadOnly)); QVERIFY(!archive.readAll().contains("secret-do-not-copy"));
        stopped.store(true); QCOMPARE(StatsArchive::restore(database,profile.filePath("new.db"),profile.filePath("cancel.json"),commit,stopped).value("status").toInt(),0); QCOMPARE(seen.size(),2);
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(QCryptographicHash::hash(original.readAll(),QCryptographicHash::Sha256),before);
        QVERIFY(!QFileInfo::exists(profile.filePath("cancel.json")));
    }
    void updaterRequiresQtPackageAndTrustedChecksum() {
        QJsonObject asset{{"name","BottleMusicQt-1.2.0-x64.zip"},{"digest","sha256:"+QString(64,'a')},{"browser_download_url","https://github.com/Ningbottle/BottlePlayer/releases/download/v1.2.0/BottleMusicQt-1.2.0-x64.zip"}};
        QJsonObject release{{"tag_name","v1.2.0"},{"assets",QJsonArray{asset}}};
        QCOMPARE(UpdateController::selectRelease(release,"1.1.0-qt-preview",{}).value("version").toString(),QString("1.2.0"));
        QVERIFY(UpdateController::selectRelease(release,"1.1.0","1.2.0").value("version").toString().isEmpty());
        QVERIFY(UpdateController::selectRelease(release,"1.2.0",{}).value("version").toString().isEmpty());
        asset.insert("digest",""); release.insert("assets",QJsonArray{asset}); QVERIFY(UpdateController::selectRelease(release,"1.1.0",{}).value("asset").toString().isEmpty());
        asset.insert("digest","sha256:"+QString(64,'a')); asset.insert("browser_download_url","https://other.example/BottleMusicQt-1.2.0-x64.zip"); release.insert("assets",QJsonArray{asset}); QVERIFY(UpdateController::selectRelease(release,"1.1.0",{}).value("asset").toString().isEmpty());
        asset.insert("name","BottleMusic-1.2.0-x64.exe"); release.insert("assets",QJsonArray{asset}); QVERIFY(UpdateController::selectRelease(release,"1.1.0",{}).value("asset").toString().isEmpty());
    }
    void eqFlatIsTransparentAndBoostTargetsFrequency() {
        constexpr int rate = 48000; std::vector<float> flat(1000, .25f); EqProcessor processor; processor.configure(rate, 1); processor.process(flat.data(), int(flat.size()));
        for (const auto value : flat) QCOMPARE(value, .25f);
        const auto measure = [rate](double frequency, bool boost) {
            std::vector<float> samples(rate);
            for (int i = 0; i < rate; ++i) samples[size_t(i)] = float(.01 * std::sin(2 * std::numbers::pi * frequency * i / rate));
            EqProcessor eq; eq.configure(rate, 1); std::array<double, 10> gains{}; if (boost) gains[5] = 6; eq.setGains(gains); eq.process(samples.data(), rate);
            double squares = 0; for (int i = rate / 2; i < rate; ++i) squares += samples[size_t(i)] * samples[size_t(i)]; return std::sqrt(squares / (rate / 2));
        };
        const auto gain = measure(1000, true) / measure(1000, false); QVERIFY(gain > 1.95 && gain < 2.05);
        QVERIFY(measure(100, true) / measure(100, false) < 1.1);
    }
    void eqClampsUnsafeGainsAndHandlesLowRate() {
        EqProcessor eq; eq.configure(8000, 2); std::array<double, 10> gains{}; gains.fill(100); eq.setGains(gains);
        std::vector<float> samples(4096, .8f); eq.process(samples.data(), 2048);
        for (const auto value : samples) QVERIFY(std::isfinite(value) && std::abs(value) <= 1);
    }
    void integerAudioConversionHasSafeEndpoints() {
        const float samples[]{-2,-1,0,.5f,1,2,std::numeric_limits<float>::quiet_NaN()};
        const auto encoded = PcmOutput::encode(samples,7,QAudioFormat::Int16); QCOMPARE(encoded.size(),14);
        const auto* result = reinterpret_cast<const qint16*>(encoded.constData()); QCOMPARE(result[0],qint16(-32768)); QCOMPARE(result[2],qint16(0)); QCOMPARE(result[3],qint16(16384)); QCOMPARE(result[4],qint16(32767)); QCOMPARE(result[6],qint16(0));
        QCOMPARE(PcmOutput::encode(samples,7,QAudioFormat::UInt8).size(),7);
    }
};
QTEST_GUILESS_MAIN(SettingsMigrationTests)
#include "tst_settings_migration.moc"
