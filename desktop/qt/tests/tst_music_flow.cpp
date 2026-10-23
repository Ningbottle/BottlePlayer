#include "music/MusicController.h"
#include "library/LibraryController.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QLibrary>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QFile>
#include <QtEndian>
#include <cstring>

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
    static void saveSnapshot(const QString& profile, const QJsonArray& rows, int index, bool radio = false) {
        QSettings settings(QDir(profile).filePath("preferences.ini"), QSettings::IniFormat);
        settings.setValue("queueSnapshot", QJsonDocument(QJsonObject{{"version",1},{"queue",rows},{"index",index},{"radio",radio}}).toJson());
        settings.sync();
    }
    static bool writeSilence(const QString& path) {
        constexpr int rate = 8000, seconds = 2;
        QByteArray wav(44 + rate * seconds * 2, '\0');
        std::memcpy(wav.data(), "RIFF", 4); std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
        qToLittleEndian<quint32>(quint32(wav.size() - 8), wav.data() + 4);
        qToLittleEndian<quint32>(16, wav.data() + 16); qToLittleEndian<quint16>(1, wav.data() + 20);
        qToLittleEndian<quint16>(1, wav.data() + 22); qToLittleEndian<quint32>(rate, wav.data() + 24);
        qToLittleEndian<quint32>(rate * 2, wav.data() + 28); qToLittleEndian<quint16>(2, wav.data() + 32);
        qToLittleEndian<quint16>(16, wav.data() + 34); std::memcpy(wav.data() + 36, "data", 4);
        qToLittleEndian<quint32>(quint32(wav.size() - 44), wav.data() + 40);
        QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(wav) == wav.size();
    }
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
    void queueSelectionAndContentsHaveSeparateNotifications() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        MusicController music({}, profile.path()); music.setVolume(0);
        QSignalSpy contents(&music, &MusicController::queueChanged);
        QSignalSpy selection(&music, &MusicController::queueIndexChanged);
        QSignalSpy favorite(&music, &MusicController::favoriteChanged);
        music.playTracks({track(1).toVariantMap(), track(2).toVariantMap(), track(3).toVariantMap()}, 0);
        QCOMPARE(contents.count(), 1); QCOMPARE(selection.count(), 1);
        contents.clear(); selection.clear(); favorite.clear();
        music.next(); QCOMPARE(music.queueIndex(), 1);
        music.previous(); QCOMPARE(music.queueIndex(), 0);
        music.playQueue(2); QCOMPARE(music.current().value("hash").toString(), track(3).value("hash").toString());
        QCOMPARE(contents.count(), 0); QCOMPARE(selection.count(), 3); QCOMPARE(favorite.count(), 3);
        music.setAccount("another-account"); QCOMPARE(contents.count(), 0);
        selection.clear();
        QVERIFY(music.enqueueTrack(track(4).toVariantMap()));
        QCOMPARE(contents.count(), 1); QCOMPARE(selection.count(), 0);
        QVERIFY(music.enqueueTrack(track(4).toVariantMap())); QCOMPARE(contents.count(), 1);
        const auto currentHash = music.current().value("hash");
        music.moveQueue(3, 0); QCOMPARE(music.queueIndex(), 3); QCOMPARE(music.current().value("hash"), currentHash);
        QCOMPARE(contents.count(), 2); QCOMPARE(selection.count(), 1);
        music.removeQueue(0); QCOMPARE(music.queueIndex(), 2); QCOMPARE(music.current().value("hash"), currentHash);
        QCOMPARE(contents.count(), 3); QCOMPARE(selection.count(), 2);
        music.removeQueue(2); QCOMPARE(music.queueIndex(), 1); QCOMPARE(contents.count(), 4); QCOMPARE(selection.count(), 3);
        music.clearQueue(); QVERIFY(music.queue().isEmpty()); QCOMPARE(music.queueIndex(), -1);
        QCOMPARE(contents.count(), 5); QCOMPARE(selection.count(), 4);
        music.clearQueue(); QCOMPARE(contents.count(), 5); QCOMPARE(selection.count(), 4);
        music.finishSession();
    }
    void restoredQueueAndReturnedListsRemainIndependent() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        saveSnapshot(profile.path(), {track(11), QJsonObject{{"title","invalid"}}, track(12)}, 2);
        MusicController music({}, profile.path());
        QCOMPARE(music.queue().size(), 2); QCOMPARE(music.queueIndex(), 1);
        QCOMPARE(music.current().value("hash").toString(), track(12).value("hash").toString());
        QCOMPARE(music.hero(), music.current());
        auto returned = music.queue(); auto row = returned[1].toMap(); row.insert("title", "changed by consumer"); returned[1] = row;
        QCOMPARE(music.current().value("title").toString(), QString("12"));
        QVERIFY(music.enqueueTrack(track(13).toVariantMap())); QCOMPARE(music.queue().size(), 3);
        QCOMPARE(returned.size(), 2); QCOMPARE(music.queue()[1].toMap().value("title").toString(), QString("12"));
        music.finishSession();
    }
    void metadataOnlyInvalidatesQueueWhenItActuallyChanges() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        configure({{"song_metadata", QJsonObject{{"image", "https://example.test/cover.jpg"}, {"album", "Resolved album"}}}});
        MusicController music(fixture_.fileName(), profile.path()); initialize(music);
        QSignalSpy contents(&music, &MusicController::queueChanged);
        QSignalSpy selection(&music, &MusicController::queueIndexChanged);
        music.playTracks({track(1).toVariantMap(), track(2).toVariantMap()}, 0);
        QCOMPARE(music.current().value("image").toString(), QString{});
        QTRY_COMPARE(music.current().value("album").toString(), QString("Resolved album"));
        QCOMPARE(contents.count(), 2); QCOMPARE(selection.count(), 1);
        QCOMPARE(music.queue()[0].toMap().value("image").toString(), QString("https://example.test/cover.jpg"));
        music.playQueue(1); QTRY_COMPARE(music.current().value("album").toString(), QString("Resolved album"));
        contents.clear(); selection.clear();
        music.playQueue(0); QTRY_VERIFY(!music.mediaPlayer()->source().isEmpty());
        QCOMPARE(contents.count(), 0); QCOMPARE(selection.count(), 1);
        QCOMPARE(music.hero().value("album").toString(), QString("Resolved album")); music.finishSession();
    }
    void feedAndSearchProjectionFollowRefreshPaginationAndFailure() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        configure({{"feed_count",3},{"search_delay",40},{"search_total",5},
            {"search_batches",QJsonArray{QJsonArray{track(10),track(11)},QJsonArray{track(11),track(12)},QJsonArray{track(12)}}}});
        MusicController music(fixture_.fileName(), profile.path()); initialize(music); QTRY_COMPARE(music.feed().size(), 3);
        music.setFeed({track(91).toVariantMap(), track(92).toVariantMap()});
        QCOMPARE(music.feed().size(), 2); QCOMPARE(music.hero().value("hash").toString(), track(91).value("hash").toString());
        auto returned = music.feed(); returned.clear(); QCOMPARE(music.feed().size(), 2);
        QSignalSpy search(&music, &MusicController::searchChanged);
        music.search("fixture"); QVERIFY(music.searching()); QVERIFY(music.searchResults().isEmpty());
        QTRY_VERIFY(!music.searching()); QCOMPARE(search.count(), 2); QCOMPARE(music.searchResults().size(), 2);
        const auto pageOne = music.searchResults(); music.search("fixture", true);
        QVERIFY(music.searching()); QCOMPARE(music.searchResults(), pageOne);
        QTRY_VERIFY(!music.searching()); QCOMPARE(music.searchResults().size(), 3); QCOMPARE(pageOne.size(), 2);
        QCOMPARE(music.searchResults()[2].toMap().value("hash").toString(), track(12).value("hash").toString());
        music.search("fixture", true); QTRY_VERIFY(!music.searching());
        QCOMPARE(music.searchResults().size(), 3); QCOMPARE(music.searchTotal(), 3);
        music.search("__bottle_no_match__"); QVERIFY(music.searchResults().isEmpty()); QTRY_VERIFY(!music.searching()); QCOMPARE(music.searchTotal(), 0);
        music.search("__bottle_search_error__"); QTRY_VERIFY(!music.searching());
        QVERIFY(!music.searchError().isEmpty()); QVERIFY(music.searchResults().isEmpty());
        music.finishSession();
    }
    void radioPendingAndModeDoNotInvalidateQueueContents() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid()); configure({{"fm_delay",80}});
        MusicController music(fixture_.fileName(), profile.path()); initialize(music);
        music.playTracks({track(40).toVariantMap(),track(41).toVariantMap()},0);
        QSignalSpy contents(&music, &MusicController::queueChanged);
        QSignalSpy selection(&music, &MusicController::queueIndexChanged);
        QSignalSpy mode(&music, &MusicController::radioChanged);
        QSignalSpy pending(&music, &MusicController::radioPendingChanged);
        QVariantMap modeTrack, pendingTrack; bool modeAtCompletion = false;
        connect(&music,&MusicController::radioChanged,&music,[&] { if (music.radio()) modeTrack = music.current(); });
        connect(&music,&MusicController::radioPendingChanged,&music,[&] {
            if (!music.radioPending()) { pendingTrack = music.current(); modeAtCompletion = music.radio(); }
        });
        music.startRadio(); QVERIFY(music.radioPending()); QCOMPARE(pending.count(),1); QCOMPARE(contents.count(),0); QCOMPARE(mode.count(),0);
        QTRY_VERIFY(music.radio() && !music.radioPending());
        QCOMPARE(pending.count(),2); QCOMPARE(contents.count(),1); QCOMPARE(mode.count(),1); QCOMPARE(selection.count(),0);
        QCOMPARE(modeTrack.value("hash").toString(),track(1).value("hash").toString());
        QCOMPARE(pendingTrack,modeTrack); QVERIFY(modeAtCompletion);
        contents.clear(); mode.clear(); music.stopRadio(); QCOMPARE(mode.count(),1); QCOMPARE(contents.count(),0);
        music.stopRadio(); QCOMPARE(mode.count(),1); QCOMPARE(contents.count(),0); music.finishSession();
    }
    void radioAppendTrimAndAccountClearKeepProjectionConsistent() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        QJsonArray rows; for (int i=1;i<=103;++i) rows.append(track(i));
        saveSnapshot(profile.path(), rows, 101, true);
        QJsonArray batches; batches.append(QJsonArray{track(103),track(104),track(105)});
        QCOMPARE(batches.first().toArray().size(),3);
        configure({{"fm_batches",batches}});
        MusicController music(fixture_.fileName(), profile.path()); initialize(music);
        QSignalSpy contents(&music, &MusicController::queueChanged);
        QSignalSpy selection(&music, &MusicController::queueIndexChanged);
        QSignalSpy mode(&music, &MusicController::radioChanged);
        music.next(); QTRY_VERIFY(!music.radioPending());
        QCOMPARE(music.queue().size(),103); QCOMPARE(music.queueIndex(),100);
        QCOMPARE(music.current().value("hash").toString(),track(103).value("hash").toString());
        QCOMPARE(music.queue().first().toMap().value("hash").toString(),track(3).value("hash").toString());
        QCOMPARE(music.queue().last().toMap().value("hash").toString(),track(105).value("hash").toString());
        QCOMPARE(contents.count(),1); QCOMPARE(selection.count(),2); QCOMPARE(mode.count(),0);
        contents.clear(); music.next(); QTRY_VERIFY(!music.radioPending());
        QCOMPARE(contents.count(),0); QCOMPARE(music.current().value("hash").toString(),track(104).value("hash").toString());
        music.setAccount("new-account"); QVERIFY(!music.radio()); QVERIFY(music.queue().isEmpty()); QCOMPARE(music.queueIndex(),-1);
        QCOMPARE(contents.count(),1); QCOMPARE(mode.count(),1); music.finishSession();
    }
    void decodedDurationAndLocalLyricsRefreshTheirProjections() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        QVERIFY(writeSilence(profile.filePath("fixture.wav")));
        QFile lrc(profile.filePath("fixture.lrc")); QVERIFY(lrc.open(QIODevice::WriteOnly));
        lrc.write("[00:00.00]<00:00.00>Hello <00:00.50>world\n[00:00.00]Translation\n[00:01.00]Second line"); lrc.close();
        MusicController music({},profile.path()); music.setVolume(0);
        QSignalSpy contents(&music,&MusicController::queueChanged);
        QSignalSpy lyrics(&music,&MusicController::lyricsChanged);
        music.openLocal(QUrl::fromLocalFile(profile.filePath("fixture.wav"))); music.setPlaying(false);
        QCOMPARE(music.lyrics().size(),2); QCOMPARE(music.lyrics()[0].toMap().value("translation").toString(),QString("Translation"));
        QCOMPARE(music.lyrics()[0].toMap().value("words").toList().size(),2);
        auto returned = music.lyrics(); returned.clear(); QCOMPARE(music.lyrics().size(),2);
        QTRY_COMPARE_WITH_TIMEOUT(music.queue()[0].toMap().value("duration").toInt(),2,10000);
        QCOMPARE(contents.count(),2); QCOMPARE(music.current().value("duration").toInt(),2);
        QVERIFY(lyrics.count() >= 2);
        bool observedClear = false, emptyLyricsOnQueueNotification = false;
        connect(&music,&MusicController::queueChanged,&music,[&] {
            if (music.queue().isEmpty()) { observedClear = true; emptyLyricsOnQueueNotification = music.lyrics().isEmpty(); }
        });
        music.clearQueue(); QVERIFY(observedClear); QVERIFY(emptyLyricsOnQueueNotification);
        QVERIFY(music.lyrics().isEmpty()); music.finishSession();
    }
    void legacyHiResPreferenceUsesTheCanonicalToken() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid()); configure({});
        { QSettings saved(profile.filePath("preferences.ini"),QSettings::IniFormat);
          saved.setValue("quality","hires"); saved.sync(); }
        MusicController music(fixture_.fileName(),profile.path());
        QCOMPARE(music.quality(),QString("high"));
        music.finishSession();
    }
    void earlyQualitySelectionRestartsPendingAvailability() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        configure({{"delay_path","/privilege/lite"},{"delay_ms",200},
            {"privilege_goods",QJsonArray{QJsonObject{{"quality","128"}}}}});
        MusicController music(fixture_.fileName(),profile.path()); initialize(music);
        music.playTracks({track(1).toVariantMap()},0);
        QTRY_COMPARE_WITH_TIMEOUT(requests("/privilege/lite").size(),1,3000);
        QVERIFY(music.availableQualities().isEmpty()); music.setQuality("flac");
        QTRY_COMPARE_WITH_TIMEOUT(music.availableQualities().size(),1,3000);
        QCOMPARE(music.availableQualities().first().toMap().value("quality").toString(),QString("128"));
        QCOMPARE(requests("/privilege/lite").size(),2);
        music.finishSession();
    }
    void singleDeliveredQualityDoesNotCollapseThePicker() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        configure({});
        MusicController music(fixture_.fileName(),profile.path()); initialize(music);
        QStringList catalog; for (const auto& option : music.qualityCatalog()) catalog.append(option.toMap().value("quality").toString());
        QCOMPARE(catalog, QStringList({"128","320","flac","high","master"}));
        QCOMPARE(music.qualityText("high"), QString("Hi-Res"));
        QCOMPARE(music.qualityText("master"), QString("臻品母带"));
        QVERIFY(music.availableQualities().isEmpty());
        music.playTracks({track(1).toVariantMap()},0);
        // Native v5 answers with exactly one quality. The picker must keep the
        // song's own availability instead of inheriting that single entry.
        QTRY_VERIFY_WITH_TIMEOUT(music.availableQualities().size()>1,5000);
        QStringList offered; for (const auto& option : music.availableQualities()) offered.append(option.toMap().value("quality").toString());
        QCOMPARE(offered, QStringList({"128","320","flac","high"}));
        QCOMPARE(requests("/privilege/lite").size(),1);
        QCOMPARE(requests("/privilege/lite")[0].toObject().value("query").toObject().value("hash"),track(1).value("hash"));
        const int before=requests("/song/url").size();
        music.setQuality("flac");
        QCOMPARE(music.quality(),QString("flac"));
        QTRY_COMPARE_WITH_TIMEOUT(requests("/song/url").size(),before+1,5000);
        QCOMPARE(requests("/song/url").last().toObject().value("query").toObject().value("quality").toString(),QString("flac"));
        // Switching quality keeps the same song, so its availability is reused.
        QCOMPARE(requests("/privilege/lite").size(),1);
        music.finishSession();
    }
    void privilegeAnswerTrimsThePickerPerSong() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        configure({{"privilege_goods",QJsonArray{QJsonObject{{"quality","128"}}}}});
        MusicController music(fixture_.fileName(),profile.path()); initialize(music);
        music.playTracks({track(1).toVariantMap()},0);
        QTRY_COMPARE_WITH_TIMEOUT(music.availableQualities().size(),1,5000);
        QCOMPARE(music.availableQualities()[0].toMap().value("quality").toString(),QString("128"));
        music.finishSession();
    }
    void emptyPrivilegeAnswerFallsBackToTheCatalog() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        configure({{"privilege_goods",QJsonArray{}}});
        MusicController music(fixture_.fileName(),profile.path()); initialize(music);
        music.playTracks({track(1).toVariantMap()},0);
        QTRY_COMPARE_WITH_TIMEOUT(music.availableQualities().size(),music.qualityCatalog().size(),5000);
        music.finishSession();
    }
    void clearedQueueDropsTheResolvedQualities() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        configure({});
        MusicController music(fixture_.fileName(),profile.path()); initialize(music);
        music.playTracks({track(1).toVariantMap()},0);
        QTRY_VERIFY_WITH_TIMEOUT(music.availableQualities().size()>1,5000);
        music.clearQueue(); QVERIFY(music.availableQualities().isEmpty()); music.finishSession();
    }
    void queueAndFeedGetterBenchmark2000Tracks() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid());
        QJsonArray rows; QVariantList feed;
        for (int i=1;i<=2000;++i) { const auto row=track(i); rows.append(row); feed.append(row.toVariantMap()); }
        saveSnapshot(profile.path(),rows,1500);
        MusicController music({},profile.path()); music.setFeed(feed);
        QCOMPARE(music.queue().size(),2000); QCOMPARE(music.feed().size(),2000);
        qsizetype observed = 0;
        QBENCHMARK {
            const auto queue = music.queue(); const auto recommendations = music.feed();
            observed = queue.size() + recommendations.size();
        }
        QCOMPARE(observed, qsizetype(4000)); music.finishSession();
    }
};
QTEST_MAIN(MusicFlowTests)
#include "tst_music_flow.moc"
