#include "account/AccountController.h"
#include "account/QrImage.h"
#include "library/LibraryController.h"
#include <QtTest>
#include <QLibrary>
#include <QImage>
#include <QJsonDocument>
#include <QJsonArray>

class AccountContracts final : public QObject {
    Q_OBJECT
    QLibrary fixture_;
    using Configure = void(*)(const char*); using Snapshot = char*(*)(); using Free = void(*)(char*);
    Configure configure_ = nullptr; Snapshot snapshot_ = nullptr; Free free_ = nullptr;
    void configure(QJsonObject options) { const auto json = QJsonDocument(options).toJson(QJsonDocument::Compact); configure_(json.constData()); }
    QJsonObject snapshot() { char* raw = snapshot_(); const auto data = QJsonDocument::fromJson(raw).object(); free_(raw); return data; }
    static void initialize(NativeGateway& gateway) { gateway.initialize([](QJsonObject) {}); }
private slots:
    void finishedRequestsDoNotCreateCancelRecords() {
        configure({{"poll_delay", 250}}); NativeGateway gateway(fixture_.fileName(), "fixture");
        initialize(gateway); QTRY_VERIFY(gateway.ready());
        bool finished = false;
        const auto completedId = gateway.get("/user/detail", {}, this, [&](const auto&) { finished = true; });
        QTRY_VERIFY(finished);
        gateway.cancel(completedId); gateway.cancel(completedId); gateway.cancel("unknown-request"); gateway.cancel({});
        QCOMPARE(snapshot().value("cancel_calls").toInt(), 0);

        finished = false; QJsonObject result;
        const auto activeId = gateway.get("/login/qr/check", {}, this, [&](const auto& value) { result = value; finished = true; });
        QTRY_COMPARE(snapshot().value("polls").toInt(), 1);
        gateway.cancel(activeId); gateway.cancel(activeId);
        QCOMPARE(snapshot().value("cancel_calls").toInt(), 1);
        QTRY_VERIFY(finished); QCOMPARE(result.value("status").toInt(), 0);
        gateway.cancel(activeId); QCOMPARE(snapshot().value("cancel_calls").toInt(), 1);

        finished = false;
        const auto statsId = gateway.stats("summary", {}, this, [&](const auto&) { finished = true; });
        gateway.cancel(statsId);
        QTRY_VERIFY(finished); QCOMPARE(snapshot().value("cancel_calls").toInt(), 1);

        finished = false;
        const auto durableId = gateway.recordPlay({{"session_id", "durable-record"}, {"listened_seconds", 61}}, this,
            [&](const auto& value) { result = value; finished = true; });
        gateway.cancel(durableId);
        QTRY_VERIFY(finished); QCOMPARE(result.value("status").toInt(), 1);
        QCOMPARE(snapshot().value("play_records").toArray().size(), 1);
        QCOMPARE(snapshot().value("cancel_calls").toInt(), 1);
    }
    void initTestCase() {
        fixture_.setFileName(qEnvironmentVariable("BOTTLE_ACCOUNT_TEST_DLL")); QVERIFY(fixture_.load());
        configure_ = reinterpret_cast<Configure>(fixture_.resolve("EchoTestConfigure"));
        snapshot_ = reinterpret_cast<Snapshot>(fixture_.resolve("EchoTestSnapshot"));
        free_ = reinterpret_cast<Free>(fixture_.resolve("EchoFreeString")); QVERIFY(configure_ && snapshot_ && free_);
    }
    void qrEncodesInMemory() {
        const auto url = loginQrImage("https://example.test/login?key=fixture"); QVERIFY(url.startsWith("data:image/png;base64,"));
        QImage image; QVERIFY(image.loadFromData(QByteArray::fromBase64(url.section(',', 1).toLatin1()), "PNG"));
        QVERIFY(image.width() >= 150); QCOMPARE(image.pixelColor(0, 0), QColor(Qt::white));
        QVERIFY(loginQrImage(QString(3000, 'x')).isEmpty());
    }
    void restoredSessionLoadsPersonalPlaylists() {
        configure({{"logged", true}}); NativeGateway gateway(fixture_.fileName(), "fixture");
        AccountController account(gateway); LibraryController library(gateway);
        connect(&account, &AccountController::sessionChanged, &library, &LibraryController::setAccount);
        initialize(gateway); QTRY_VERIFY(account.loggedIn()); QTRY_COMPARE(library.playlists().size(), 2);
        QCOMPARE(account.userId(), QString("42")); QCOMPARE(account.username(), QStringLiteral("纸页听众"));
    }
    void refreshDiscardsLateQrResponse() {
        configure({{"first_key_delay", 500}, {"ignore_cancel", true}});
        NativeGateway gateway(fixture_.fileName(), "fixture"); AccountController account(gateway);
        initialize(gateway); QTRY_VERIFY(gateway.ready()); account.activate();
        QTRY_COMPARE(snapshot().value("keys").toInt(), 1);
        account.beginLogin(); QTRY_COMPARE(snapshot().value("keys").toInt(), 2);
        QTRY_COMPARE(account.state(), QString("waiting")); const auto latest = account.qrImage();
        QTest::qWait(650); QCOMPARE(account.qrImage(), latest); QVERIFY(!latest.isEmpty());
        account.deactivate(); QCOMPARE(account.state(), QString("idle")); QVERIFY(account.qrImage().isEmpty());
    }
    void confirmedLoginAndLogout() {
        configure({{"statuses", QJsonArray{2, 4}}}); NativeGateway gateway(fixture_.fileName(), "fixture");
        AccountController account(gateway); LibraryController library(gateway);
        connect(&account, &AccountController::sessionChanged, &library, &LibraryController::setAccount);
        initialize(gateway); QTRY_VERIFY(gateway.ready()); account.activate();
        QTRY_COMPARE_WITH_TIMEOUT(account.state(), QString("scanned"), 4000);
        QTRY_VERIFY_WITH_TIMEOUT(account.loggedIn(), 6500); QTRY_COMPARE(library.playlists().size(), 2);
        account.logout(); QTRY_VERIFY(!account.loggedIn()); QVERIFY(library.playlists().isEmpty());
        bool posted = false; for (const auto& raw : snapshot().value("requests").toArray()) {
            const auto row = raw.toObject(); if (row.value("path") == "/auth/logout") posted = row.value("method") == "POST";
        }
        QVERIFY(posted);
    }
    void expiredQrStopsPolling() {
        configure({{"statuses", QJsonArray{0}}}); NativeGateway gateway(fixture_.fileName(), "fixture"); AccountController account(gateway);
        initialize(gateway); QTRY_VERIFY(gateway.ready()); account.activate();
        QTRY_COMPARE_WITH_TIMEOUT(account.state(), QString("expired"), 4000); QVERIFY(account.qrImage().isEmpty());
        const int polls = snapshot().value("polls").toInt(); QTest::qWait(2100); QCOMPARE(snapshot().value("polls").toInt(), polls);
    }
    void pollingIsSingleFlight() {
        configure({{"poll_delay", 3000}}); NativeGateway gateway(fixture_.fileName(), "fixture"); AccountController account(gateway);
        initialize(gateway); QTRY_VERIFY(gateway.ready()); account.activate();
        QTRY_COMPARE_WITH_TIMEOUT(snapshot().value("polls").toInt(), 1, 3500);
        QTest::qWait(2300); QCOMPARE(snapshot().value("polls").toInt(), 1);
        QCOMPARE(snapshot().value("max_polls_active").toInt(), 1); account.deactivate();
    }
    void playlistPaginationPreservesSongIdentity() {
        configure({{"logged", true}}); NativeGateway gateway(fixture_.fileName(), "fixture"); LibraryController library(gateway);
        initialize(gateway); QTRY_VERIFY(gateway.ready()); library.setAccount("42"); QTRY_COMPARE(library.playlists().size(), 2);
        library.openPlaylist("collection_alpha"); QTRY_VERIFY(library.complete()); QCOMPARE(library.tracks().size(), 51);
        const auto last = library.tracks().last().toMap(); QCOMPARE(last.value("audioId").toString(), QString("9051"));
        QSignalSpy play(&library, &LibraryController::playRequested); library.play(50); QCOMPARE(play.size(), 1);
        QCOMPARE(play.first()[1].toInt(), 50); QCOMPARE(play.first()[0].toList().size(), 51);
    }
    void repeatedPageDoesNotClaimCompletion() {
        configure({{"logged", true}, {"repeat_page", true}}); NativeGateway gateway(fixture_.fileName(), "fixture"); LibraryController library(gateway);
        initialize(gateway); QTRY_VERIFY(gateway.ready()); library.setAccount("42"); QTRY_COMPARE(library.playlists().size(), 2);
        library.openPlaylist("collection_alpha"); QTRY_VERIFY(!library.loading()); QVERIFY(!library.complete());
        QCOMPARE(library.tracks().size(), 50); QVERIFY(!library.error().isEmpty());
    }
    void logoutDropsLatePlaylistResponse() {
        configure({{"logged", true}, {"tracks_delay", 350}, {"ignore_cancel", true}});
        NativeGateway gateway(fixture_.fileName(), "fixture"); LibraryController library(gateway);
        initialize(gateway); QTRY_VERIFY(gateway.ready()); library.setAccount("42"); QTRY_COMPARE(library.playlists().size(), 2);
        library.openPlaylist("collection_alpha"); library.setAccount({}); QTest::qWait(500);
        QVERIFY(library.tracks().isEmpty()); QVERIFY(library.playlists().isEmpty()); QVERIFY(library.playlistId().isEmpty());
    }
};
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv); AccountContracts tests;
    const int result = QTest::qExec(&tests, argc, argv); fprintf(stderr, "account contracts: %s\n", result ? "failed" : "passed"); return result;
}
#include "tst_account.moc"
