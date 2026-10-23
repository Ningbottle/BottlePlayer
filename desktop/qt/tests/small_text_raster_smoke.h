// Compare the actual UI roles with their native font alternatives, at the
// sizes used by controls and metadata. Keep these captures on the Windows
// RHI path: an offscreen/FreeType image does not test DirectWrite masks.
window->setMinimumSize(QSize(800, 540));
window->resize(1000, 650);
QQmlComponent smallTextComponent(&engine);
smallTextComponent.setData(R"QML(
import QtQuick
import BottleMusic
Rectangle {
    width: 1000; height: 650; z: 10001
    property bool darkInk: true
    color: darkInk ? "white" : "black"
    Column {
        x: 20; y: 16; spacing: 6
        Repeater {
            model: [
                {label: "UI 12", family: Theme.fontUi, size: 12, hint: Font.PreferVerticalHinting},
                {label: "UI 14", family: Theme.fontUi, size: 14, hint: Font.PreferVerticalHinting},
                {label: "Meta 12", family: Theme.fontMeta, size: 12, hint: Font.PreferVerticalHinting},
                {label: "Title 14", family: Theme.fontUi, size: 14, hint: Font.PreferVerticalHinting},
                {label: "YaHei 12 vertical", family: "Microsoft YaHei UI", size: 12, hint: Font.PreferVerticalHinting},
                {label: "YaHei 12 full", family: "Microsoft YaHei UI", size: 12, hint: Font.PreferFullHinting},
                {label: "YaHei 14 full", family: "Microsoft YaHei UI", size: 14, hint: Font.PreferFullHinting},
                {label: "MiSans 12", family: "MiSans", size: 12, hint: Font.PreferVerticalHinting},
                {label: "Serif 12", family: Theme.fontSerif, size: 12, hint: Font.PreferVerticalHinting},
                {label: "Serif 14", family: Theme.fontSerif, size: 14, hint: Font.PreferVerticalHinting},
                {label: "Display 20", family: Theme.fontDisplay, size: 20, hint: Font.PreferVerticalHinting},
                {label: "Display 28", family: Theme.fontDisplay, size: 28, hint: Font.PreferVerticalHinting}
            ]
            Row {
                required property var modelData
                height: 42; spacing: 16
                Text {
                    width: 156; text: modelData.label; color: parent.parent.parent.darkInk ? "black" : "white"
                    font.family: "Inter"; font.pixelSize: 12
                }
                CssText {
                    objectName: "smallTextProbe_" + modelData.label
                    text: "山风等等我  音质设置  播放歌曲  最近收藏  0123456789  Music"
                    color: parent.parent.parent.darkInk ? "black" : "white"
                    font.family: modelData.family; font.pixelSize: modelData.size
                    font.hintingPreference: modelData.hint
                }
            }
        }
    }
}
)QML",QUrl("small-text-raster.qml"));
std::unique_ptr<QObject> smallTextPanel(smallTextComponent.create());
auto* smallTextPanelItem=qobject_cast<QQuickItem*>(smallTextPanel.get());
check("small_text_panel_created",smallTextPanelItem);
if (smallTextPanelItem) {
    smallTextPanelItem->setParentItem(window->contentItem());
    QJsonArray smallFonts;
    walkItems(smallTextPanelItem,[&](QQuickItem* item) {
        if (!item->objectName().startsWith("smallTextProbe_")) return;
        QTextLayout shaped(item->property("text").toString(),item->property("font").value<QFont>());
        shaped.beginLayout(); auto line=shaped.createLine(); line.setLineWidth(2000); shaped.endLayout();
        QJsonArray families;
        for (const auto& run:shaped.glyphRuns()) families.append(QJsonObject{
            {"family",run.rawFont().familyName()},{"size",run.rawFont().pixelSize()},{"style",run.rawFont().styleName()}});
        smallFonts.append(QJsonObject{{"sample",item->objectName()},{"fonts",families}});
    });
    report.insert("small_text_fonts",smallFonts);
    for (bool darkInk:{true,false}) {
        smallTextPanelItem->setProperty("darkInk",darkInk);
        check(darkInk?"small_text_light_capture":"small_text_dark_capture",
            screenshot(darkInk?"small-text-light":"small-text-dark"));
    }
    smallTextPanelItem->setVisible(false);
}

int nativeMasksChanged=0;
for (const auto& sample:nativeSmallMasks)
    if (sample.image!=sample.font.alphaMapForGlyph(sample.glyph,QRawFont::PixelAntialiasing)) ++nativeMasksChanged;
report.insert("native_small_masks_changed",nativeMasksChanged);
check("native_small_glyph_coverage_preserved",nativeSmallMasks.size()>=12 && nativeMasksChanged==0);

