#include "Diagnostics.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>

void writeDiagnostic(const QString& directory, const QString& stream, QJsonObject fields) {
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    if (stream != "playback" && stream != "audio" && stream != "requests") return;
    QDir().mkpath(directory);
    QFile file(QDir(directory).filePath(stream + "-diagnostics.jsonl"));
    if (file.size() > 512 * 1024) {
        const auto previous = file.fileName() + ".1";
        QFile::remove(previous); if (!file.rename(previous)) return;
        file.setFileName(QDir(directory).filePath(stream + "-diagnostics.jsonl"));
    }
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    fields.insert("at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    file.write(QJsonDocument(fields).toJson(QJsonDocument::Compact) + '\n');
}
