// Pull-down search navigation: exercise actual pointer/key input and verify
// the retained wall and shared playback state across search and return.
const bool live=args[3]=="search-home-live" || args[3]=="hanging-home-live" || args[3]=="live-bravais";
music.initialize(); music.setReducedMotion(false);
check("native_ready",waitFor([&]{return music.ready()&&!home.loading();},60000));
QMetaObject::invokeMethod(window,"changeMode",Q_ARG(QVariant,QStringLiteral("curtain"))); QTest::qWait(600);
auto* homePage=quick("bravaisHome"); auto* searchPage=quick("homeSearchPage"); auto* pull=quick("homeSearchPull"); auto* field=quick("bravaisField");
if(!homePage||!searchPage||!pull||!field){report.insert("ok",false);writeReport();window->close();return 3;}
check("blackboard_removed",!quick("searchBlackboard")&&!quick("blackboardHandle"));
check("no_home_top_bar",!quick("bravaisBar"));
music.openLocal(QUrl::fromLocalFile(qEnvironmentVariable("BOTTLE_SMOKE_AUDIO")));
check("local_audio_loaded",waitFor([&]{return music.duration()>1000;},5000));
music.setPlaying(false);music.seek(12000);QTest::qWait(250);
const auto originalSource=music.mediaPlayer()->source();
const auto activateWindow=[&]{window->requestActivate();return waitFor([&]{return window->isActive();},2500);};
const auto closeSearch=[&]{QMetaObject::invokeMethod(homePage,"closeSearch");QTest::qWait(350);};
const auto submit=[&](const QString& term,bool append=false){QMetaObject::invokeMethod(homePage,"submitSearch",Q_ARG(QVariant,term),Q_ARG(QVariant,append));};
auto* bravais=homePage;
#include "visual_bravais_checks.h"
for(const auto& size:{QSize(1280,850),QSize(900,620)}) {
    window->resize(size);QTest::qWait(450);activateWindow();
    const auto suffix=QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
    check("wall_reaches_top_"+suffix,std::abs(sceneRect(field).top())<0.5);
    auto* card=quick("homeNowPlaying");
    check("card_owns_controls_"+suffix,sceneRect(card).contains(sceneRect(quick("homeCardPlay")))&&sceneRect(card).contains(sceneRect(quick("bravaisTools")))&&sceneRect(card).contains(sceneRect(quick("homeCardProgress"))));
    for(const auto& spot:{QPointF(6,6),QPointF(20,card->height()-10)}) {
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,card->mapToScene(spot).toPoint());QTest::qWait(80);
        check("card_blocks_cover_click_"+suffix+QString::number(spot.y()),homePage->property("selected").isNull()&&music.mediaPlayer()->source()==originalSource&&!music.playing());
    }
    if(live)QTest::qWait(2500);
    screenshot("home-wall-"+suffix);
    click(window,find("homeSidebarToggle"));QTest::qWait(320);
    check("sidebar_opens_"+suffix,homePage->property("sidebarOpen").toBool());
    click(window,find("bravaisTab_recommend"));QTest::qWait(450);
    check("sidebar_selects_and_retracts_"+suffix,homePage->property("selectedTab")=="recommend"&&!homePage->property("sidebarOpen").toBool());
    // Put the wall at a nondefault camera position so a reset cannot pass.
    field->setProperty("centerX",145.0);field->setProperty("centerY",620.0);QTest::qWait(100);
    const double cameraX=field->property("centerX").toDouble(),cameraY=field->property("centerY").toDouble();
    const auto point=pull->mapToScene(QPointF(pull->width()/2,14)).toPoint();
    QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,point);QTest::mouseMove(window,point+QPoint(0,20),80);QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,point+QPoint(0,20));QTest::qWait(200);
    check("short_pull_cancels_"+suffix,!homePage->property("searchOpen").toBool()&&homePage->property("selected").isNull());
    QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,point);QTest::mouseMove(window,point+QPoint(0,65),100);QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,point+QPoint(0,65));QTest::qWait(450);
    check("pull_opens_search_page_"+suffix,homePage->property("searchOpen").toBool()&&searchPage->isVisible()&&sceneRect(searchPage)==QRectF(0,0,size.width(),size.height()));
    check("search_input_focused_"+suffix,find("bravaisSearchInput")->property("activeFocus").toBool());
    check("search_content_fits_"+suffix,sceneRect(searchPage).contains(sceneRect(quick("homeSearchResults")))&&sceneRect(quick("homeSearchResults")).bottom()<sceneRect(card).top());
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,QPoint(25,size.height()/2));QTest::qWait(100);
    check("search_page_blocks_wall_input_"+suffix,homePage->property("selected").isNull()&&field->property("centerX").toDouble()==cameraX);
    screenshot("search-empty-"+suffix);
    click(window,find("homeSearchBack"));QTest::qWait(350);
    check("back_restores_same_wall_"+suffix,!homePage->property("searchOpen").toBool()&&field->property("centerX").toDouble()==cameraX&&field->property("centerY").toDouble()==cameraY);
    activateWindow();QTest::keyClick(window,Qt::Key_K,Qt::ControlModifier);QTest::qWait(350);
    check("keyboard_opens_search_"+suffix,homePage->property("searchOpen").toBool()&&find("bravaisSearchInput")->property("activeFocus").toBool());
    QTest::keyClick(window,Qt::Key_Escape);QTest::qWait(350);
    check("escape_returns_home_"+suffix,!homePage->property("searchOpen").toBool());
    closeSearch();
}
window->resize(1280,850);QTest::qWait(350);activateWindow();
pull->forceActiveFocus();QTest::keyClick(window,Qt::Key_Space);QTest::qWait(350);
check("focused_pull_space_opens_without_playing",homePage->property("searchOpen").toBool()&&!music.playing());
QLibrary fixture(args[1]);const auto configure=live?nullptr:reinterpret_cast<void(*)(const char*)>(fixture.resolve("EchoTestConfigure"));
if(configure)configure("{\"search_delay\":650}");
auto* input=quick("bravaisSearchInput");input->forceActiveFocus();input->setProperty("text",live?QStringLiteral("夜空中最亮的星"):QStringLiteral("Test"));
QTest::keyClick(window,Qt::Key_Return);QTest::qWait(100);
check("input_submits_search",music.searchTerm()==(live?QStringLiteral("夜空中最亮的星"):QStringLiteral("Test")));
if(!live)check("loading_clears_previous_results",music.searching()&&quick("homeSearchResults")->property("count").toInt()==0);
check("search_results_arrive",waitFor([&]{return !music.searching()&&!music.searchResults().isEmpty();},live?30000:4000));
check("results_belong_to_search_page",quick("homeSearchResults")->property("count").toInt()==music.searchResults().size());
if(live)QTest::qWait(3500);
screenshot("search-results-1280x850");
if(live && music.searchResults().size()<music.searchTotal()) {
    const auto firstCount=music.searchResults().size();
    auto* list=quick("homeSearchResults");QMetaObject::invokeMethod(list,"positionViewAtEnd");QTest::qWait(200);activateWindow();
    click(window,find("homeSearchMore"));
    check("live_pagination_adds_results",waitFor([&]{return !music.searching()&&music.searchResults().size()>firstCount;},30000));
    list->setProperty("contentY",0.0);QTest::qWait(150);
}
if(auto* enqueue=find("homeSearchEnqueue0")){const auto queueSize=music.queue().size();click(window,enqueue);QTest::qWait(100);check("enqueue_does_not_interrupt_current",music.queue().size()==queueSize+1&&music.mediaPlayer()->source()==originalSource&&!music.playing());}
const int resultCount=music.searchResults().size();
closeSearch();click(window,find("bravaisGlobalSearch"));QTest::qWait(350);
check("reopening_retains_query_and_results",input->property("text")==music.searchTerm()&&quick("homeSearchResults")->property("count").toInt()==resultCount);
window->resize(900,620);QTest::qWait(350);screenshot("search-results-900x620");
if(!live) {
    if(configure)configure("{}");submit(QStringLiteral("__bottle_search_error__"));
    check("failure_distinct_from_empty",waitFor([&]{return !music.searching()&&!music.searchError().isEmpty();},3000));
    check("retry_available",quick("homeSearchMore")&&quick("homeSearchMore")->isVisible()&&quick("homeSearchMore")->isEnabled());
    screenshot("search-error");
    if(configure)configure("{\"search_delay\":650}");activateWindow();
    auto* retry=quick("homeSearchMore");const auto retryPoint=sceneRect(retry).center().toPoint();
    QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,retryPoint);QTest::qWait(30);
    QJsonObject retryProbe{{"pressed",retry->property("down").toBool()},{"x",retryPoint.x()},{"y",retryPoint.y()}};
    QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,retryPoint);QTest::qWait(80);
    retryProbe.insert("searching",music.searching());retryProbe.insert("term",music.searchTerm());
    const auto snapshot=reinterpret_cast<char*(*)()>(fixture.resolve("EchoTestSnapshot"));const auto freeString=reinterpret_cast<void(*)(char*)>(fixture.resolve("EchoFreeString"));
    if(snapshot&&freeString){auto* raw=snapshot();retryProbe.insert("requests",QJsonDocument::fromJson(raw).object().value("requests"));freeString(raw);}
    report.insert("retry_probe",retryProbe);
    check("retry_submits_same_query",music.searching()&&music.searchTerm()==QStringLiteral("__bottle_search_error__"));
    waitFor([&]{return !music.searching();},3000);
    if(configure)configure("{}");submit(QStringLiteral("__bottle_no_match__"));waitFor([&]{return !music.searching();},3000);
    check("empty_result_clears_error",music.searchError().isEmpty()&&music.searchResults().isEmpty()&&quick("homeSearchStatus")->property("text").toString().contains(QStringLiteral("没有找到")));
    screenshot("search-no-results");
}
check("search_preserves_paused_source_and_clock",music.mediaPlayer()->source()==originalSource&&!music.playing()&&std::abs(music.position()-12000)<120);
if(!live && configure) {
    // The provider contract accepts HTTP(S) URLs, not a file URL. This probe
    // checks UI dispatch and queue ownership; decoding is covered separately.
    const QUrl fixtureUrl(QStringLiteral("http://127.0.0.1:1/fixture.wav"));
    configure("{}");submit(QStringLiteral("Test"));
    waitFor([&]{return !music.searching()&&!music.searchResults().isEmpty();},4000);
    music.setVolume(0);activateWindow();
    check("play_delegate_ready",waitFor([&]{auto* button=quick("homeSearchPlay0");return button&&button->isVisible()&&button->isEnabled()&&sceneRect(quick("homeSearchResults")).contains(sceneRect(button));},2000));
    click(window,find("homeSearchPlay0"));
    check("result_play_button_uses_search_queue",waitFor([&]{return music.current().value("hash")==music.searchResults().first().toMap().value("hash")&&music.queueIndex()==0&&music.queue().size()==music.searchResults().size()&&music.mediaPlayer()->source()==fixtureUrl;},4000));
    report.insert("play_probe",QJsonObject{{"current",music.current().value("hash").toString()},{"expected",music.searchResults().first().toMap().value("hash").toString()},{"queue_size",int(music.queue().size())},{"source",music.mediaPlayer()->source().toString()},{"expected_source",fixtureUrl.toString()}});
    music.setPlaying(false);
}
closeSearch();music.setReducedMotion(true);QMetaObject::invokeMethod(homePage,"openSearch");QTest::qWait(50);
check("reduced_motion_search_is_immediate",homePage->property("searchOpen").toBool()&&searchPage->y()==0);
closeSearch();
click(window,find("maximizeButton"));QTest::qWait(250);check("maximize_control",window->visibility()==QWindow::Maximized);
click(window,find("maximizeButton"));QTest::qWait(250);check("restore_control",window->visibility()==QWindow::Windowed);
report.insert("checks",checks);report.insert("qml_warnings",QJsonArray::fromStringList(warnings));
bool passed=warnings.isEmpty();for(const auto& item:checks)passed=passed&&item.toObject().value("ok").toBool();
report.insert("ok",passed);writeReport();window->close();return passed?0:1;
