#include "QrImage.h"
#include "qrcodegen.hpp"
#include <QImage>
#include <QBuffer>

QString loginQrImage(const QString& payload) {
    const auto bytes = payload.toUtf8();
    if (bytes.isEmpty() || bytes.size() > 2048 || bytes.contains('\0')) return {};
    try {
        const auto code = qrcodegen::QrCode::encodeText(bytes.constData(), qrcodegen::QrCode::Ecc::MEDIUM);
        constexpr int scale = 6, border = 4;
        const int side = (code.getSize() + border * 2) * scale;
        QImage image(side, side, QImage::Format_RGB32); image.fill(Qt::white);
        for (int y = 0; y < code.getSize(); ++y) for (int x = 0; x < code.getSize(); ++x) {
            if (!code.getModule(x, y)) continue;
            for (int dy = 0; dy < scale; ++dy) for (int dx = 0; dx < scale; ++dx)
                image.setPixel((x + border) * scale + dx, (y + border) * scale + dy, qRgb(22, 33, 28));
        }
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG")) return {};
        return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
    } catch (const std::exception&) { return {}; }
}
