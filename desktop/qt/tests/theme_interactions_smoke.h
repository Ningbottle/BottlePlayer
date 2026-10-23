// Exercise the real swatches and controls with an isolated appearance profile.
QLibrary fixture(args[1]); fixture.setLoadHints(QLibrary::PreventUnloadHint);
if (!fixture.load()) return 2;
auto configure = reinterpret_cast<void(*)(const char*)>(fixture.resolve("EchoTestConfigure"));
if (!configure) return 2;
configure("{\"feed_count\":2}");
music.setVolume(0); music.setReducedMotion(true); music.initialize();
check("theme_core_ready", waitFor([&] { return music.ready() && music.feed().size() == 2 && !home.loading(); }, 10000));
window->resize(1280, 850); window->requestActivate();
QMetaObject::invokeMethod(window, "changeMode", Q_ARG(QVariant, QStringLiteral("paper")));

// Test the shared filled button too; the home has a bespoke play capsule.
QQmlComponent controls(&engine);
controls.setData(R"(
import QtQuick
import BottleMusic
Item {
    width: 180; height: 40
    ActionButton { objectName: "themeFilledButton"; width: 160; height: 34; text: "播放全部"; filled: true }
})", QUrl("qrc:/theme-controls.qml"));
std::unique_ptr<QObject> controlOwner(controls.create(engine.rootContext()));
auto* controlHost = qobject_cast<QQuickItem*>(controlOwner.get());
if (!check("shared_controls_created", controlHost != nullptr)) return 2;
controlHost->setParentItem(window->contentItem()); controlHost->setPosition(QPointF(350, 105));

// Compute contrast independently of Theme's palette-generation helpers.
const auto over = [](QColor fg, QColor bg, double opacity = 1.0) {
    const double a = fg.alphaF() * opacity;
    return QColor::fromRgbF(fg.redF()*a+bg.redF()*(1-a), fg.greenF()*a+bg.greenF()*(1-a), fg.blueF()*a+bg.blueF()*(1-a));
};
const auto ratio = [](QColor a, QColor b) {
    const auto luminance = [](QColor c) {
        const auto linear = [](double v) { return v <= .04045 ? v/12.92 : std::pow((v+.055)/1.055, 2.4); };
        return .2126*linear(c.redF())+.7152*linear(c.greenF())+.0722*linear(c.blueF());
    };
    const double x = luminance(a), y = luminance(b);
    return (std::max(x,y)+.05)/(std::min(x,y)+.05);
};
const auto color = [](QObject* item, const char* property) { return item ? item->property(property).value<QColor>() : QColor{}; };
const auto hueMatches = [](QColor actual, QColor seed) {
    if (seed.hslSaturationF() < .01) return actual.hslSaturationF() < .01;
    const double delta = std::abs(actual.hslHueF()-seed.hslHueF());
    return actual.hslSaturationF() > .03 && std::min(delta, 1-delta) < .025;
};
const auto buttonReadable = [&](QQuickItem* button, const char* inkProperty, const QString& prefix) {
    auto* face = button ? button->property("background").value<QQuickItem*>() : nullptr;
    if (!check(prefix+"_present", button && face && button->isVisible() && button->isEnabled())) return;
    const QPoint center = sceneRect(button).center().toPoint();
    button->setFocus(false); QTest::mouseMove(window, QPoint(320, 90)); QTest::qWait(220);
    const QColor rest = color(face, "color");
    const auto readable = [&] {
        const QColor base = color(window, "color");
        const double opacity = button->opacity();
        return ratio(over(color(button, inkProperty), base, opacity), over(color(face,"color"), base, opacity)) >= 4.5;
    };
    check(prefix+"_rest_contrast", readable());
    QTest::mouseMove(window, center); QTest::qWait(220);
    const QColor hover = color(face, "color");
    check(prefix+"_hover_contrast", readable());
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, center); QTest::qWait(220);
    check(prefix+"_pressed_contrast", readable());
    check(prefix+"_states_distinct", rest != hover && hover != color(face,"color"));
    // Release outside the control so these appearance checks never start audio.
    QTest::mouseMove(window, QPoint(320, 90));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(320, 90));
};
const QStringList presets{"#b0392e", "#c2703d", "#a8862f", "#4f7a4a", "#2f6f7a", "#3d5a99", "#7a4f8f"};
const QStringList seeds = presets + QStringList{"#000000", "#ffffff", "#777777", "#00ff00", "#ffff00"};
for (const auto& mode : {QStringLiteral("light"), QStringLiteral("dark")}) {
    appearance.setMode(mode); appearance.setAccent(""); QTest::qWait(300);
    const QColor defaultPage = color(window, "color"), defaultPane = color(quick("paperSidebar"), "color");
    const QColor defaultBar = color(quick("paperPlayerDock"), "color");
    for (const auto& seed : seeds) {
        const QString prefix = mode+seed.mid(1);
        if (presets.contains(seed)) {
            click(window, find("quickSettingsButton")); auto* popup = find("quickSettings");
            const bool opened = popup && waitFor([&] { return popup->property("opened").toBool(); }, 1000);
            auto* swatch = opened ? findItem(popup->property("contentItem").value<QQuickItem*>(), "quickAccent_"+seed) : nullptr;
            check(prefix+"_swatch_available", swatch != nullptr);
            click(window, swatch);
            check(prefix+"_selection_reaches_controller", appearance.accent() == seed);
            if (popup) QMetaObject::invokeMethod(popup, "close");
        } else appearance.setAccent(seed); // Custom ColorDialog uses this same setter.
        QTest::qWait(300);
        const QColor selected(seed);
        check(prefix+"_surfaces_follow_hue", hueMatches(color(window,"color"),selected)
            && hueMatches(color(quick("paperSidebar"),"color"),selected)
            && hueMatches(color(quick("paperPlayerDock"),"color"),selected));
        bool textReadable = true;
        for (const char* ink : {"text", "text2", "text3"})
            for (const char* surface : {"bg", "bgElevated", "surface", "surfaceHover", "surfaceActive", "paneBg", "barBg"})
                textReadable = textReadable && ratio(color(themeObject,ink),color(themeObject,surface)) >= 4.5;
        check(prefix+"_text_contrast", textReadable);
        check(prefix+"_radio_follows_tone", color(themeObject,"radioAccent") == color(themeObject,"accent"));
        buttonReadable(quick("playButton"), "tint", prefix+"_transport");
        buttonReadable(quick("themeFilledButton"), "ink", prefix+"_filled");
        auto* hero = quick("heroPlay");
        check(prefix+"_hero_matches_transport", hero && color(hero->property("background").value<QQuickItem*>(),"color") == color(quick("playButton"),"primaryFill"));
        if (seed == "#3d5a99" || seed == "#a8862f") {
            controlHost->setVisible(false);
            screenshot("theme-"+prefix);
            controlHost->setVisible(true);
        }
        if (seed == "#3d5a99") {
            click(window, find("quickSettingsButton"));
            auto* popup = find("quickSettings");
            auto* toggle = popup ? qobject_cast<QQuickItem*>(findItem(popup->property("contentItem").value<QQuickItem*>(), "quickCompact")) : nullptr;
            if (toggle && !toggle->property("checked").toBool()) click(window,toggle);
            QTest::qWait(220);
            auto* thumbHost = toggle ? toggle->property("contentItem").value<QQuickItem*>() : nullptr;
            const auto thumbs = thumbHost ? thumbHost->childItems() : QList<QQuickItem*>{};
            check(prefix+"_checked_toggle_readable", toggle && toggle->property("checked").toBool() && !thumbs.isEmpty()
                && ratio(color(thumbs.first(),"color"),color(themeObject,"accent")) >= 4.5);
            if (toggle) click(window,toggle);
            if (popup) QMetaObject::invokeMethod(popup,"close");
        }
    }
    appearance.setAccent(""); QTest::qWait(300);
    check(mode+"_default_restored", color(window,"color") == defaultPage && color(quick("paperSidebar"),"color") == defaultPane && color(quick("paperPlayerDock"),"color") == defaultBar);
}
controlHost->setVisible(false);

