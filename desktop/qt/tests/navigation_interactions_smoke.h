// Real page navigation and input events against an isolated Native fixture.
QLibrary fixture(args[1]); fixture.setLoadHints(QLibrary::PreventUnloadHint);
if (!fixture.load()) return 2;
auto configure = reinterpret_cast<void(*)(const char*)>(fixture.resolve("EchoTestConfigure"));
if (!configure) return 2;
configure("{\"logged\":true,\"feed_count\":30,\"user_playlists_error\":true}");
music.setVolume(0); music.setReducedMotion(true); music.initialize();
check("initial_playlist_failure", waitFor([&] {
    return music.ready() && account.loggedIn() && !home.loading()
        && !library.playlistsLoading() && !library.playlistsError().isEmpty();
}, 10000));
check("failed_playlist_list_empty", library.playlists().isEmpty() && !library.playlistsMore());
window->resize(1280, 850);
window->requestActivate();
check("input_window_active", waitFor([&] { return window->isActive(); }, 2000));
const auto navigate = [&](const char* route) {
    QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, QString::fromLatin1(route)));
    QCoreApplication::processEvents();
};
QMetaObject::invokeMethod(window, "changeMode", Q_ARG(QVariant, QStringLiteral("paper")));
QCoreApplication::processEvents();
click(window, find("libraryEntry"));
auto* directory = quick("sidebarPlaylistsAll");
check("failed_playlist_directory_reachable", directory && directory->isVisible());
click(window, directory);
check("failed_playlist_directory_opens", window->property("route").toString() == "library"
    && quick("libraryRefresh") && quick("libraryRefresh")->isVisible() && quick("libraryRefresh")->isEnabled());
configure("{\"logged\":true,\"feed_count\":30}");
click(window, find("libraryRefresh"));
check("playlist_refresh_recovers", waitFor([&] {
    return !library.playlistsLoading() && library.playlistsError().isEmpty() && library.playlists().size() == 2;
}, 3000));
navigate("home");
directory = quick("sidebarPlaylistsAll");
check("small_playlist_directory_reachable", directory && directory->isVisible() && !library.playlistsMore());

// Follow the Tab chain from the arrow controls, then activate the current card.
for (const auto key : {Qt::Key_Space, Qt::Key_Return, Qt::Key_Enter}) {
    navigate("home");
    auto* ring = quick("playlistCarousel");
    auto* scroller = quick("homeScroller");
    auto* carousel = quick("homeCarousel");
    const auto prefix = QStringLiteral("carousel_key_%1_").arg(int(key));
    check(prefix + "ready", waitFor([&] {
        return ring && ring->property("count").toInt() >= 2 && ring->property("currentItem").value<QQuickItem*>();
    }, 2000));
    if (!ring || !scroller || !carousel) continue;
    const qreal top = carousel->mapToItem(scroller, QPointF()).y() + scroller->property("contentY").toDouble();
    scroller->setProperty("contentY", qMax(qreal(0), top - 24));
    QCoreApplication::processEvents();
    auto* next = quick("carouselNext");
    if (!next) { check(prefix + "arrow", false); continue; }
    next->forceActiveFocus(Qt::TabFocusReason);
    const int before = ring->property("currentIndex").toInt();
    QTest::keyClick(window, Qt::Key_Space);
    check(prefix + "arrow_changes_selection", waitFor([&] { return ring->property("currentIndex").toInt() != before; }, 1000));
    auto* card = ring->property("currentItem").value<QQuickItem*>();
    const auto collection = card ? card->property("modelData").toMap() : QVariantMap{};
    for (int i = 0; card && !card->hasActiveFocus() && i < 20; ++i) QTest::keyClick(window, Qt::Key_Tab);
    const bool focused = card && card->hasActiveFocus();
    check(prefix + "tab_reaches_current_card", focused);
    // Avoid sending Space to an unrelated control if focus traversal failed.
    if (focused) QTest::keyClick(window, key);
    check(prefix + "opens_selected_playlist", window->property("route").toString() == "playlist"
        && library.publicPlaylist() && library.playlistId() == collection.value("id").toString());
}

