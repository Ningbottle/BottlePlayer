// Real menu input with an isolated profile and local media; no account/network service.
QLibrary fixture(args[1]); fixture.setLoadHints(QLibrary::PreventUnloadHint);
if (!fixture.load()) return 2;
auto configure = reinterpret_cast<void(*)(const char*)>(fixture.resolve("EchoTestConfigure"));
auto snapshot = reinterpret_cast<char*(*)()>(fixture.resolve("EchoTestSnapshot"));
auto free = reinterpret_cast<void(*)(char*)>(fixture.resolve("EchoFreeString"));
if (!configure || !snapshot || !free) return 2;

constexpr int rate = 8000, seconds = 30;
QByteArray wav(44 + rate * seconds * 2, '\0');
wav.replace(0, 4, "RIFF"); wav.replace(8, 8, "WAVEfmt "); wav.replace(36, 4, "data");
qToLittleEndian<quint32>(quint32(wav.size() - 8), wav.data() + 4);
qToLittleEndian<quint32>(16, wav.data() + 16); qToLittleEndian<quint16>(1, wav.data() + 20);
qToLittleEndian<quint16>(1, wav.data() + 22); qToLittleEndian<quint32>(rate, wav.data() + 24);
qToLittleEndian<quint32>(rate * 2, wav.data() + 28); qToLittleEndian<quint16>(2, wav.data() + 32);
qToLittleEndian<quint16>(16, wav.data() + 34); qToLittleEndian<quint32>(quint32(wav.size() - 44), wav.data() + 40);
QTcpServer server;
if (!server.listen(QHostAddress::LocalHost)) return 2;
QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
    while (auto* socket = server.nextPendingConnection()) {
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &wav] {
            if (socket->property("sent").toBool()) return;
            const auto request = socket->property("request").toByteArray() + socket->readAll();
            socket->setProperty("request", request);
            if (!request.contains("\r\n\r\n")) return;
            socket->setProperty("sent", true);
            const auto range = QRegularExpression("Range: bytes=(\\d+)-(\\d*)", QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(request));
            const qint64 start = range.hasMatch() ? range.captured(1).toLongLong() : 0;
            const qint64 end = range.hasMatch() && !range.captured(2).isEmpty() ? qMin(range.captured(2).toLongLong(), qint64(wav.size() - 1)) : wav.size() - 1;
            const auto body = wav.mid(start, qMax<qint64>(0, end - start + 1));
            QByteArray header = range.hasMatch() ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
            if (range.hasMatch()) header += "Content-Range: bytes " + QByteArray::number(start) + "-" + QByteArray::number(end) + "/" + QByteArray::number(wav.size()) + "\r\n";
            header += "Content-Type: audio/wav\r\nAccept-Ranges: bytes\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
            socket->write(header + body); socket->disconnectFromHost();
        });
    }
});
const auto options = QJsonDocument(QJsonObject{{"feed_count", 2}, {"audio_url", QStringLiteral("http://127.0.0.1:%1/fixture.wav").arg(server.serverPort())}}).toJson(QJsonDocument::Compact);
configure(options.constData()); music.setVolume(0); music.setReducedMotion(true); music.initialize();
check("quality_core_ready", waitFor([&] { return music.ready() && music.feed().size() == 2; }, 5000));
window->resize(1280, 850); window->requestActivate();
QMetaObject::invokeMethod(window, "changeMode", Q_ARG(QVariant, QStringLiteral("paper")));
QTest::qWait(200);
const auto menuItem = [&](QObject* menu, const QString& quality) {
    return menu ? qobject_cast<QQuickItem*>(findItem(menu->property("contentItem").value<QQuickItem*>(), "quality_" + quality)) : nullptr;
};
const auto choose = [&](const char* trigger, const char* name, const QString& quality) {
    auto* button = quick(trigger); auto* menu = find(name);
    check(QString(name) + "_trigger_enabled", button && button->isEnabled() && button->isVisible());
    click(window, button);
    const bool opened = menu && waitFor([&] { return menu->property("opened").toBool(); }, 1000);
    check(QString(name) + "_opens_by_click", opened);
    auto* item = menuItem(menu, quality);
    check(QString(name) + "_option_visible_" + quality, opened && item && item->isVisible());
    if (opened && item) click(window, item);
    check(QString(name) + "_selects_" + quality, music.quality() == quality);
    // A successful resolve can rebuild the available-qualities delegates.
    auto* selected = menuItem(menu, quality);
    check(QString(name) + "_checked_" + quality, selected && selected->property("checked").toBool());
    int checked = 0;
    for (const auto* key : {"128", "320", "flac", "high", "master"}) {
        auto* option = menuItem(menu, QString::fromLatin1(key));
        if (option && option->property("checked").toBool()) ++checked;
    }
    check(QString(name) + "_single_selection", checked == 1);
    if (menu) QMetaObject::invokeMethod(menu, "close");
};
auto* barMenu = find("qualityMenu");
check("empty_queue_has_all_preferences", music.queueIndex() < 0 && barMenu && barMenu->property("count").toInt() == 5);
for (const auto* key : {"320", "flac", "high", "master", "128"}) {
    choose("qualityButton", "qualityMenu", QString::fromLatin1(key));
    check(QString("idle_label_") + key, quick("qualityButton")->property("text").toString() == music.qualityText(QString::fromLatin1(key)));
    QSettings saved(QDir(profile).filePath("preferences.ini"), QSettings::IniFormat);
    check(QString("idle_quality_persisted_") + key, saved.value("quality").toString() == QLatin1String(key));
}

