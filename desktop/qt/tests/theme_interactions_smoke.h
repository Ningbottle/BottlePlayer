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

// Test shared actions as well as the home and transport controls.
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
// Golden sRGB roles from @radix-ui/colors 3.0.0, independently checked
// against actual window/sidebar/button paint rather than hue interpolation.
const QHash<QString, QStringList> expected {
    {"tomato", {"#fffcfc", "#faf9fb", "#e54d2e", "#181111", "#1a191b", "#e54d2e"}},
    {"orange", {"#fefcfb", "#f9f9f8", "#f76b15", "#17120e", "#191918", "#f76b15"}},
    {"amber", {"#fefdfb", "#f9f9f8", "#ffc53d", "#16120c", "#191918", "#ffc53d"}},
    {"green", {"#fbfefc", "#f7f9f8", "#30a46c", "#0e1512", "#171918", "#30a46c"}},
    {"teal", {"#fafefd", "#f7f9f8", "#12a594", "#0d1514", "#171918", "#12a594"}},
    {"blue", {"#fbfdff", "#f9f9fb", "#0090ff", "#0d1520", "#18191b", "#0090ff"}},
    {"violet", {"#fdfcfe", "#faf9fb", "#6e56cf", "#14121f", "#1a191b", "#6e56cf"}},
    {"gray", {"#fcfcfc", "#f9f9f9", "#8d8d8d", "#111111", "#191919", "#6e6e6e"}}
};
const QHash<QString, QStringList> tintedPanels {
    {"tomato", {"#fff8f7", "#1f1513"}}, {"orange", {"#fff7ed", "#1e160f"}},
    {"amber", {"#fefbe9", "#1d180f"}}, {"green", {"#f4fbf6", "#121b17"}},
    {"teal", {"#f3fbf9", "#111c1b"}}, {"blue", {"#f4faff", "#111927"}},
    {"violet", {"#faf8ff", "#1b1525"}}, {"gray", {"#f9f9f9", "#191919"}}
};
const auto correctSurfaces = [&](bool night) {
    const QString family = themeObject->property("accentFamily").toString();
    const auto roles = expected.value(family);
    const int offset = night ? 3 : 0;
    return roles.size() == 6 && color(window,"color") == QColor(roles[offset])
        && color(quick("paperSidebar"),"color") == QColor(tintedPanels.value(family).value(night ? 1 : 0))
        && color(quick("paperPlayerDock"),"color") == QColor(tintedPanels.value(family).value(night ? 1 : 0))
        && color(themeObject,"bgElevated") == QColor(roles[offset+1])
        && color(themeObject,"accent") == QColor(roles[offset+2]);
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
    check(prefix+"_radix_solid_states", rest != hover && hover == color(face,"color"));
    // Release outside the control so these appearance checks never start audio.
    QTest::mouseMove(window, QPoint(320, 90));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(320, 90));
};
const QStringList presets{"#e54d2e", "#f76b15", "#ffc53d", "#30a46c", "#12a594", "#0090ff", "#6e56cf"};
const QStringList seeds = presets + QStringList{"#000000", "#ffffff", "#777777", "#00ff00", "#ffff00", "#3d5a99", "#b0392e"};
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
        } else appearance.setAccent(seed); // Resolve legacy custom hex preferences without rewriting their saved values.
        QTest::qWait(300);
        check(prefix+"_radix_surface_roles", correctSurfaces(mode == "dark"));
        bool textReadable = true;
        for (const char* ink : {"text", "text2", "text3"})
            for (const char* surface : {"bg", "bgElevated", "surface", "surfaceHover", "surfaceActive", "paneBg", "barBg"})
                textReadable = textReadable && ratio(color(themeObject,ink),color(themeObject,surface)) >= 4.5;
        check(prefix+"_text_contrast", textReadable);
        check(prefix+"_radio_follows_tone", color(themeObject,"radioAccent") == color(themeObject,"accentText"));
        buttonReadable(quick("playButton"), "tint", prefix+"_transport");
        buttonReadable(quick("themeFilledButton"), "ink", prefix+"_filled");
        auto* hero = quick("heroPlay");
        check(prefix+"_hero_matches_transport", hero && color(hero->property("background").value<QQuickItem*>(),"color") == color(quick("playButton"),"primaryFill"));
        if (seed == "#0090ff" || seed == "#ffc53d") {
            controlHost->setVisible(false);
            screenshot("theme-"+prefix);
            controlHost->setVisible(true);
        }
        if (seed == "#0090ff") {
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
    check("wall_"+seed+"_background_hue", color(window,"color") == QColor(expected.value(themeObject->property("accentFamily").toString()).value(3)));
    auto* wall = find("bravaisHome");
    if (wall) QMetaObject::invokeMethod(wall,"openSearch");
    QTest::qWait(100);
    auto* input = quick("bravaisSearchInput");
    check("wall_"+seed+"_search_hue", color(quick("homeSearchBackground"),"color") == color(window,"color"));
    check("wall_"+seed+"_selection_contrast", input && ratio(color(input,"selectedTextColor"),color(input,"selectionColor")) >= 4.5);
    if (wall) QMetaObject::invokeMethod(wall,"closeSearch");
}
QMetaObject::invokeMethod(window,"enterImmersive", Q_ARG(QVariant, false)); QTest::qWait(300);
for (const auto& seed : presets) {
    appearance.setAccent(seed); QTest::qWait(220);
    auto* button = quick("immersivePlayButton");
    auto* face = button ? button->property("background").value<QQuickItem*>() : nullptr;
    check("immersive_"+seed+"_play_follows_tone", face && color(face,"color") == color(themeObject,"wallAccent")
        && ratio(color(button,"tint"),color(face,"color")) >= 4.5);
}
QMetaObject::invokeMethod(window,"leaveImmersive");
appearance.setAccent("#3d5a99");
check("theme_preference_flushed", appearance.flushStorage());
AppearanceController reloaded(profile);
check("theme_preference_survives_reload", reloaded.accent() == "#3d5a99" && reloaded.mode() == "dark");
QMetaObject::invokeMethod(window,"changeMode",Q_ARG(QVariant,QStringLiteral("paper")));
const auto typeLadder = [&](const QString& route) {
    bool valid = true; int textCount = 0;
    walkItems(window->contentItem(),[&](QQuickItem* item) {
        if (!item->isVisible() || item->opacity() == 0 || !sceneRect(item).intersects(QRectF(0,0,window->width(),window->height()))) return;
        if (!item->metaObject()->className() || !QByteArray(item->metaObject()->className()).startsWith("QQuickText")) return;
        const auto font = item->property("font").value<QFont>();
        ++textCount;
        valid = valid && QList<int>{12,14,20,28}.contains(font.pixelSize());
    });
    check(route+"_four_type_sizes_minimum_12",valid && textCount > 5);
};
typeLadder("home");
QMetaObject::invokeMethod(window,"openSettings",Q_ARG(QVariant,QStringLiteral("appearance"))); QTest::qWait(100);
typeLadder("appearance");
check("appearance_has_named_color_choices", find("accentChoice_#0090ff") && find("accentChoice_default"));
check("lyric_choices_use_shared_ladder", find("lyricSizeM") && find("lyricSizeL") && !find("lyricSizeXL"));
check("all_rules_are_half_pixel", qFuzzyCompare(themeObject->property("hairline").toDouble(),.5));
auto* qualityFace = quick("qualityButton") ? quick("qualityButton")->property("background").value<QQuickItem*>() : nullptr;
auto* qualityPen = qualityFace ? qualityFace->property("border").value<QObject*>() : nullptr;
check("fractional_border_not_rounded_by_qt",qualityPen && !qualityPen->property("pixelAligned").toBool()
    && qFuzzyCompare(qualityPen->property("width").toDouble(),.5) && qualityFace->property("antialiasing").toBool());

