{
    QLibrary fixture(args[1]); const auto configure=reinterpret_cast<void(*)(const char*)>(fixture.resolve("EchoTestConfigure"));
    check("fixture_configuration_available",configure!=nullptr);
    for(const auto& size:{QSize(1280,850),QSize(900,620)}) {
        if(size.width()>window->screen()->availableGeometry().width()||size.height()>window->screen()->availableGeometry().height())continue;
        window->resize(size);QTest::qWait(400);const auto suffix=QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
        QMetaObject::invokeMethod(bravais,"chooseTab",Q_ARG(QVariant,QStringLiteral("recommend")));
        QMetaObject::invokeMethod(bravais,"locate",Q_ARG(QVariant,0));QTest::qWait(500);
        const auto focusedBounds=sceneRect(quick("bravaisFocusedTile")),bounds=sceneRect(quick("bravaisField"));
        check("focused_card_fits_viewport_"+suffix,bounds.adjusted(23,23,-23,-23).contains(focusedBounds));
        check("tile_text_on_scrim_focused_"+suffix,tileTypographyOk());
        screenshot("bravais-focused-track-"+suffix);
        auto* badge=quick("bravaisNowPlayingBadge");auto* label=find("bravaisNowPlayingBadge_label");
        check("now_playing_badge_contrast_"+suffix,badge&&label&&contrast(label->property("color").value<QColor>(),badge->property("color").value<QColor>())>=4.5);
        QTest::keyClick(window,Qt::Key_Escape);QTest::qWait(150);
        check("escape_closes_focused_card_"+suffix,bravais->property("selected").isNull());
        for(const auto* tab:{"albums","playlists"}) {QMetaObject::invokeMethod(bravais,"chooseTab",Q_ARG(QVariant,QString::fromLatin1(tab)));QMetaObject::invokeMethod(bravais,"locate",Q_ARG(QVariant,0));QTest::qWait(400);screenshot("bravais-focused-"+QString::fromLatin1(tab)+"-"+suffix);QMetaObject::invokeMethod(bravais,"dismissLayer");}
        QMetaObject::invokeMethod(bravais,"chooseTab",Q_ARG(QVariant,QStringLiteral("recommend")));
        QMetaObject::invokeMethod(bravais,"openSearch");QTest::qWait(300);screenshot("bravais-search-idle-"+suffix);
        check("wall_chrome_ink_contrast_"+suffix,wallInkOk(quick("bravaisBar"),themeObject->property("wallBg").value<QColor>())&&wallInkOk(quick("windowTitlebar"),themeObject->property("wallBg").value<QColor>())&&find("bravaisSearchInput")->property("color")==themeObject->property("wallChromeInk"));
        if(configure)configure("{\"search_delay\":1600}");
        QMetaObject::invokeMethod(bravais,"submitSearch",Q_ARG(QVariant,QStringLiteral("Test")));QTest::qWait(200);screenshot("bravais-search-busy-"+suffix);waitFor([&]{return !music.searching();},4000);screenshot("bravais-search-results-"+suffix);
        if(configure)configure("{}");QMetaObject::invokeMethod(bravais,"submitSearch",Q_ARG(QVariant,QStringLiteral("__bottle_no_match__")));waitFor([&]{return !music.searching();},3000);QTest::qWait(200);
        check("search_empty_shows_centre_state_"+suffix,bravais->property("items").value<QJSValue>().property("length").toInt()==0&&quick("bravaisCentreState")->isVisible()&&find("bravaisStatus")->property("text").toString().startsWith(QStringLiteral("没有找到")));
        screenshot("bravais-search-empty-"+suffix);QMetaObject::invokeMethod(bravais,"closeSearch");
        music.cycleLoopMode();QTest::qWait(200);auto* toast=find("noticeToast");
        check("bravais_toast_wall_"+suffix,toast&&toast->property("color")==themeObject->property("wallChromeSurface")&&toast->property("borderColor").value<QColor>().alpha()==0);
        check("bravais_toast_below_bar_"+suffix,sceneRect(quick("noticeToast")).top()>=48&&sceneRect(quick("noticeToast")).top()<=60&&sceneRect(quick("noticeToast")).right()<=window->width()-15.5);
        screenshot("bravais-toast-"+suffix);
        click(window,find("bravaisQueue"));QTest::qWait(200);check("bravais_queue_notice_clearance_"+suffix,sceneRect(quick("playerNotices")).right()<=sceneRect(quick("queuePanelCard")).left()-8);check("bravais_queue_material_"+suffix,queueMaterialOk());screenshot("bravais-queue-"+suffix);click(window,find("bravaisQueue"));
        click(window,find("bravaisMore"));auto* menu=find("bravaisMoreMenu");
        check("more_menu_anchored_to_button_"+suffix,menu&&std::abs(menu->property("x").toDouble()+menu->property("width").toDouble()-sceneRect(quick("bravaisMore")).right())<=1);
        screenshot("bravais-more-"+suffix);QTest::keyClick(window,Qt::Key_Escape);
        check("escape_closes_more_menu_"+suffix,waitFor([&]{return !menu->property("visible").toBool();},1000));
        click(window,find("bravaisTools"));QTest::qWait(200);
        auto* popup=find("bravaisToolsPopup");check("tools_popup_inside_window_"+suffix,popup&&popup->property("x").toDouble()>=8&&popup->property("x").toDouble()+popup->property("width").toDouble()<=window->width()-8&&popup->property("y").toDouble()>=44);
        check("tools_popup_anchored_to_button_"+suffix,popup&&std::abs(popup->property("x").toDouble()+popup->property("width").toDouble()-sceneRect(quick("bravaisTools")).right())<=1);
        screenshot("bravais-tools-paused-"+suffix);music.setPlaying(true);QTest::qWait(150);screenshot("bravais-tools-playing-"+suffix);music.setPlaying(false);QTest::keyClick(window,Qt::Key_Escape);
        check("escape_closes_tools_popup_"+suffix,waitFor([&]{return !popup->property("visible").toBool();},1000));
        for(const auto* tab:{"mine","favorites"}) {QMetaObject::invokeMethod(bravais,"chooseTab",Q_ARG(QVariant,QString::fromLatin1(tab)));QTest::qWait(200);screenshot("bravais-empty-"+QString::fromLatin1(tab)+"-"+suffix);}
        int emptyButtons=0;bool emptyLabel=false;walkItems(quick("bravaisCentreState"),[&](QQuickItem* item){if(item->isVisible()&&item->inherits("QQuickAbstractButton"))emptyButtons++;if(item->property("text").toString()==QStringLiteral("还没有本地收藏"))emptyLabel=true;});
        check("favorites_empty_has_no_refresh_"+suffix,emptyButtons==0&&emptyLabel);
    }
    music.cycleLoopMode();
    window->resize(900,620);QMetaObject::invokeMethod(bravais,"chooseTab",Q_ARG(QVariant,QStringLiteral("recommend")));QTest::qWait(300);
    if(configure){
        configure("{\"top_playlist_delay\":3000}");home.refresh();QTest::qWait(400);check("bravais_loading_fits_900",home.loading()&&barGeometryOk());screenshot("bravais-status-loading-900");waitFor([&]{return !home.loading();},6000);
        configure("{\"top_playlist_empty\":true}");home.refresh();waitFor([&]{return !home.loading()&&!home.error().isEmpty();},4000);QTest::qWait(300);check("bravais_error_fits_900",!home.error().isEmpty()&&barGeometryOk()&&quick("bravaisRetry")->isVisible());screenshot("bravais-status-error-900");
        configure("{}");home.refresh();waitFor([&]{return !home.loading();},3000);music.setFeed(feed);
    }
    const auto accent=themeObject->property("wallAccent");appearance.setMode("dark");QTest::qWait(300);check("wall_accent_light_independent",accent==themeObject->property("wallAccent"));appearance.setMode("light");
    window->resize(1280,850);QTest::qWait(400);
}
