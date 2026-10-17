#include "StatsArchive.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QUuid>
#include <QSet>
#include <QDateTime>
#include <cmath>

namespace {
constexpr qint64 maximumBytes = 64 * 1024 * 1024;
constexpr int maximumRecords = 100000;
QJsonObject failure(const QString& message) { return {{"status", 0}, {"error", message}}; }
struct ReadConnection {
    QString name = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
    explicit ReadConnection(const QString& path) { db.setDatabaseName(path); db.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000"); }
    ~ReadConnection() { db.close(); db = {}; QSqlDatabase::removeDatabase(name); }
};
const QStringList fields{"session_id", "song_hash", "song_name", "singer_name", "album_id", "album_name", "cover_url", "duration_seconds", "completed", "listened_seconds", "quality", "played_at"};
QJsonObject canonical(QJsonObject row) {
    QJsonObject clean;
    for (const auto& key : fields) {
        const auto value = row.value(key);
        if (key == "completed") clean.insert(key, value.isBool() ? value.toBool() : value.toInt() != 0);
        else if (key == "played_at" || key.endsWith("_seconds")) clean.insert(key, value.toDouble());
        else clean.insert(key, value.toString().left(16384));
    }
    if (clean.value("session_id").toString().isEmpty()) {
        // Stable across copies and repeated imports, while different source row
        // identities retain separate listens even at the same timestamp.
        auto identity = clean; identity.insert("sourceRow", row.value("id"));
        clean.insert("session_id", "legacy-qt-" + QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex()));
    }
    return clean;
}
}
QJsonObject StatsArchive::read(const QString& path, QJsonArray& records) {
    records = {};
    const QFileInfo info(path);
    if (!info.isFile() || info.size() > 256 * 1024 * 1024) return failure(QStringLiteral("无法读取统计文件，或文件超过 256 MB"));
    if (info.suffix().compare("json", Qt::CaseInsensitive) == 0) {
        QFile file(path); if (file.size() > maximumBytes || !file.open(QIODevice::ReadOnly)) return failure(QStringLiteral("统计档案无法读取或超过 64 MB"));
        QJsonParseError error; const auto object = QJsonDocument::fromJson(file.readAll(), &error).object();
        if (error.error != QJsonParseError::NoError || object.value("schema") != "bottlemusic.stats-export" || object.value("version").toInt() != 1 || !object.value("records").isArray()) return failure(QStringLiteral("统计档案格式无效"));
        const auto raw = object.value("records").toArray();
        if (raw.size() > maximumRecords) return failure(QStringLiteral("统计档案超过 100000 条记录"));
        for (const auto& value : raw) if (value.isObject()) records.append(canonical(value.toObject()));
    } else {
        ReadConnection connection(path);
        if (!connection.db.open() || !connection.db.transaction()) return failure(QStringLiteral("统计数据库无法只读打开，请关闭旧播放器后重试"));
        QSet<QString> columns; QSqlQuery schema(connection.db);
        if (!schema.exec("PRAGMA table_info(play_history_v2)")) return failure(QStringLiteral("统计表不可读取"));
        while (schema.next()) columns.insert(schema.value(1).toString());
        for (const auto& key : {"id", "song_hash", "song_name", "duration_seconds", "listened_seconds", "played_at"})
            if (!columns.contains(QLatin1String(key))) return failure(QStringLiteral("数据库不含受支持的聆听统计表"));
        QStringList projection{"id"};
        for (const auto& key : fields) projection.append(columns.contains(key) ? key : "NULL AS " + key);
        QSqlQuery query(connection.db); query.setForwardOnly(true);
        if (!query.exec("SELECT " + projection.join(',') + " FROM play_history_v2 ORDER BY id LIMIT 100001")) return failure(QStringLiteral("读取聆听记录失败"));
        qint64 bytes = 0;
        while (query.next()) {
            QJsonObject row;
            for (int i = 0; i < projection.size(); ++i) row.insert(i == 0 ? "id" : fields[i - 1], QJsonValue::fromVariant(query.value(i)));
            auto clean = canonical(row); bytes += QJsonDocument(clean).toJson(QJsonDocument::Compact).size();
            if (records.size() >= maximumRecords || bytes > maximumBytes) { records = {}; return failure(QStringLiteral("统计超过迁移容量，请先导出较小档案")); }
            records.append(clean);
        }
        if (query.lastError().isValid()) { records = {}; return failure(QStringLiteral("统计读取中断，迁移已停止")); }
        query.finish(); schema.finish(); connection.db.commit();
    }
    return {{"status", 1}, {"count", records.size()}};
}
QJsonObject StatsArchive::write(const QString& path, const QJsonArray& records) {
    if (QFileInfo(path).suffix().compare("json", Qt::CaseInsensitive) != 0) return failure(QStringLiteral("统计导出必须使用 .json 档案"));
    const QJsonObject archive{{"schema", "bottlemusic.stats-export"}, {"version", 1}, {"exportedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}, {"records", records}};
    const auto bytes = QJsonDocument(archive).toJson(QJsonDocument::Compact);
    if (bytes.size() > maximumBytes) return failure(QStringLiteral("统计档案超过 64 MB"));
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return failure(QStringLiteral("统计档案无法保存"));
    return {{"status", 1}, {"count", records.size()}};
}
QJsonObject StatsArchive::restore(const QString& source, const QString& currentDatabase, const QString& backup,
                                  const std::function<int(const QJsonObject&)>& record, const std::atomic_bool& stopped) {
    QJsonArray incoming, existing;
    const auto input = read(source, incoming); if (input.value("status").toInt() != 1) return input;
    if (QFileInfo::exists(currentDatabase)) { const auto current = read(currentDatabase, existing); if (current.value("status").toInt() != 1) return current; }
    if (stopped.load()) return failure(QStringLiteral("迁移已取消，尚未写入记录"));
    const auto saved = write(backup, existing); if (saved.value("status").toInt() != 1) return saved;
    int added = 0, duplicate = 0, skipped = 0;
    QString error;
    for (const auto& value : incoming) {
        if (stopped.load()) { error = QStringLiteral("迁移已停止，已导入部分可重复安全合并的记录"); break; }
        const auto row = value.toObject();
        const double at = row.value("played_at").toDouble();
        if (row.value("song_hash").toString().isEmpty() || at <= 0 || at > 1e15 || !std::isfinite(at)) { ++skipped; continue; }
        const int status = record(row);
        if (status == 0) ++added; else if (status == 6) ++duplicate; else if (status == 1 || status == 2) ++skipped;
        else { error = QStringLiteral("统计核心写入失败，已导入内容保留，可重试合并"); break; }
    }
    return {{"status", error.isEmpty() ? 1 : 0}, {"added", added}, {"duplicate", duplicate}, {"skipped", skipped}, {"backup", backup}, {"error", error}};
}
