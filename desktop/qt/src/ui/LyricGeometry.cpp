#include "LyricGeometry.h"
#include <QTextLayout>
#include <algorithm>

QVariantList LyricGeometry::shape(const QString& text, const QFont& font, qreal width, qreal lineHeight) const {
    QVariantList result;
    if (text.size() > 16384 || width <= 0 || lineHeight <= 0) return result;
    QTextLayout layout(text, font);
    QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); layout.setTextOption(option);
    layout.beginLayout();
    for (int number = 0; number < 256; ++number) {
        auto line = layout.createLine(); if (!line.isValid()) break;
        line.setLineWidth(width);
        QVariantList positions;
        const int begin = line.textStart(), end = begin + line.textLength();
        for (int i = begin; i <= end; ++i) positions.append(line.cursorToX(i));
        result.append(QVariantMap{{"start", begin}, {"end", end}, {"y", number * lineHeight}, {"height", lineHeight}, {"positions", positions}});
    }
    layout.endLayout(); return result;
}
