#include "account/VipController.h"
#include "library/FavoriteController.h"
#include "library/LibraryController.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QLibrary>
#include <QJsonDocument>
#include <QSignalSpy>

class LibraryMigrationTests : public QObject {
    Q_OBJECT
    QLibrary fixture_;
    using Configure = void(*)(const char*);
    Configure configure_ = nullptr;
    QString root() { return qEnvironmentVariable("BOTTLE_TEST_TEMP_ROOT", QDir::tempPath()) + "/bottle-library-XXXXXX"; }
    static void initialize(NativeGateway& gateway) { bool ready = false; gateway.initialize([&](const QJsonObject& value) { ready = value.value("status").toInt() == 1; }); QTRY_VERIFY(ready); }
    void configure(QJsonObject config) { configure_(QJsonDocument(config).toJson(QJsonDocument::Compact).constData()); }
private slots:
    void initTestCase() {
        fixture_.setFileName(qEnvironmentVariable("BOTTLE_ACCOUNT_TEST_DLL")); fixture_.setLoadHints(QLibrary::PreventUnloadHint); QVERIFY(fixture_.load());
        configure_ = reinterpret_cast<Configure>(fixture_.resolve("EchoTestConfigure")); QVERIFY(configure_);
    }
    void confirmsVipInsteadOfTrustingAcceptance() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid()); configure({{"logged", true}});
        NativeGateway gateway(fixture_.fileName(), profile.path()); initialize(gateway);
        VipController vip(gateway); vip.setAccount("42"); QTRY_VERIFY(!vip.loading()); QCOMPARE(vip.tier(), QString("none"));
        vip.claim(); QTRY_VERIFY_WITH_TIMEOUT(!vip.busy(), 5000); QCOMPARE(vip.tier(), QString("premium")); QVERIFY(!vip.accepted().isEmpty());
        vip.setAccount({}); QCOMPARE(vip.tier(), QString("unknown")); QVERIFY(vip.accepted().isEmpty());
    }
    void stopsUpgradeAfterCancellationAndKeepsConfirmedGrant() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid()); configure({{"logged", true}, {"delay_path", "/youth/day/vip"}, {"delay_ms", 150}});
        NativeGateway gateway(fixture_.fileName(), profile.path()); initialize(gateway);
        VipController vip(gateway); vip.setAccount("42"); QTRY_VERIFY(!vip.loading()); vip.claim(); vip.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!vip.busy(), 5000); QCOMPARE(vip.tier(), QString("listen"));
        QVERIFY(!vip.caution().isEmpty()); QVERIFY(vip.message().contains(QStringLiteral("畅听权益已确认")));
    }
    void doesNotInventMeaningForUnknownFailureCode() {
        const auto message = VipController::failureMessage({{"status", 0}, {"error_code", 131001}});
        QVERIFY(message.contains("131001")); QVERIFY(!message.contains(QStringLiteral("今天已领取")));
    }
    void favoritesKeepNumericIdsAndPersistAnonymousChanges() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid()); configure({{"logged", true}, {"liked", true}});
        NativeGateway gateway(fixture_.fileName(), profile.path()); initialize(gateway);
        LibraryController library(gateway); library.setAccount("42"); QTRY_VERIFY(!library.playlistsLoading());
        FavoriteController favorite(gateway, profile.path()); favorite.setAccount("42"); favorite.setPlaylists(library.playlists()); QTRY_VERIFY(!favorite.loading());
        const QVariantMap first{{"hash", QString::number(1, 16).rightJustified(32, '0')}, {"title", "Test"}, {"albumId", "album-42"}, {"audioId", "9001"}};
        QVERIFY(favorite.isFavorite(first)); favorite.toggle(first); QTRY_COMPARE(favorite.pendingCount(), 0); QVERIFY(!favorite.isFavorite(first));
        favorite.toggle(first); QTRY_VERIFY_WITH_TIMEOUT(!favorite.loading() && favorite.pendingCount() == 0, 5000); QVERIFY(favorite.isFavorite(first));
        favorite.setAccount({}); favorite.toggle(first); QVERIFY(favorite.isFavorite(first)); favorite.toggle(first); QVERIFY(!favorite.isFavorite(first));
        FavoriteController restored(gateway, profile.path()); QVERIFY(!restored.isFavorite(first));
    }
    void rejectsUnownedPlaylistAndKeepsTransportOutbox() {
        QTemporaryDir profile(root()); QVERIFY(profile.isValid()); configure({{"logged", true}, {"liked", true}, {"playlist_transport_error", true}});
        NativeGateway gateway(fixture_.fileName(), profile.path()); initialize(gateway);
        LibraryController library(gateway); library.setAccount("42"); QTRY_VERIFY(!library.playlistsLoading());
        FavoriteController favorite(gateway, profile.path()); favorite.setAccount("42"); favorite.setPlaylists(library.playlists()); QTRY_VERIFY(!favorite.loading());
        const QVariantMap track{{"hash", QString(32, 'b')}, {"title", "New"}, {"albumId", "42"}, {"audioId", "9000"}};
        QSignalSpy notices(&favorite, &FavoriteController::notice);
        favorite.addToPlaylist({{"id", "collection_unowned"}, {"listid", "999"}}, track); QCOMPARE(notices.count(), 1);
        favorite.toggle(track); QTRY_VERIFY(favorite.message().contains(QStringLiteral("等待同步"))); QCOMPARE(favorite.pendingCount(), 1);
        favorite.setAccount({}); QCOMPARE(favorite.pendingCount(), 0); QVERIFY(!favorite.isFavorite(track));
        favorite.setAccount("43"); QCOMPARE(favorite.pendingCount(), 0);
        favorite.setAccount("42"); QCOMPARE(favorite.pendingCount(), 1);
    }
};
QTEST_GUILESS_MAIN(LibraryMigrationTests)
#include "tst_library_migration.moc"