QMetaObject::invokeMethod(window,"openSettings",Q_ARG(QVariant,QStringLiteral("playback"))); QTest::qWait(100);
typeLadder("playback");
auto* outputSelector = quick("audioOutputDevice");
auto* outputPopup = outputSelector ? outputSelector->property("popup").value<QObject*>() : nullptr;
if (outputPopup) QMetaObject::invokeMethod(outputPopup,"open");
check("output_device_menu_opens", outputPopup && waitFor([&] { return outputPopup->property("opened").toBool(); },1000));
if (outputPopup) QMetaObject::invokeMethod(outputPopup,"close");
QMetaObject::invokeMethod(window,"navigate",Q_ARG(QVariant,QStringLiteral("player"))); QTest::qWait(100);
typeLadder("player");
check("no_default_glow_or_press_zoom",themeObject->property("pressScale").toDouble()==1 && themeObject->property("heroGlowOpacity").toDouble()==0);
check("theme_qml_warnings_absent", warnings.isEmpty());
report.insert("checks", checks); report.insert("warnings", QJsonArray::fromStringList(warnings));
bool passed = true; for (const auto& row : checks) passed = passed && row.toObject().value("ok").toBool();
report.insert("ok", passed); writeReport(); music.finishSession(); return passed ? 0 : 1;