// The button delegates must still yield a horizontal drag to PathView.
navigate("home");
auto* ring = quick("playlistCarousel");
auto* carousel = quick("homeCarousel");
auto* pointerScroller = quick("homeScroller");
if (ring && carousel && pointerScroller) {
    const qreal top = carousel->mapToItem(pointerScroller, QPointF()).y() + pointerScroller->property("contentY").toDouble();
    pointerScroller->setProperty("contentY", qMax(qreal(0), top - 24));
    auto* card = ring->property("currentItem").value<QQuickItem*>();
    if (card) {
        const int before = ring->property("currentIndex").toInt();
        const QPoint start = card->mapToScene(QPointF(card->width() / 2, card->height() / 2)).toPoint();
        const QPoint distance(qRound(ring->width() * 0.4), 0);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
        for (int i = 1; i <= 6; ++i) QTest::mouseMove(window, start + distance * i / 6, 20);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start + distance);
        check("carousel_drag_changes_selection", waitFor([&] {
            return ring->property("currentIndex").toInt() != before && !ring->property("moving").toBool();
        }, 5000));
        check("carousel_drag_does_not_open", window->property("route").toString() == "home");
        card = ring->property("currentItem").value<QQuickItem*>();
        const auto collection = card ? card->property("modelData").toMap() : QVariantMap{};
        click(window, card);
        check("carousel_pointer_opens_selected_playlist", window->property("route").toString() == "playlist"
            && library.playlistId() == collection.value("id").toString());
    } else check("carousel_pointer_card_present", false);
} else check("carousel_pointer_page_ready", false);

