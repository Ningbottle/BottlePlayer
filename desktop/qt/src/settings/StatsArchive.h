#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <atomic>
#include <functional>

// Only listening records cross this boundary. SQLite connections are read-only
// and thread-local; credentials, device state and caches are never copied.
namespace StatsArchive {
QJsonObject read(const QString& path, QJsonArray& records);
QJsonObject write(const QString& path, const QJsonArray& records);
QJsonObject restore(const QString& source, const QString& currentDatabase, const QString& backup,
                    const std::function<int(const QJsonObject&)>& record, const std::atomic_bool& stopped);
}
