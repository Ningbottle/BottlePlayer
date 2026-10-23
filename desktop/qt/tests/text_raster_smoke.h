// Windows diagnostic: render the reported title through each Qt text path at
// identical sizes, with the real application fonts and scene graph backend.
QLibrary rasterFixture(args[1]);
rasterFixture.setLoadHints(QLibrary::PreventUnloadHint);
if (!rasterFixture.load()) return 2;
auto rasterConfigure = reinterpret_cast<void(*)(const char*)>(rasterFixture.resolve("EchoTestConfigure"));
if (!rasterConfigure) return 2;
rasterConfigure("{\"feed_count\":30}");
music.setVolume(0); music.setReducedMotion(true); music.initialize();
check("text_raster_ready", waitFor([&] { return music.ready() && !home.loading(); }, 10000));
window->resize(1280, 850);
QMetaObject::invokeMethod(window, "changeMode", Q_ARG(QVariant, QStringLiteral("paper")));
QTest::qWait(400);
auto* rasterTitle = quick("heroTitle");
if (!rasterTitle) return 2;
rasterTitle->setProperty("text", QStringLiteral("山风山风等等我"));
const QFont rasterFont = rasterTitle->property("font").value<QFont>();
QTextLayout rasterLayout(QStringLiteral("山风山风等等我"), rasterFont);
rasterLayout.beginLayout(); auto rasterLine = rasterLayout.createLine(); rasterLine.setLineWidth(1000); rasterLayout.endLayout();
QJsonArray rasterFamilies;
for (const auto& run : rasterLayout.glyphRuns())
    rasterFamilies.append(QJsonObject{{"family",run.rawFont().familyName()}, {"style",run.rawFont().styleName()}, {"size",run.rawFont().pixelSize()}});
check("bundled_display_face",rasterFamilies.size()==1
    && rasterFamilies.first().toObject().value("family")=="Noto Serif SC Display");