// The newspaper keeps metadata, artwork and actions separate at desktop sizes.
navigate("home");
for (const auto size : {QSize(900,620), QSize(1100,740), QSize(1280,850), QSize(1600,900)}) {
    window->resize(size); appearance.setSidebarWidth(232); QTest::qWait(150);
    const auto suffix = QStringLiteral("_%1x%2").arg(size.width()).arg(size.height());
    auto* feature = quick("paperFeature"); auto* art = quick("heroArtwork");
    auto* title = quick("heroTitle"); auto* artist = quick("heroArtist");
    auto* primary = quick("heroPlay"); auto* details = quick("heroOpenPlayer");
    const auto box = sceneRect(feature).adjusted(-1,-1,1,1);
    check("paper_feature_content_fits"+suffix, feature && art && title && artist && primary && details
        && box.contains(sceneRect(art)) && box.contains(sceneRect(title)) && box.contains(sceneRect(artist))
        && box.contains(sceneRect(primary)) && box.contains(sceneRect(details)));
    check("paper_metadata_separate_from_art"+suffix, title && art && sceneRect(title).left()-sceneRect(art).right() >= 19
        && sceneRect(artist).top()-sceneRect(title).bottom() >= 7);
    check("paper_actions_aligned"+suffix, primary && details && primary->height() >= 40 && details->height() >= 40
        && qAbs(sceneRect(primary).top()-sceneRect(details).top()) < 1 && sceneRect(details).left()-sceneRect(primary).right() >= 11);
    const auto dock = sceneRect(quick("paperPlayerDock"));
    QList<QRectF> buttons; bool dockFits = true;
    for (const char* name : {"prevButton","playButton","nextButton","lyricsButton","qualityButton","queueButton"}) {
        auto* button = quick(name); const auto rect = sceneRect(button);
        dockFits = dockFits && button && rect.height() >= 32 && dock.contains(rect);
        for (const auto& other : buttons) dockFits = dockFits && !rect.intersects(other);
        buttons.append(rect);
    }
    check("paper_transport_targets_separate"+suffix,dockFits);
    auto* settingsButton = quick("settingsButton");
    check("paper_sidebar_footer_reachable"+suffix, settingsButton && settingsButton->height() >= 34
        && sceneRect(quick("paperSidebar")).contains(sceneRect(settingsButton)));
}
window->resize(1280,850); appearance.setSidebarWidth(360); QTest::qWait(150);
auto* feature = quick("paperFeature");
if (feature) {
    auto track = feature->property("track").toMap();
    track["title"] = QStringLiteral("山风山风等等我（很长的现场演唱版本 / Long title）");
    track["artist"] = QStringLiteral("歌手甲、歌手乙、歌手丙、歌手丁");
    track["album"] = QStringLiteral("另一张专辑的独立信息");
    feature->setProperty("track",track); QTest::qWait(150);
    check("paper_long_metadata_stays_inside", sceneRect(feature).contains(sceneRect(quick("heroTitle")))
        && sceneRect(quick("heroArtist")).top() >= sceneRect(quick("heroTitle")).bottom()+7
        && sceneRect(quick("heroPlay")).top() > sceneRect(quick("heroArtist")).bottom());
    check("paper_album_separate_from_artist", quick("heroAlbum") && quick("heroAlbum")->isVisible()
        && sceneRect(quick("heroAlbum")).top() >= sceneRect(quick("heroArtist")).bottom()+7);
} else check("paper_long_metadata_present",false);
appearance.setSidebarWidth(232); QTest::qWait(100);
auto* heroPlay = quick("heroPlay");
if (heroPlay) heroPlay->forceActiveFocus(Qt::TabFocusReason);
QTest::keyClick(window,Qt::Key_Tab);
check("paper_tab_reaches_details", quick("heroOpenPlayer") && quick("heroOpenPlayer")->hasActiveFocus());
if (quick("heroOpenPlayer") && quick("heroOpenPlayer")->hasActiveFocus()) QTest::keyClick(window,Qt::Key_Space);
check("paper_details_opens_player",waitFor([&] { return window->property("route").toString() == "player"; },1000));
navigate("home");
heroPlay = quick("heroPlay");
const auto heroHash = quick("paperFeature") ? quick("paperFeature")->property("track").toMap().value("hash").toString() : QString{};
QSignalSpy featuredAction(quick("paperFeature"), SIGNAL(play()));
if (heroPlay) { heroPlay->forceActiveFocus(Qt::TabFocusReason); QTest::keyClick(window,Qt::Key_Space); }
check("paper_play_key_selects_featured_track", waitFor([&] { return featuredAction.count() == 1 && !heroHash.isEmpty() && music.queueIndex() >= 0 && music.current().value("hash").toString() == heroHash; },1000));
music.setPlaying(false); music.clearQueue();
if (auto* queue = quick("queueButton")) { queue->forceActiveFocus(Qt::TabFocusReason); QTest::keyClick(window,Qt::Key_Space); }
check("paper_queue_key_does_not_start_playback", window->property("listeningOpen").toBool() && music.queueIndex() < 0);
window->setProperty("listeningOpen",false);
if (auto* statsButton = quick("statsEntry")) { statsButton->forceActiveFocus(Qt::TabFocusReason); QTest::keyClick(window,Qt::Key_Space); }
check("paper_navigation_key_does_not_start_playback",window->property("route").toString() == "stats" && music.queueIndex() < 0);
navigate("home");
music.setReducedMotion(false);
auto* scroller = quick("homeScroller");
auto* magnet = find("homeMagnet");
check("magnet_page_ready", scroller && magnet);
const auto wheelAt = [&](QPoint pixels, QPoint angle) {
    const QPoint pos = scroller->mapToScene(QPointF(scroller->width() - 24, scroller->height() / 2)).toPoint();
    QWheelEvent event(pos, window->mapToGlobal(pos), pixels, angle, Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    QCoreApplication::sendEvent(window, &event);
    QCoreApplication::processEvents();
};
if (scroller && magnet) {
    scroller->setProperty("contentY", 0.0);
    scroller->setProperty("magnetFrom", 0.0);
    scroller->setProperty("contentY", 180.0);
    check("magnet_starts_after_scroll_idle", waitFor([&] { return magnet->property("running").toBool(); }, 1000));
    const qreal before = scroller->property("contentY").toDouble();
    wheelAt(QPoint(0, -48), QPoint(0, -120));
    const qreal after = scroller->property("contentY").toDouble();
    check("pixel_delta_cancels_magnet", !magnet->property("running").toBool());
    check("pixel_delta_applied_after_cancel", qAbs(after - before - 48) <= 1);
    check("magnet_restarts_after_new_idle", waitFor([&] { return magnet->property("running").toBool(); }, 1000));
    wheelAt({}, QPoint(0, 120));
    check("angle_delta_cancels_magnet", !magnet->property("running").toBool());
}
check("navigation_qml_warnings_absent", warnings.isEmpty());
report.insert("checks", checks); report.insert("warnings", QJsonArray::fromStringList(warnings));
bool passed = true;
for (const auto& row : checks) passed = passed && row.toObject().value("ok").toBool();
report.insert("ok", passed); writeReport(); music.finishSession(); return passed ? 0 : 1;
