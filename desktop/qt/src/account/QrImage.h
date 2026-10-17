#pragma once
#include <QString>
// QR payload stays in memory; nothing is sent to an image-generation service.
QString loginQrImage(const QString& payload);