report.insert("font",QJsonObject{{"requested",rasterFont.family()}, {"pixel_size",rasterFont.pixelSize()}, {"weight",rasterFont.weight()}, {"hinting",rasterFont.hintingPreference()}, {"runs",rasterFamilies}});
report.insert("dpr",window->devicePixelRatio());
report.insert("default_render_type",rasterTitle->property("renderType").toInt());
screenshot("text-raster-home");
const auto captureTitle=[&](const QString& name) {
    const qreal dpr=window->devicePixelRatio();
    const QRectF region=sceneRect(rasterTitle).adjusted(-8,-8,8,8);
    const QRect pixels(qFloor(region.x()*dpr),qFloor(region.y()*dpr),qCeil(region.width()*dpr),qCeil(region.height()*dpr));
    const auto frame=window->grabWindow();
    return !frame.isNull() && frame.rect().contains(pixels)
        && frame.copy(pixels).save(QDir(args[2]).filePath(name+".png"));
};
check("title_capture_default",captureTitle("title-default"));
// The bundled 山 has a continuous left stem. Count connected ink regions in
// the actual default CssText output: the broken curve path detaches its serif
// from that stem. This checks pixels, not the renderType property.
QQmlComponent strokeComponent(&engine);
strokeComponent.setData(R"QML(
import QtQuick
import BottleMusic
Rectangle {
    width: 1280; height: 850; color: "white"; z: 10000
    property int glyphSize: 37
    property bool darkInk: true
    CssText {
        x: 20; y: 20; text: "山"; color: parent.darkInk ? "black" : "white"
        font.family: Theme.fontDisplay; font.pixelSize: parent.glyphSize
    }
}
)QML",QUrl("text-stroke.qml"));
std::unique_ptr<QObject> strokePanel(strokeComponent.create());
auto* strokePanelItem=qobject_cast<QQuickItem*>(strokePanel.get());
check("stroke_panel_created",strokePanelItem);
if (strokePanelItem) {
    strokePanelItem->setParentItem(window->contentItem());
    for (bool darkInk : {true,false}) for (int size : {28,37,48,64}) {
        strokePanelItem->setProperty("glyphSize",size);
        strokePanelItem->setProperty("darkInk",darkInk);
        strokePanelItem->setProperty("color",QColor(darkInk?Qt::white:Qt::black));
        QTest::qWait(100);
        const int side=qCeil(140*window->devicePixelRatio());
        const auto frame=window->grabWindow();
        const QImage capture=frame.isNull()?QImage():frame.copy(0,0,side,side);
        QVector<bool> ink(capture.width()*capture.height(),false);
        for (int y=0;y<capture.height();++y) for (int x=0;x<capture.width();++x)
            ink[y*capture.width()+x]=darkInk ? qGray(capture.pixel(x,y))<128 : qGray(capture.pixel(x,y))>128;
        int components=0,inkPixels=0;
        for (int start=0;start<ink.size();++start) {
            if (!ink[start]) continue;
            QVector<int> pending{start}; ink[start]=false;
            for (qsizetype i=0;i<pending.size();++i) {
                const int x=pending[i]%capture.width(),y=pending[i]/capture.width();
                for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
                    const int nx=x+dx,ny=y+dy;
                    if(nx<0||ny<0||nx>=capture.width()||ny>=capture.height()) continue;
                    const int next=ny*capture.width()+nx;
                    if(ink[next]) {ink[next]=false;pending.append(next);}
                }
            }
            inkPixels+=pending.size();
            if(pending.size()>1) ++components;
        }
        const auto name=QStringLiteral("continuous_stem_%1_%2").arg(size).arg(darkInk?"light":"dark");
        report.insert(name+"_components",components);
        const bool backgroundOk=!capture.isNull()
            && (darkInk?qGray(capture.pixel(0,0))>240:qGray(capture.pixel(0,0))<15);
        check(name,backgroundOk&&components==1&&inkPixels>size&&inkPixels<ink.size()/2);
        capture.save(QDir(args[2]).filePath(name+".png"));
    }
    strokePanelItem->setVisible(false);
}
for (const auto& variant : {std::pair{2,"curve"}, std::pair{1,"native"}, std::pair{0,"distance"}}) {
    rasterTitle->setProperty("renderType",variant.first);
    rasterTitle->setProperty("renderTypeQuality",104);
    QTest::qWait(250);
    check(QStringLiteral("title_capture_")+variant.second,captureTitle(QStringLiteral("title-")+variant.second));
}
// The following page captures must reflect normal application rendering,
// not the last diagnostic distance-field variant.
rasterTitle->setProperty("renderType",report.value("default_render_type").toInt());
engine.rootContext()->setContextProperty("rasterFontFamily",rasterFont.family());
engine.rootContext()->setContextProperty("rasterFontWeight",int(rasterFont.weight()));
QQmlComponent rasterComponent(&engine);
rasterComponent.setData(R"QML(
import QtQuick
Rectangle {
    width: 1280; height: 850; color: "#ece7dc"; z: 10000
    Row {
        x: 16; y: 12; spacing: 12
        Repeater {
            model: [ { name: "CurveRendering", mode: 2 }, { name: "NativeRendering", mode: 1 }, { name: "QtRendering / 104", mode: 0 } ]
            Column {
                required property var modelData
                width: 404; spacing: 7
                Text { text: parent.modelData.name; font.family: "Inter"; font.pixelSize: 16; color: "#221b12" }
                Repeater {
                    model: [12,13,14,16,20,24,28,32,37,48,64]
                    Item {
                        required property int modelData
                        width: 404; height: modelData * 1.35 + 14
                        Text { x: 0; y: 4; text: parent.modelData; color: "#847460"; font.pixelSize: 11 }
                        Text {
                            x: 28; y: 4; text: "山风等等我"; color: "#221b12"
                            font.family: rasterFontFamily; font.weight: rasterFontWeight; font.pixelSize: parent.modelData
                            renderType: parent.parent.modelData.mode; renderTypeQuality: 104
                        }
                    }
                }
            }
        }
    }
}
)QML",QUrl("text-raster.qml"));
std::unique_ptr<QObject> rasterPanel(rasterComponent.create());
auto* rasterPanelItem=qobject_cast<QQuickItem*>(rasterPanel.get());
check("raster_panel_created",rasterPanelItem);
if (rasterPanelItem) {
    rasterPanelItem->setParentItem(window->contentItem());
    screenshot("text-raster-sizes");
    rasterPanelItem->setVisible(false);
}
#include "small_text_raster_smoke.h"
check("small_text_home_capture",screenshot("small-text-home"));
QMetaObject::invokeMethod(window,"openSettings",Q_ARG(QVariant,QStringLiteral("appearance")));
check("small_text_settings_capture",screenshot("small-text-settings"));
report.insert("checks",checks);
report.insert("qml_warnings",QJsonArray::fromStringList(warnings));
bool rasterPassed=warnings.isEmpty();
for (const auto& item:checks) rasterPassed=rasterPassed&&item.toObject().value("ok").toBool();
report.insert("ok",rasterPassed); writeReport(); music.finishSession(); return rasterPassed?0:1;