// The side panel can also set the preference before selecting a track.
QMetaObject::invokeMethod(window, "changeMode", Q_ARG(QVariant, QStringLiteral("curtain")));
window->setProperty("listeningOpen", true); QTest::qWait(300);
choose("listeningQuality", "listeningQualityMenu", "flac");
window->setProperty("listeningOpen", false);
QMetaObject::invokeMethod(window, "changeMode", Q_ARG(QVariant, QStringLiteral("paper")));
QTest::qWait(200);

music.playFrom("feed", 0);
check("selected_preference_used_for_playback", waitFor([&] { return !music.loading() && music.playing() && music.actualQuality() == "flac"; }, 5000));
music.setPlaying(false); music.seek(4000);
check("track_uses_available_qualities", waitFor([&] { return barMenu && barMenu->property("count").toInt() == 4; },3000));
choose("qualityButton", "qualityMenu", "320");
check("paused_switch_loads_selected_quality", waitFor([&] { return !music.loading() && music.actualQuality() == "320"; }, 5000));
check("paused_switch_keeps_position", !music.playing() && qAbs(music.position() - 4000) < 150);
choose("qualityButton", "qualityMenu", "320");
// The optional player-page card is visible only while a track is selected.
layoutModel.hide("cover"); layoutModel.add("quality");
QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, QStringLiteral("player")));
QTest::qWait(200);
choose("playerQualityCard", "playerQualityMenu", "128");
check("player_card_switch_preserves_pause", waitFor([&] { return !music.loading() && music.actualQuality() == "128"; }, 5000)
    && !music.playing() && qAbs(music.position() - 4000) < 150);
char* raw = snapshot(); const auto requests = QJsonDocument::fromJson(raw).object().value("requests").toArray(); free(raw);
QStringList requested;
for (const auto& row : requests) if (row.toObject().value("path") == "/song/url") requested.append(row.toObject().value("query").toObject().value("quality").toString());
check("menu_selection_reaches_native", requested == QStringList{"flac", "320", "128"});
auto trimmedOptions=QJsonDocument::fromJson(options).object();
trimmedOptions.insert("privilege_goods",QJsonArray{QJsonObject{{"quality","128"}}});
configure(QJsonDocument(trimmedOptions).toJson(QJsonDocument::Compact).constData());
music.playFrom("feed",1);
check("known_standard_only_track_trims_menu",waitFor([&] { return barMenu && barMenu->property("count").toInt()==1; },3000));
trimmedOptions.insert("privilege_goods",QJsonArray{});
configure(QJsonDocument(trimmedOptions).toJson(QJsonDocument::Compact).constData());
music.playFrom("feed",0);
check("unknown_track_qualities_restore_catalog",waitFor([&] { return music.availableQualities().size()==5
    && barMenu && barMenu->property("count").toInt()==5; },3000));
QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, QStringLiteral("home")));
music.clearQueue();
check("cleared_queue_restores_all_preferences", barMenu && barMenu->property("count").toInt() == 5);
choose("qualityButton", "qualityMenu", "high");
QFile local(QDir(profile).filePath("fixture.wav"));
if (!local.open(QIODevice::WriteOnly) || local.write(wav) != wav.size()) return 2;
local.close(); music.openLocal(QUrl::fromLocalFile(local.fileName()));
check("local_file_quality_remains_disabled", quick("qualityButton") && !quick("qualityButton")->isEnabled());
music.clearQueue();
check("quality_qml_warnings_absent", warnings.isEmpty());
report.insert("checks", checks); report.insert("warnings", QJsonArray::fromStringList(warnings));
bool passed = true; for (const auto& row : checks) passed = passed && row.toObject().value("ok").toBool();
report.insert("ok", passed); writeReport(); music.finishSession(); return passed ? 0 : 1;
