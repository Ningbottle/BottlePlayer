// Grayscale text for the DirectComposition alpha surface. The diagnostic
// BOTTLE_TEXT_AA=subpixel override selects the stock backend. Qt Quick's
// native glyph nodes cannot be asked for that through the font: on Windows
// the DirectWrite engine leaves its glyph format unset unless ClearType is
// wanted, and the text material then defaults to subpixel (Format_A32), so
// QFont::NoSubpixelAntialias changes nothing. This scene graph backend is
// the stock RHI context with two differences: native glyph nodes prefer gray
// antialiasing (Format_A8 masks), and those masks keep DirectWrite's linear
// coverage. installUiAssets() selects it.
#include <QtQuick/private/qsgadaptationlayer_p.h>
#include <QtQuick/private/qsgcontextplugin_p.h>
#include <QtQuick/private/qsgdefaultcontext_p.h>
#ifdef Q_OS_WIN
#include <QtGui/private/qwindowsfontdatabasebase_p.h>
#endif
#include <QtMath>

namespace {

// Qt's DirectWrite engine pushes gray coverage c through 1 - (1 - c)^2.31 so
// gray text keeps ClearType's weight; Chromium's gray text is much lighter
// (Skia's mask gamma stays close to linear for dark text). The curve sits in
// per-thread font engine data, and the threaded render loop rasterizes with
// the render thread's copy, so both threads get the table.
// BOTTLE_TEXT_GAMMA overrides the exponent for comparisons.
void applyGrayTextGamma() {
#ifdef Q_OS_WIN
    static const qreal gamma = [] {
        bool ok = false;
        const qreal value = qEnvironmentVariable("BOTTLE_TEXT_GAMMA").toDouble(&ok);
        return ok && value > 0 ? value : 1.0;
    }();
    if (const auto data = QWindowsFontDatabaseBase::data())
        for (int i = 0; i < 256; ++i)
            data->pow_gamma[i] = uint(qRound(qPow(i / 255.0, gamma) * 2047));
#endif
}

class GrayTextContext final : public QSGDefaultContext {
public:
    explicit GrayTextContext(QObject* parent = nullptr) : QSGDefaultContext(parent) { applyGrayTextGamma(); }

    void renderContextInitialized(QSGRenderContext* renderContext) override {
        applyGrayTextGamma();
        QSGDefaultContext::renderContextInitialized(renderContext);
    }

    QSGGlyphNode* createGlyphNode(QSGRenderContext* rc, QSGTextNode::RenderType renderType,
                                  int renderTypeQuality) override {
        QSGGlyphNode* node = QSGDefaultContext::createGlyphNode(rc, renderType, renderTypeQuality);
        if (renderType == QSGTextNode::NativeRendering)
            node->setPreferredAntialiasingMode(QSGGlyphNode::GrayAntialiasing);
        return node;
    }
};

} // namespace

class BottleGrayTextBackend final : public QSGContextPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QSGContextFactoryInterface_iid FILE "GrayTextBackend.json")

public:
    QStringList keys() const override { return {QStringLiteral("bottle-graytext")}; }
    QSGContext* create(const QString&) const override { return new GrayTextContext; }
    Flags flags(const QString&) const override { return SupportsShaderEffectNode; }
};

#include "GrayTextBackend.moc"
