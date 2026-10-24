// One shell, one queue and one playback state, including legacy profiles.
QLibrary shellFixture(args[1]); shellFixture.setLoadHints(QLibrary::PreventUnloadHint);
if (!shellFixture.load()) return 2;
const auto shellConfigure = reinterpret_cast<void(*)(const char*)>(shellFixture.resolve("EchoTestConfigure"));
if (!shellConfigure) return 2;
shellConfigure("{\"feed_count\":30}");
music.setVolume(0); music.setReducedMotion(true); music.initialize();
check("shell_core_ready", waitFor([&] { return music.ready() && !home.loading() && music.feed().size() == 30; }, 10000));
check("legacy_profile_opens_home", window->property("route") == "home" && find("paperPage")
    && quick("paperSidebar")->isVisible() && quick("paperPlayerDock")->isVisible());
check("presentation_navigation_removed", !find("stageEntry") && !find("bravaisHome") && !find("posterWallPage"));
QSettings legacyAppearance(QDir(profile).filePath("appearance.ini"),QSettings::IniFormat);
check("legacy_metadata_preserved", legacyAppearance.value("presentation").toString() == "curtain");

// Local silence and original sidecar text exercise the real decoder and clock.
const auto clip = QDir(profile).filePath("Quiet Shell");
QByteArray shellWav(44 + 8000 * 2 * 60, '\0');
memcpy(shellWav.data(), "RIFF", 4); memcpy(shellWav.data()+8, "WAVEfmt ", 8); memcpy(shellWav.data()+36, "data", 4);
qToLittleEndian<quint32>(quint32(shellWav.size()-8),shellWav.data()+4);
qToLittleEndian<quint32>(16,shellWav.data()+16);
qToLittleEndian<quint16>(1,shellWav.data()+20); qToLittleEndian<quint16>(1,shellWav.data()+22);
qToLittleEndian<quint32>(8000,shellWav.data()+24); qToLittleEndian<quint32>(16000,shellWav.data()+28);
qToLittleEndian<quint16>(2,shellWav.data()+32); qToLittleEndian<quint16>(16,shellWav.data()+34);
qToLittleEndian<quint32>(quint32(shellWav.size()-44),shellWav.data()+40);
QFile shellAudio(clip+".wav");
check("shell_audio_fixture_created",shellAudio.open(QIODevice::WriteOnly) && shellAudio.write(shellWav)==shellWav.size());
shellAudio.close();
QFile shellLyrics(clip+".lrc");
check("shell_lyric_fixture_created",shellLyrics.open(QIODevice::WriteOnly)
    && shellLyrics.write("[00:00.00]A quiet room\n[00:08.00]The music stays with us\n[00:16.00]A page turns\n")>0);
shellLyrics.close();
music.openLocal(QUrl::fromLocalFile(clip+".wav"));
check("shell_media_loaded",waitFor([&]{return music.duration()>50000 && music.lyrics().size()==3;},5000));
music.setPlaying(false); music.seek(12000);
check("shell_queue_appended",music.enqueueTrack(music.feed().first().toMap()) && music.queue().size()==2);
const auto shellSource=music.mediaPlayer()->source();
const auto shellTrack=music.current().value("hash");
const auto samePlayback=[&](const QString& step) {
    check(step+"_preserves_playback",music.mediaPlayer()->source()==shellSource && music.current().value("hash")==shellTrack
        && music.queue().size()==2 && music.queueIndex()==0 && !music.playing() && std::abs(music.position()-12000)<150);
};
const auto shellScreen=window->screen()->availableGeometry().size();
window->resize(qMax(900,qMin(1280,shellScreen.width())),qMax(620,qMin(850,shellScreen.height())));
window->requestActivate(); QTest::qWait(200);
screenshot("01-home");

QTest::keyClick(window,Qt::Key_K,Qt::ControlModifier); QTest::qWait(100);
auto* shellSearch=quick("searchInput");
check("shared_search_keyboard_focus",shellSearch && shellSearch->hasActiveFocus());
if (shellSearch) shellSearch->setProperty("text","Test");
QTest::keyClick(window,Qt::Key_Return);
check("shared_search_returns_results",waitFor([&]{return window->property("route")=="search" && !music.searching() && !music.searchResults().isEmpty();},5000));
samePlayback("search");
QTest::keyClick(window,Qt::Key_K,Qt::ControlModifier); QTest::qWait(100);
QTest::keyClick(window,Qt::Key_Space);
check("typing_space_does_not_play",shellSearch && shellSearch->hasActiveFocus() && !music.playing());

