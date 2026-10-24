// Real application transitions with a local image service and isolated profile.
QLibrary renderFixture(args[1]); renderFixture.setLoadHints(QLibrary::PreventUnloadHint);
if (!renderFixture.load()) return 2;
auto renderConfigure = reinterpret_cast<void(*)(const char*)>(renderFixture.resolve("EchoTestConfigure"));
if (!renderConfigure) return 2;
renderConfigure("{\"feed_count\":30}");
music.setVolume(0); music.setReducedMotion(false); music.initialize();
check("render_core_ready", waitFor([&] { return music.ready() && !home.loading(); }, 10000));
QImage testCover(1200, 1200, QImage::Format_RGB32); testCover.fill(QColor("#426c59"));
QByteArray png; QBuffer pngBuffer(&png); pngBuffer.open(QIODevice::WriteOnly); testCover.save(&pngBuffer, "PNG");
QTcpServer covers; if (!covers.listen(QHostAddress::LocalHost)) return 2;
QObject::connect(&covers, &QTcpServer::newConnection, &app, [&] {
    auto* socket = covers.nextPendingConnection();
    QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, png] {
        if (socket->property("sent").toBool()) return;
        const auto request = socket->property("request").toByteArray() + socket->readAll();
        socket->setProperty("request", request);
        if (!request.contains("\r\n\r\n")) return;
        socket->setProperty("sent", true);
        const int delay = request.contains("/timeout") ? 700 : request.contains("/slow") ? 200 : 0;
        QTimer::singleShot(delay, socket, [socket, png] {
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: " + QByteArray::number(png.size()) + "\r\nConnection: close\r\n\r\n" + png);
            socket->disconnectFromHost();
        });
    });
});
const auto imageBase = QString("http://127.0.0.1:%1/").arg(covers.serverPort());
const auto navigate = [&](const char* route) { QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, QString::fromLatin1(route))); };
const auto selectCover = [&](int id, const QString& path) {
    music.playTracks({QVariantMap{{"hash", QString::number(id,16).rightJustified(32,'0')}, {"title","Render fixture"}, {"image",imageBase+path}}}, 0);
    music.setPlaying(false);
};
const auto fly = [&] { QMetaObject::invokeMethod(window,"flyCoverToPlayer"); };
QMetaObject::invokeMethod(window,"navigate",Q_ARG(QVariant,QStringLiteral("home")));
check("sidebar_single_library_entry", !find("playerEntry") && !find("sidebarPlaylists") && find("libraryEntry"));
selectCover(801,"fast.png"); QTest::qWait(300); fly();
auto* flight = find("coverFlight");
if (!flight) { check("cover_flight_exists", false); report.insert("checks",checks); report.insert("ok",false); writeReport(); return 1; }
check("cover_flight_started", flight && waitFor([&] { return flight->property("visible").toBool(); }, 1500));
const int flightBucket = flight ? flight->property("bucket").toInt() : 0;
bool bucketStable = flightBucket > 0;
for (int i=0;i<3;++i) { QTest::qWait(20); bucketStable = bucketStable && flight->property("bucket").toInt()==flightBucket; }
check("cover_flight_image_bucket_stable", bucketStable);
check("cover_flight_completed", waitFor([&] { return !window->property("coverFlying").toBool(); }, 1500));
auto* playerPage = find("playerPage"); auto* artwork = quick("playerCoverArt");
const auto target = flight ? flight->property("target").toRectF() : QRectF{};
const auto settled = sceneRect(artwork);
check("cover_flight_settled_slot", artwork && qAbs(target.x()-settled.x())<1 && qAbs(target.y()-settled.y())<1);
check("cover_restored_after_flight", playerPage && !playerPage->property("hideCover").toBool());

navigate("home"); selectCover(802,"slow-navigation.png"); fly();
check("cover_pending_without_hiding_destination", waitFor([&] { auto* page=find("playerPage"); return flight->property("pending").toBool() && page && !page->property("hideCover").toBool(); }, 1000));
navigate("home"); QTest::qWait(500);
check("navigation_cancels_pending_cover", !window->property("coverFlying").toBool() && !flight->property("visible").toBool() && !flight->property("pending").toBool());
navigate("player"); QTest::qWait(100); playerPage=find("playerPage");
check("return_to_player_shows_cover", playerPage && !playerPage->property("hideCover").toBool());

navigate("home"); selectCover(803,"slow-track.png"); fly();
check("cover_pending_before_track_change", waitFor([&] { return flight->property("pending").toBool(); }, 1000));
selectCover(804,"fast-next.png"); QTest::qWait(450);
playerPage=find("playerPage");
check("track_change_cancels_cover", !window->property("coverFlying").toBool() && !flight->property("visible").toBool() && playerPage && !playerPage->property("hideCover").toBool());

navigate("home"); selectCover(806,"slow-layout.png"); fly();
check("cover_pending_before_layout_change", waitFor([&] { return flight->property("pending").toBool(); }, 1000));
const auto previousCards = layoutModel.cards();
const bool layoutMoved = layoutModel.move("lyrics", 0, 0) && layoutModel.cards() != previousCards;
playerPage = find("playerPage");
check("layout_change_cancels_cover", layoutMoved && !window->property("coverFlying").toBool() && !flight->property("visible").toBool() && playerPage && !playerPage->property("hideCover").toBool());
QTest::qWait(450); layoutModel.reset();

navigate("home"); selectCover(805,"timeout.png"); fly(); QTest::qWait(1000); playerPage=find("playerPage");
check("slow_image_timeout_restores_cover", !window->property("coverFlying").toBool() && !flight->property("pending").toBool() && playerPage && !playerPage->property("hideCover").toBool());
navigate("home"); music.setReducedMotion(true); fly(); QTest::qWait(250);
check("reduced_motion_skips_cover_flight", window->property("route").toString()=="player" && !window->property("coverFlying").toBool());
check("render_qml_warnings_absent",warnings.isEmpty());
report.insert("dpr",window->devicePixelRatio()); report.insert("checks",checks); report.insert("warnings",QJsonArray::fromStringList(warnings));
bool renderOk=true; for(const auto& row:checks) renderOk=renderOk&&row.toObject().value("ok").toBool();
report.insert("ok",renderOk); screenshot("render-review"); writeReport(); music.finishSession(); return renderOk?0:1;