// Dark artwork scenes own their luminance, but their furniture follows the hue.
QMetaObject::invokeMethod(window, "changeMode", Q_ARG(QVariant, QStringLiteral("curtain")));
QTest::qWait(250);
for (const auto& seed : presets) {
    appearance.setAccent(seed); QTest::qWait(220);
    check("wall_"+seed+"_background_hue", hueMatches(color(window,"color"),QColor(seed)));
    auto* wall = find("bravaisHome");
    if (wall) QMetaObject::invokeMethod(wall,"openSearch");
    QTest::qWait(100);
    auto* input = quick("bravaisSearchInput");
    check("wall_"+seed+"_search_hue", hueMatches(color(quick("homeSearchBackground"),"color"),QColor(seed)));
    check("wall_"+seed+"_selection_contrast", input && ratio(color(input,"selectedTextColor"),color(input,"selectionColor")) >= 4.5);
    if (wall) QMetaObject::invokeMethod(wall,"closeSearch");
}
QMetaObject::invokeMethod(window,"enterImmersive", Q_ARG(QVariant, false)); QTest::qWait(300);
for (const auto& seed : presets) {
    appearance.setAccent(seed); QTest::qWait(220);
    auto* button = quick("immersivePlayButton");
    auto* face = button ? button->property("background").value<QQuickItem*>() : nullptr;
    check("immersive_"+seed+"_play_follows_tone", face && hueMatches(color(face,"color"),QColor(seed))
        && ratio(color(button,"tint"),color(face,"color")) >= 4.5);
}
QMetaObject::invokeMethod(window,"leaveImmersive");
appearance.setAccent("#3d5a99");
check("theme_preference_flushed", appearance.flushStorage());
AppearanceController reloaded(profile);
check("theme_preference_survives_reload", reloaded.accent() == "#3d5a99" && reloaded.mode() == "dark");
check("theme_qml_warnings_absent", warnings.isEmpty());
report.insert("checks", checks); report.insert("warnings", QJsonArray::fromStringList(warnings));
bool passed = true; for (const auto& row : checks) passed = passed && row.toObject().value("ok").toBool();
report.insert("ok", passed); writeReport(); music.finishSession(); return passed ? 0 : 1;
