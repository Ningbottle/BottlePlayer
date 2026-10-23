#include "ui/UiAssets.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPainter>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSvgRenderer>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QtPlugin>

// GrayTextBackend.cpp: the stock scene graph with grayscale native text.
Q_IMPORT_PLUGIN(BottleGrayTextBackend)

IconProvider::IconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage IconProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    const QStringList parts = id.split(QLatin1Char('/'));
    const QColor color(QLatin1Char('#') + parts.value(1, QStringLiteral("000000")));
    const QByteArray stroke = parts.value(2, QStringLiteral("1.75")).toLatin1();
    QString asset = parts.value(0);
    if (asset == QLatin1String("heart") && parts.contains(QStringLiteral("fill"))) asset = QStringLiteral("heart-fill");
    QFile file(QStringLiteral(":/qt/qml/BottleMusic/assets/icons/%1.svg").arg(asset));
    QByteArray svg = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    // Lucide's original SVG geometry is retained; fill is a rendering style.
    if (parts.contains(QStringLiteral("fill"))) svg.replace("fill=\"none\"", "fill=\"currentColor\"");
    if (parts.contains(QStringLiteral("nostroke"))) svg.replace("stroke=\"currentColor\"", "stroke=\"none\"");
    svg.replace("stroke-width=\"2\"", QByteArray("stroke-width=\"") + stroke + "\"");
    svg.replace("currentColor", color.name(QColor::HexRgb).toLatin1()).replace("STROKE", stroke);

    int width = requestedSize.width() > 0 ? requestedSize.width() : requestedSize.height();
    int height = requestedSize.height() > 0 ? requestedSize.height() : width;
    // QQuickImage applies its DPR to sourceSize before requesting a provider.
    // An explicit final-pixel key avoids that extra raster/resize under zoom.
    for (const auto& part : parts) if (part.startsWith("px")) {
        bool valid=false; const int pixels=part.mid(2).toInt(&valid);
        if (valid && pixels>0 && pixels<=2048) width=height=pixels;
    }
    if (width <= 0) width = height = 24;
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) {
        static QMutex mutex; static QSet<QString> warned;
        QMutexLocker lock(&mutex);
        if (!warned.contains(asset)) { warned.insert(asset); qWarning("BottleMusic: missing icon %s", qPrintable(asset)); }
        if (size) *size = image.size();
        return image;
    }
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(color.alphaF());
    renderer.render(&painter);
    if (size) *size = image.size();
    return image;
}

void installUiAssets(QQmlEngine& engine) {
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    // Native glyphs otherwise use ClearType on the Windows surface, while
    // layered text is grayscale. Use grayscale consistently to avoid coloured
    // fringes. Preserve a ClearType A/B switch.
#ifdef Q_OS_WIN
    if (qgetenv("BOTTLE_TEXT_AA") != "cleartype")
#else
    if (qgetenv("BOTTLE_TEXT_AA") == "gray")
#endif
        QQuickWindow::setSceneGraphBackend(QStringLiteral("bottle-graytext"));
    const QDir fonts(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("fonts")));
    for (const QString& name : fonts.entryList({QStringLiteral("*.ttf"), QStringLiteral("*.otf")}, QDir::Files))
        QFontDatabase::addApplicationFont(fonts.filePath(name));
    // Font fallback stacks: Inter → Microsoft YaHei UI → MiSans for CJK UI
    // text. The system YaHei face (the web reference's own stack) holds its
    // stems at small sizes where MiSans's rounder, lighter strokes read soft;
    // MiSans stays in the chain for any glyph YaHei lacks. The bundled WenKai
    // → KaiTi for lyrics, and
    // Mint's newsprint serif chain Libre Caslon Display → EB Garamond →
    // Noto Serif SC → SimSun. Noto Serif SC leads the CJK fallback: under the
    // DirectWrite engine selected below its serif strokes hold up at small
    // sizes, while SimSun's GDI-hinted outlines read thin and uneven there.
    QFont::insertSubstitutions(QStringLiteral("Inter"),
                               {QStringLiteral("Microsoft YaHei UI"), QStringLiteral("MiSans"), QStringLiteral("Microsoft YaHei")});
    QFont::insertSubstitution(QStringLiteral("LXGW WenKai Lite"), QStringLiteral("KaiTi"));
    QFont::insertSubstitutions(QStringLiteral("Libre Caslon Display"),
                               {QStringLiteral("EB Garamond"), QStringLiteral("Noto Serif SC Display"), QStringLiteral("Noto Serif SC"), QStringLiteral("SimSun")});
    QFont::insertSubstitutions(QStringLiteral("EB Garamond"), {QStringLiteral("Noto Serif SC"), QStringLiteral("SimSun")});
    QFont::insertSubstitution(QStringLiteral("Noto Serif SC"), QStringLiteral("SimSun"));
    QFont::insertSubstitution(QStringLiteral("Cascadia Code"), QStringLiteral("Consolas"));
    QFont base(QStringLiteral("Inter"));
    base.setPixelSize(14);
    // Preserve fractional horizontal advances when DPI or UI scale changes.
    // PreferVerticalHinting selects the DirectWrite engine (the Chromium
    // pipeline): vertical-only hinting, fractional X advances, subpixel
    // positioning under ClearType. GDI (PreferFullHinting, excluded from
    // QWindowsFontEngine::useDirectWrite()'s whitelist) snaps stems to the
    // pixel grid but also rounds advances to whole pixels — ~2% wider lines
    // versus the web reference — and reads uneven on serif faces. It stays
    // reachable through BOTTLE_TEXT_ENGINE=gdi for comparison.
    base.setHintingPreference(qgetenv("BOTTLE_TEXT_ENGINE") == "gdi"
        ? QFont::PreferFullHinting : QFont::PreferVerticalHinting);
    // Blink sizes a line from the primary face alone; Qt grows it to the
    // tallest fallback on the line, so 外观 under Caslon or 聆听 under EB
    // Garamond used a lower baseline. Typo metrics put the CJK fallbacks
    // (YaHei UI 0.81/0.25 em, Noto Serif SC 0.88/0.12 em) inside the Latin
    // faces, whose typo and hhea values already agree.
    base.setStyleStrategy(QFont::PreferTypoLineMetrics);
    QGuiApplication::setFont(base);
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
}