QMetaObject::invokeMethod(window,"openSettings",Q_ARG(QVariant,QStringLiteral("appearance"))); QTest::qWait(200);
check("alternate_home_options_removed",find("settingsPage") && !find("settingsOpenPaper") && !find("settingsOpenCurtain"));
check("appearance_controls_retained",find("appearanceMode") && find("lyricFontControl") && find("coverModeControl"));
samePlayback("settings"); screenshot("02-settings");
click(window,find("settingsClose"));
check("settings_returns_to_search",window->property("route")=="search");
click(window,find("playerCoverButton")); QTest::qWait(200);
check("cover_opens_player",window->property("route")=="player" && find("playerPage"));
samePlayback("player");
if (auto* page=find("playerPage")) QMetaObject::invokeMethod(page,"wakeTools");
screenshot("03-player");

const auto queueRows=[&] {
    auto* panel=find("queuePanel");
    return panel ? engine.newQObject(panel).property("shown").property("length").toInt() : -1;
};
click(window,find("queueButton")); QTest::qWait(200);
check("player_opens_shared_queue",find("queuePanel") && queueRows()==2 && !find("listeningPanel"));
if (auto* input=find("queueSearch")) input->setProperty("text","Quiet Shell");
check("shared_queue_filters",waitFor([&]{return queueRows()==1;},1000));
samePlayback("filtered_queue");
click(window,find("queueClose"));
check("queue_unloads_after_close",waitFor([&]{return !find("queuePanel");},1000));
if (auto* page=find("playerPage")) QMetaObject::invokeMethod(page,"wakeTools");
const QRect shellGeometry(window->position(),window->size());
const auto shellVisibility=window->visibility();
click(window,find("playerFullscreenButton")); QTest::qWait(200);
check("focus_is_same_window",window->property("immersive").toBool() && find("immersivePlayerPage")
    && !find("playerPage") && window->visibility()==shellVisibility && QRect(window->position(),window->size())==shellGeometry);
samePlayback("focus"); screenshot("04-focus");
click(window,find("immersiveQueueButton")); QTest::qWait(200);
check("focus_uses_shared_queue",find("queuePanel") && queueRows()==2 && !find("listeningPanel"));
check("focus_queue_material",queueMaterialOk());
samePlayback("focus_queue");
window->resize(900,620); QTest::qWait(200);
check("focus_queue_fits_minimum",find("queuePanelCard")
    && QRectF(0,0,window->width(),window->height()).contains(sceneRect(quick("queuePanelCard"))));
QTest::keyClick(window,Qt::Key_Escape); QTest::qWait(200);
check("escape_closes_queue_first",!window->property("listeningOpen").toBool() && window->property("immersive").toBool());
QTest::keyClick(window,Qt::Key_Escape); QTest::qWait(200);
check("escape_restores_player",!window->property("immersive").toBool() && window->property("route")=="player" && find("playerPage"));
samePlayback("focus_exit");
QTest::keyClick(window,Qt::Key_Escape); QTest::qWait(200);
check("player_returns_home",window->property("route")=="home" && find("paperPage"));
samePlayback("home_return");
click(window,find("queueButton")); QTest::qWait(200);
check("home_uses_shared_queue",find("queuePanel") && queueRows()==2);
music.clearQueue();
check("clear_queue_updates_shared_panel",waitFor([&]{return queueRows()==0 && music.queueIndex()<0;},1000));
check("shell_has_no_qml_warnings",warnings.isEmpty());
report.insert("checks",checks); report.insert("qml_warnings",QJsonArray::fromStringList(warnings));
bool shellPassed=warnings.isEmpty(); for (const auto& item:checks) shellPassed=shellPassed && item.toObject().value("ok").toBool();
report.insert("ok",shellPassed);writeReport();window->close();music.finishSession();return shellPassed?0:1;
