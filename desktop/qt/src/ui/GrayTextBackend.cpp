// Grayscale text backend, the Windows default; BOTTLE_TEXT_AA=cleartype
// selects the stock backend for A/B comparison. Qt Quick's
// native glyph nodes cannot be asked for gray through the font: on Windows
// the DirectWrite engine leaves its glyph format unset unless ClearType is
// wanted, and the text material then defaults to subpixel (Format_A32), so
// QFont::NoSubpixelAntialias changes nothing. This scene graph backend is
// the stock RHI context with two differences: native glyph nodes prefer gray
// antialiasing (Format_A8 masks), and those masks can be weighted by drawing
// each glyph twice — see WeightedGlyphNode. installUiAssets() selects it.
#include <QtQuick/private/qsgadaptationlayer_p.h>
#include <QtQuick/private/qsgcontextplugin_p.h>
#include <QtQuick/private/qsgdefaultcontext_p.h>
#include <QtQuick/qsgflatcolormaterial.h>
#include <QtQuick/qsgnode.h>
#ifdef Q_OS_WIN
#include <QtGui/private/qwindowsfontdatabasebase_p.h>
#endif
#include <QtMath>

namespace {

// Both GDI and DirectWrite's alphaMapForGlyph apply pow_gamma in Qt 6.8.3:
// coverage c becomes 1 - (1 - c)^gamma. Leave Qt's native 2.31 table intact;
// the former unconditional 1.8 override weakened small, partially covered
// strokes. Only an explicit diagnostic override may change the table.
void applyGrayTextGamma() {
#ifdef Q_OS_WIN
    static const qreal gamma = [] {
        bool ok = false;
        const qreal value = qEnvironmentVariable("BOTTLE_TEXT_GAMMA").toDouble(&ok);
        return ok && qIsFinite(value) && value > 0 ? value : 0;
    }();
    if (gamma <= 0)
        return;
    if (const auto data = QWindowsFontDatabaseBase::data())
        for (int i = 0; i < 256; ++i)
            data->pow_gamma[i] = uint(qRound(qPow(i / 255.0, gamma) * 2047));
#endif
}

// Optional second-pass weighting, engine-independent: compositing the
// identical glyph mask again at opacity w yields alpha' = 1-(1-c)(1-wc)
// (w=1 is the gamma-2 curve). It defaults off: native coverage already has
// gamma correction. Keep this as a diagnostic knob, not synthetic bold.
qreal textWeight() {
    static const qreal weight = [] {
        bool ok = false;
        const qreal value = qEnvironmentVariable("BOTTLE_TEXT_WEIGHT").toDouble(&ok);
        return ok ? qBound<qreal>(0, value, 1) : 0.0;
    }();
    return weight;
}

class WeightedGlyphNode final : public QSGGlyphNode {
public:
    WeightedGlyphNode(QSGGlyphNode *first, QSGGlyphNode *second, qreal weight)
        : m_first(first), m_second(second)
    {
        // QSGInternalTextNode touches geometry() right after update(); a
        // default-constructed node's m_geometry is null. This shell needs a
        // real (empty) geometry and a material — it never draws itself.
        auto *empty = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0, 0);
        empty->setIndexDataPattern(QSGGeometry::StaticPattern);
        empty->setVertexDataPattern(QSGGeometry::StaticPattern);
        setGeometry(empty);
        setFlag(OwnsGeometry);
        m_dummyMaterial.setColor(Qt::transparent);
        setMaterial(&m_dummyMaterial);
        appendChildNode(first);
        auto *weightNode = new QSGOpacityNode;
        weightNode->setOpacity(weight);
        weightNode->appendChildNode(second);
        appendChildNode(weightNode);
    }

    void setGlyphs(const QPointF &position, const QGlyphRun &glyphs) override {
        m_first->setGlyphs(position, glyphs);
        m_second->setGlyphs(position, glyphs);
    }
    void setColor(const QColor &color) override { m_first->setColor(color); m_second->setColor(color); }
    void setStyle(QQuickText::TextStyle style) override { m_first->setStyle(style); m_second->setStyle(style); }
    void setStyleColor(const QColor &color) override { m_first->setStyleColor(color); m_second->setStyleColor(color); }
    void setPreferredAntialiasingMode(AntialiasingMode mode) override {
        m_first->setPreferredAntialiasingMode(mode);
        m_second->setPreferredAntialiasingMode(mode);
    }
    void setRenderTypeQuality(int quality) override {
        m_first->setRenderTypeQuality(quality);
        m_second->setRenderTypeQuality(quality);
    }
    void setBoundingRect(const QRectF &rect) override {
        QSGGlyphNode::setBoundingRect(rect);
        m_first->setBoundingRect(rect);
        m_second->setBoundingRect(rect);
    }
    QPointF baseLine() const override { return m_first->baseLine(); }
    void update() override { m_first->update(); m_second->update(); }
    // preprocess() is not forwarded: the renderer traverses children itself
    // and the glyph nodes carry their own UsePreprocess flag.

private:
    QSGGlyphNode *m_first;
    QSGGlyphNode *m_second;
    QSGFlatColorMaterial m_dummyMaterial;
};

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
        if (renderType != QSGTextNode::NativeRendering)
            return node;
        node->setPreferredAntialiasingMode(QSGGlyphNode::GrayAntialiasing);
        const qreal weight = textWeight();
        if (weight <= 0)
            return node;
        QSGGlyphNode* second = QSGDefaultContext::createGlyphNode(rc, renderType, renderTypeQuality);
        second->setPreferredAntialiasingMode(QSGGlyphNode::GrayAntialiasing);
        return new WeightedGlyphNode(node, second, weight);
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
