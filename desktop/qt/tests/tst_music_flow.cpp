#include "music/MusicController.h"
#include "library/LibraryController.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QLibrary>
#include <QJsonDocument>
#include <QSignalSpy>

class MusicFlowTests final : public QObject {
    Q_OBJECT
    QLibrary fixture_;
    void (*configure_)(const char*) = nullptr;
    char* (*snapshot_)() = nullptr;
    void (*free_)(char*) = nullptr;
    QString root() { return qEnvironmentVariable("BOTTLE_TEST_TEMP_ROOT", QDir::tempPath()) + "/bottle-flow-XXXXXX"; }
    void configure(QJsonObject object) { configure_(QJsonDocument(object).toJson(QJsonDocument::Compact).constData()); }
    QJsonArray requests(const QString& path) {
        auto* raw=snapshot_();const auto rows=QJsonDocument::fromJson(raw).object().value("requests").toArray();free_(raw);
        QJsonArray selected;for(const auto& row:rows)if(row.toObject().value("path")==path)selected.append(row);return selected;
    }
    static QJsonObject track(int i) { return {{"hash",QString::number(i,16).rightJustified(32,'0')},{"title",QString::number(i)}}; }
    void initialize(MusicController& music) { music.setVolume(0);music.initialize();QTRY_VERIFY(music.ready()); }
private slots:
    void initTestCase() {
        fixture_.setFileName(qEnvironmentVariable("BOTTLE_ACCOUNT_TEST_DLL"));fixture_.setLoadHints(QLibrary::PreventUnloadHint);QVERIFY(fixture_.load());
        configure_=reinterpret_cast<void(*)(const char*)>(fixture_.resolve("EchoTestConfigure"));
        snapshot_=reinterpret_cast<char*(*)()>(fixture_.resolve("EchoTestSnapshot"));
        free_=reinterpret_cast<void(*)(char*)>(fixture_.resolve("EchoFreeString"));QVERIFY(configure_&&snapshot_&&free_);
    }
    void radioConsumesTailAndAppendsWithoutDuplicates() {
        QTemporaryDir profile(root());configure({{"fm_batches",QJsonArray{QJsonArray{track(1),track(2),track(3)},QJsonArray{track(3),track(4),track(5)}}}});
        MusicController music(fixture_.fileName(),profile.path());initialize(music);music.startRadio();QTRY_VERIFY(music.radio()&&!music.radioPending());
        QCOMPARE(music.queue().size(),3);music.next();QCOMPARE(music.queueIndex(),1);
        QTRY_VERIFY(!music.radioPending());QCOMPARE(music.queue().size(),5);QCOMPARE(music.current().value("hash").toString(),track(2).value("hash").toString());
        music.next();QCOMPARE(music.current().value("hash").toString(),track(3).value("hash").toString());
        music.next();QCOMPARE(music.current().value("hash").toString(),track(4).value("hash").toString());music.finishSession();
    }
    void failedRadioEntryPreservesQueueAndMode() {
        QTemporaryDir profile(root());configure({{"fm_fail",true}});MusicController music(fixture_.fileName(),profile.path());initialize(music);
        music.playTracks({track(41).toVariantMap(),track(42).toVariantMap()},0);const auto before=music.current();music.startRadio();
        QTRY_VERIFY(!music.radioPending());QVERIFY(!music.radio());QCOMPARE(music.current().value("hash"),before.value("hash"));QCOMPARE(music.queue().size(),2);music.finishSession();
    }
    void pendingRadioCannotReplaceNewManualSelection() {
        QTemporaryDir profile(root());configure({{"fm_delay",200},{"ignore_cancel",true}});MusicController music(fixture_.fileName(),profile.path());initialize(music);
        music.startRadio();music.playTracks({track(77).toVariantMap()},0);QTest::qWait(350);
        QVERIFY(!music.radio());QVERIFY(!music.radioPending());QCOMPARE(music.current().value("hash").toString(),track(77).value("hash").toString());music.finishSession();
    }
    void restoredRadioReloadsSavedTrack() {
        QTemporaryDir profile(root());configure({});QSettings settings(profile.filePath("preferences.ini"),QSettings::IniFormat);
        settings.setValue("queueSnapshot",QJsonDocument(QJsonObject{{"version",1},{"queue",QJsonArray{track(1),track(2)}},{"index",1},{"position",2500},{"radio",true}}).toJson());settings.sync();
        MusicController music(fixture_.fileName(),profile.path());initialize(music);music.toggle();QTRY_VERIFY(!requests("/song/url").isEmpty());
        QCOMPARE(requests("/song/url")[0].toObject().value("query").toObject().value("hash"),track(2).value("hash"));QVERIFY(requests("/personal/fm").isEmpty());music.finishSession();
    }
    void errorThenPlayResolvesAgain() {
        QTemporaryDir profile(root());configure({});MusicController music(fixture_.fileName(),profile.path());initialize(music);music.playTracks({track(1).toVariantMap()},0);
        QTRY_VERIFY_WITH_TIMEOUT(!music.loading()&&music.mediaPlayer()->error()!=QMediaPlayer::NoError,10000);
        const int before=requests("/song/url").size();music.toggle();QTRY_COMPARE(requests("/song/url").size(),before+1);music.finishSession();
    }
    void accountChangeDiscardsPendingRadio() {
        QTemporaryDir profile(root());configure({{"fm_delay",150},{"ignore_cancel",true}});MusicController music(fixture_.fileName(),profile.path());initialize(music);
        music.startRadio();music.setAccount("new-account");QTest::qWait(250);QVERIFY(!music.radio());QVERIFY(music.queue().isEmpty());music.finishSession();
    }
    void rowPlayWaitsForCompletePlaylist() {
        QTemporaryDir profile(root());configure({{"logged",true},{"tracks_delay",100}});MusicController music(fixture_.fileName(),profile.path());initialize(music);
        LibraryController library(music.nativeGateway());connect(&library,&LibraryController::playRequested,&music,&MusicController::playTracks);
        library.setAccount("42");QTRY_VERIFY(!library.playlistsLoading());library.openPlaylist("collection_alpha");
        QTRY_VERIFY(library.loading()&&library.tracks().size()==50);library.play(0);QVERIFY(music.queue().isEmpty());
        QTRY_VERIFY(library.complete());QCOMPARE(music.queue().size(),51);music.finishSession();
    }
    void albumUsesAlbumRoute() {
        QTemporaryDir profile(root());configure({});MusicController music(fixture_.fileName(),profile.path());initialize(music);
        LibraryController library(music.nativeGateway());library.openAlbum({{"id","123"},{"name","Album"}});QTRY_VERIFY(library.complete());
        QCOMPARE(library.tracks().size(),2);QCOMPARE(requests("/album/songs").size(),1);QVERIFY(requests("/playlist/track/all").isEmpty());music.finishSession();
    }
};
QTEST_MAIN(MusicFlowTests)
#include "tst_music_flow.moc"