// One joined stroke and two open counters must survive at reading sizes.
// Exercise both Text (controls) and CssText (editorial layout), including
// quarter-pixel positions used by centered controls at fractional DPI.
QQmlComponent smallStrokeComponent(&engine);
smallStrokeComponent.setData(R"QML(
import QtQuick
import BottleMusic
Rectangle {
    width: 1000; height: 650; z: 10002
    property bool darkInk: true
    property int glyphSize: 12
    property string glyph: "日"
    property real phase: 0
    property bool css: false
    color: darkInk ? "white" : "black"
    Text {
        x: 20 + parent.phase; y: 20 + parent.phase; visible: !parent.css
        text: parent.glyph; font.family: Theme.fontUi; font.pixelSize: parent.glyphSize
        color: parent.darkInk ? "black" : "white"
    }
    CssText {
        x: 20 + parent.phase; y: 20 + parent.phase; visible: parent.css
        text: parent.glyph; font.family: Theme.fontUi; font.pixelSize: parent.glyphSize
        color: parent.darkInk ? "black" : "white"
    }
}
)QML",QUrl("small-text-stroke.qml"));
std::unique_ptr<QObject> smallStrokePanel(smallStrokeComponent.create());
auto* smallStroke=qobject_cast<QQuickItem*>(smallStrokePanel.get());
check("small_stroke_panel_created",smallStroke);
if (smallStroke) {
    smallStroke->setParentItem(window->contentItem());
    QJsonArray topology;
    for (bool css:{false,true}) for (bool darkInk:{true,false}) for (int size:{12,14}) {
        smallStroke->setProperty("css",css); smallStroke->setProperty("darkInk",darkInk);
        smallStroke->setProperty("glyphSize",size);
        for (const QString& glyph:{QStringLiteral("山"),QStringLiteral("日"),QStringLiteral("O"),QStringLiteral("8")}) {
            smallStroke->setProperty("glyph",glyph);
            bool intact=true;
            QJsonArray phases;
            for (qreal phase:{0.0,0.25,0.5,0.75}) {
                smallStroke->setProperty("phase",phase);
                QTest::qWait(40);
                const auto frame=window->grabWindow();
                const int side=qCeil(80*window->devicePixelRatio());
                const QImage capture=frame.isNull()?QImage():frame.copy(0,0,side,side);
                QVector<bool> ink(capture.width()*capture.height(),false);
                int inkPixels=0;
                bool gray=true;
                for (int y=0;y<capture.height();++y) for (int x=0;x<capture.width();++x) {
                    const QRgb pixel=capture.pixel(x,y);
                    gray=gray && qRed(pixel)==qGreen(pixel) && qGreen(pixel)==qBlue(pixel);
                    const bool covered=darkInk?qGray(pixel)<128:qGray(pixel)>128;
                    ink[y*capture.width()+x]=covered; inkPixels+=covered;
                }
                const auto countRegions=[&](bool foreground) {
                    QVector<bool> visited(ink.size(),false);
                    int count=0;
                    for (int start=0;start<ink.size();++start) {
                        if (visited[start]||ink[start]!=foreground) continue;
                        QVector<int> pending{start}; visited[start]=true;
                        bool edge=false;
                        for (qsizetype i=0;i<pending.size();++i) {
                            const int x=pending[i]%capture.width(),y=pending[i]/capture.width();
                            edge=edge||x==0||y==0||x==capture.width()-1||y==capture.height()-1;
                            for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
                                if (!foreground&&dx&&dy) continue;
                                const int nx=x+dx,ny=y+dy;
                                if (nx<0||ny<0||nx>=capture.width()||ny>=capture.height()) continue;
                                const int next=ny*capture.width()+nx;
                                if (!visited[next]&&ink[next]==foreground) {visited[next]=true;pending.append(next);}
                            }
                        }
                        if (foreground||!edge) ++count;
                    }
                    return count;
                };
                const int joined=countRegions(true),counters=countRegions(false);
                const int expectedCounters=glyph==QStringLiteral("山")?0:glyph==QStringLiteral("O")?1:2;
                const bool backgroundOk=!capture.isNull()
                    && (darkInk?qGray(capture.pixel(0,0))>240:qGray(capture.pixel(0,0))<15);
                const bool ok=backgroundOk&&gray&&joined==1&&counters==expectedCounters&&inkPixels>size;
                intact=intact&&ok;
                phases.append(QJsonObject{{"phase",phase},{"joined",joined},{"counters",counters},{"ink",inkPixels}});
                if (!ok) capture.save(QDir(args[2]).filePath(QStringLiteral("small-stroke-%1-%2-%3-%4-%5.png")
                    .arg(css?"css":"text").arg(darkInk?"light":"dark").arg(size).arg(glyph).arg(phase)));
            }
            const auto name=QStringLiteral("small_stroke_%1_%2_%3_%4")
                .arg(css?"css":"text").arg(darkInk?"light":"dark").arg(size).arg(glyph);
            check(name,intact);
            topology.append(QJsonObject{{"case",name},{"phases",phases}});
        }
    }
    report.insert("small_text_topology",topology);
    smallStroke->setVisible(false);
}
