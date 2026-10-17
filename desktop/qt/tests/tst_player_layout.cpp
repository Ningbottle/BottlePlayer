#include "ui/PlayerLayout.h"
#include <QTemporaryDir>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

namespace {
QString tempTemplate() {
    // A caller can select its writable test root in restricted environments.
    const QString root = qEnvironmentVariable("BOTTLE_TEST_TEMP_ROOT", QDir::tempPath());
    return QDir(root).absoluteFilePath("bottle-layout-test-XXXXXX");
}
QRect rect(const QVariantMap& cards, const char* id) {
    const auto value = cards.value(QLatin1String(id)).toMap();
    return {value.value("x").toInt(), value.value("y").toInt(), value.value("w").toInt(), value.value("h").toInt()};
}
bool disjoint(const QVariantMap& cards) {
    const auto ids = cards.keys();
    for (int i = 0; i < ids.size(); ++i) {
        const auto value = cards[ids[i]].toMap();
        const QRect a(value["x"].toInt(), value["y"].toInt(), value["w"].toInt(), value["h"].toInt());
        if (!QRect(0, 0, 12, 6).contains(a) || !a.isValid()) return false;
        for (int j = i + 1; j < ids.size(); ++j) {
            const auto b = cards[ids[j]].toMap();
            if (a.intersects(QRect(b["x"].toInt(), b["y"].toInt(), b["w"].toInt(), b["h"].toInt()))) return false;
        }
    }
    return true;
}
}
class PlayerLayoutTests final : public QObject {
    Q_OBJECT
private slots:
    void requestedDefault() {
        QTemporaryDir profile(tempTemplate()); QVERIFY(profile.isValid()); PlayerLayout layout(profile.path());
        QCOMPARE(rect(layout.cards(), "lyrics"), QRect(6, 0, 6, 6));
        QCOMPARE(rect(layout.cards(), "cover"), QRect(0, 0, 6, 5));
        QCOMPARE(rect(layout.cards(), "info"), QRect(0, 5, 6, 1));
        QVERIFY(disjoint(layout.cards()));
    }
    void previewsAreTransactional() {
        QTemporaryDir profile(tempTemplate()); QVERIFY(profile.isValid()); PlayerLayout layout(profile.path()); const auto original = layout.cards();
        QSignalSpy changed(&layout, &PlayerLayout::cardsChanged);
        const auto preview = layout.previewMove("lyrics", 0, 0);
        QVERIFY(preview["ok"].toBool()); QCOMPARE(layout.cards(), original); QCOMPARE(changed.size(), 0);
        const auto next = preview["cards"].toMap();
        QCOMPARE(rect(next, "cover"), QRect(6, 0, 6, 5)); QCOMPARE(rect(next, "info"), QRect(6, 5, 6, 1));
        QVERIFY(layout.commit(next)); QCOMPARE(changed.size(), 1); QVERIFY(disjoint(layout.cards()));
        auto invalid = next; invalid["lyrics"] = QVariantMap{{"x", 0}, {"y", 0}, {"w", 12}, {"h", 6}};
        QVERIFY(!layout.commit(invalid)); QCOMPARE(layout.cards(), next); QCOMPARE(changed.size(), 1);
    }
    void resizingCannotCoverNeighbours() {
        QTemporaryDir profile(tempTemplate()); QVERIFY(profile.isValid()); PlayerLayout layout(profile.path());
        QVERIFY(layout.resize("cover", 12, 6)); QCOMPARE(rect(layout.cards(), "cover"), QRect(0, 0, 6, 5));
        QVERIFY(layout.resize("cover", 0, 0)); QCOMPARE(rect(layout.cards(), "cover"), QRect(0, 0, 2, 2));
        QVERIFY(!layout.resize("absent", 4, 4)); QVERIFY(disjoint(layout.cards()));
    }
    void addHideAndEmpty() {
        QTemporaryDir profile(tempTemplate()); QVERIFY(profile.isValid()); PlayerLayout layout(profile.path());
        QVERIFY(layout.add("progress")); QVERIFY(layout.add("quality")); QCOMPARE(layout.cards().size(), 5);
        QVERIFY(disjoint(layout.cards())); QVERIFY(!layout.add("quality")); QVERIFY(!layout.add("unknown"));
        for (const auto& id : layout.cards().keys()) QVERIFY(layout.hide(id));
        QVERIFY(layout.cards().isEmpty()); QVERIFY(layout.add("lyrics"));
        QCOMPARE(rect(layout.cards(), "lyrics"), QRect(0, 0, 6, 6));
        layout.reset(); QCOMPARE(rect(layout.cards(), "lyrics"), QRect(6, 0, 6, 6));
    }
    void persistsAndRejectsCorruption() {
        QTemporaryDir profile(tempTemplate()); QVERIFY(profile.isValid()); QVariantMap expected;
        { PlayerLayout layout(profile.path()); QVERIFY(layout.move("lyrics", 0, 0)); QVERIFY(layout.add("progress")); expected = layout.cards(); }
        { PlayerLayout restored(profile.path()); QCOMPARE(restored.cards(), expected); }
        { QSettings settings(profile.filePath("player-layout.ini"), QSettings::IniFormat);
          settings.setValue("layout", QByteArray("{\"version\":1,\"cards\":{\"lyrics\":{\"x\":5,\"y\":0,\"w\":7.5,\"h\":6}}}")); settings.sync(); }
        PlayerLayout clean(profile.path()); QCOMPARE(rect(clean.cards(), "lyrics"), QRect(6, 0, 6, 6));
        auto invalid = clean.cards(); invalid["unknown"] = QVariantMap{{"x", 0}, {"y", 0}, {"w", 1}, {"h", 1}};
        QVERIFY(!clean.commit(invalid));
    }
    void upgradesOnlyPreviousDefault() {
        QTemporaryDir profile(tempTemplate()); QVERIFY(profile.isValid());
        const QByteArray oldDefault("{\"version\":1,\"cards\":{\"lyrics\":{\"x\":5,\"y\":0,\"w\":7,\"h\":6},\"cover\":{\"x\":0,\"y\":0,\"w\":5,\"h\":4},\"info\":{\"x\":0,\"y\":4,\"w\":5,\"h\":2}}}");
        { QSettings saved(profile.filePath("player-layout.ini"), QSettings::IniFormat);
          saved.setValue("layout", oldDefault); saved.sync(); }
        { PlayerLayout upgraded(profile.path());
          QCOMPARE(rect(upgraded.cards(), "cover"), QRect(0, 0, 6, 5)); }
        QSettings saved(profile.filePath("player-layout.ini"), QSettings::IniFormat);
        QCOMPARE(saved.value("layout").toByteArray(), oldDefault); // Reading does not rewrite the profile.
        auto customized = QJsonDocument::fromJson(oldDefault).object();
        auto cards = customized.value("cards").toObject(); cards.remove("info");
        customized["cards"] = cards;
        saved.setValue("layout", QJsonDocument(customized).toJson(QJsonDocument::Compact)); saved.sync();
        PlayerLayout retained(profile.path());
        QCOMPARE(rect(retained.cards(), "cover"), QRect(0, 0, 5, 4));
        QVERIFY(!retained.cards().contains("info"));
    }
};
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    PlayerLayoutTests tests;
    const int result = QTest::qExec(&tests, argc, argv);
    fprintf(stderr, "player layout tests: %s\n", result ? "failed" : "passed");
    return result;
}
#include "tst_player_layout.moc"
