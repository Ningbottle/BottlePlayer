#include "DataMigration.h"
#include "music/Track.h"
#include "core/NativeGateway.h"
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QCoreApplication>
#include <QProcess>
#include <QSet>
#include <cmath>
#include <algorithm>
#include <QRegularExpression>

namespace {
const QStringList preferenceKeys{"volume", "quality", "loopMode", "lyricSize", "vinyl", "reducedMotion", "queueSnapshot"};
const QStringList appearanceKeys{"mode", "presentation", "accent", "compact", "lyricFont", "lyricFocus", "labMode", "sidebarWidth"};
QJsonObject readPreferences(const QString& path, const QStringList& keys) {
    QSettings settings(path, QSettings::IniFormat); QJsonObject object;
    for (const auto& key : keys) if (settings.contains(key)) object.insert(key, QJsonValue::fromVariant(settings.value(key)));
    return object;
}
bool writeJson(const QString& path, const QJsonObject& value) {
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Indented);
    return file.write(bytes) == bytes.size() && file.commit();
}
}
DataMigration::DataMigration(QString profile, QObject* parent) : QObject(parent), profile_(std::move(profile)) {}
DataMigration::~DataMigration() { if (stopped_) stopped_->store(true); }
void DataMigration::setNativeGateway(NativeGateway* gateway) { gateway_ = gateway; }
void DataMigration::exportStatistics(const QUrl& file) {
    if (!gateway_ || !file.isLocalFile() || busy_) return;
    busy_ = true; stopped_ = std::make_shared<std::atomic_bool>(false); message_ = QStringLiteral("正在导出聆听统计…"); emit changed();
    gateway_->archiveStats({}, file.toLocalFile(), false, stopped_, this, [this](const QJsonObject& response) {
        busy_ = false; message_ = response.value("status").toInt() == 1 ? QStringLiteral("已导出 %1 条聆听记录").arg(response.value("count").toInt()) : response.value("error").toString(); emit changed();
    });
}
void DataMigration::importStatistics(const QUrl& file) {
    if (!gateway_ || !file.isLocalFile() || busy_) return;
    const auto backup = QDir(profile_).filePath("stats-backup-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmsszzz") + ".json");
    busy_ = true; stopped_ = std::make_shared<std::atomic_bool>(false); message_ = QStringLiteral("正在只读迁移聆听统计…"); emit changed();
    gateway_->archiveStats(file.toLocalFile(), backup, true, stopped_, this, [this](const QJsonObject& response) {
        busy_ = false;
        message_ = response.value("status").toInt() == 1 ? QStringLiteral("统计合并完成：新增 %1，重复 %2，跳过 %3。原有统计已备份。")
            .arg(response.value("added").toInt()).arg(response.value("duplicate").toInt()).arg(response.value("skipped").toInt()) : response.value("error").toString();
        emit changed(); emit statisticsImported();
    });
}
void DataMigration::cancelStatistics() { if (stopped_) stopped_->store(true); }
QJsonObject DataMigration::sanitizeSnapshot(const QJsonObject& source, bool legacy) {
    QJsonArray queue; int selected = -1; const auto rawIndex = source.value(legacy ? "currentIndex" : "index");
    const int index = rawIndex.isDouble() && std::floor(rawIndex.toDouble()) == rawIndex.toDouble() ? rawIndex.toInt(-1) : -1;
    const auto rows = source.value("queue").toArray();
    for (int i = 0; i < rows.size() && queue.size() < 2000; ++i) {
        if (!rows[i].isObject()) continue;
        auto track = Track::fromJson(rows[i].toObject()); if (track.hash.isEmpty() || track.hash.size() > 4096) continue;
        if (i == index) selected = int(queue.size());
        queue.append(QJsonObject::fromVariantMap(track.toVariant()));
    }
    double position = source.value("position").toDouble();
    if (legacy) position *= 1000;
    if (selected < 0 || !std::isfinite(position) || position < 0 || position >= 86400000) position = 0;
    return {{"version", 1}, {"queue", queue}, {"index", selected}, {"position", position}, {"radio", legacy ? source.value("queueMode").toString() == "personalFm" : source.value("radio").toBool()}};
}
void DataMigration::exportData(const QUrl& target) {
    if (!target.isLocalFile() || busy_) return;
    if (QFileInfo(target.toLocalFile()).suffix().compare("json", Qt::CaseInsensitive) != 0) { message_ = QStringLiteral("请选择 .json 档案，避免覆盖播放器文件"); emit changed(); return; }
    if (prepareSnapshot_ && !prepareSnapshot_()) { message_ = QStringLiteral("当前设置未能保存，导出已停止"); emit changed(); return; }
    const auto preferences = readPreferences(QDir(profile_).filePath("preferences.ini"), preferenceKeys);
    const auto appearance = readPreferences(QDir(profile_).filePath("appearance.ini"), appearanceKeys);
    QSettings favorites(QDir(profile_).filePath("favorites.ini"), QSettings::IniFormat);
    QSettings history(QDir(profile_).filePath("history.ini"), QSettings::IniFormat);
    QSettings layout(QDir(profile_).filePath("player-layout.ini"), QSettings::IniFormat);
    const auto equalizer = readPreferences(QDir(profile_).filePath("equalizer.ini"), {"enabled", "preset", "bands"});
    const QJsonObject data{{"schema", "bottlemusic.qt-data-export"}, {"version", 1}, {"exportedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"preferences", preferences}, {"appearance", appearance}, {"equalizer", equalizer},
        {"anonymousFavorites", QJsonDocument::fromJson(favorites.value("anonymous").toByteArray()).array()},
        {"recent", QJsonDocument::fromJson(history.value("recent").toByteArray()).array()},
        {"layout", QJsonDocument::fromJson(layout.value("layout").toByteArray()).object()}};
    message_ = writeJson(target.toLocalFile(), data) ? QStringLiteral("数据已导出，不含账号凭据、设备标识或 API Key") : QStringLiteral("导出失败，请检查目标目录权限"); emit changed();
}
void DataMigration::importData(const QUrl& source) {
    if (!source.isLocalFile() || busy_) return;
    QFile file(source.toLocalFile());
    if (file.size() <= 0 || file.size() > 8 * 1024 * 1024 || !file.open(QIODevice::ReadOnly)) { message_ = QStringLiteral("无法读取导入文件，或文件超过 8 MB"); emit changed(); return; }
    QJsonParseError error; const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    const auto payload = document.object(); const bool legacy = payload.value("schema") == "bottlemusic.user-data-export";
    if (error.error != QJsonParseError::NoError || payload.value("version").toInt() != 1
        || (!legacy && payload.value("schema") != "bottlemusic.qt-data-export")) { message_ = QStringLiteral("导出格式或版本不受支持"); emit changed(); return; }
    if (prepareSnapshot_ && !prepareSnapshot_()) { message_ = QStringLiteral("当前设置未能保存，导入已停止"); emit changed(); return; }
    const auto backup = QDir(profile_).filePath("import-backup-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmsszzz") + ".json");
    QSettings preferences(QDir(profile_).filePath("preferences.ini"), QSettings::IniFormat), appearance(QDir(profile_).filePath("appearance.ini"), QSettings::IniFormat);
    QSettings favorites(QDir(profile_).filePath("favorites.ini"), QSettings::IniFormat), history(QDir(profile_).filePath("history.ini"), QSettings::IniFormat), layout(QDir(profile_).filePath("player-layout.ini"), QSettings::IniFormat);
    QSettings equalizer(QDir(profile_).filePath("equalizer.ini"), QSettings::IniFormat);
    if (!writeJson(backup, {{"schema", "bottlemusic.qt-data-export"}, {"version", 1},
        {"preferences", readPreferences(preferences.fileName(), preferenceKeys)}, {"appearance", readPreferences(appearance.fileName(), appearanceKeys)},
        {"anonymousFavorites", QJsonDocument::fromJson(favorites.value("anonymous").toByteArray()).array()}, {"recent", QJsonDocument::fromJson(history.value("recent").toByteArray()).array()},
        {"layout", QJsonDocument::fromJson(layout.value("layout").toByteArray()).object()}, {"equalizer", readPreferences(equalizer.fileName(), {"enabled", "preset", "bands"})}})) {
        message_ = QStringLiteral("未能保存导入前备份，导入已停止"); emit changed(); return;
    }
    QJsonObject importedPreferences = payload.value("preferences").toObject(), importedAppearance = payload.value("appearance").toObject();
    QJsonObject snapshot, importedLayout = payload.value("layout").toObject();
    QJsonObject importedEq = payload.value("equalizer").toObject();
    QJsonArray importedFavorites = payload.value("anonymousFavorites").toArray(), importedRecent = payload.value("recent").toArray();
    bool hasFavorites = payload.value("anonymousFavorites").isArray(), hasRecent = payload.value("recent").isArray();
    if (legacy) {
        const auto values = payload.value("localStorage").toObject();
        const auto text = [&values](const char* key) { return values.value(QLatin1String(key)).toString(); };
        importedPreferences = {}; importedAppearance = {};
        if (values.contains("player_volume")) { bool valid = false; const auto volume = text("player_volume").toDouble(&valid); if (valid) importedPreferences.insert("volume", volume); }
        if (values.contains("player_quality")) importedPreferences.insert("quality", text("player_quality"));
        if (values.contains("player_loop_mode")) importedPreferences.insert("loopMode", text("player_loop_mode"));
        if (values.contains("appearance_cover_mode")) importedPreferences.insert("vinyl", text("appearance_cover_mode") == "vinyl");
        if (values.contains("appearance_lyric_scale")) importedPreferences.insert("lyricSize", text("appearance_lyric_scale") == "sm" ? 26 : text("appearance_lyric_scale") == "lg" ? 38 : text("appearance_lyric_scale") == "xl" ? 46 : 32);
        for (const auto& pair : {qMakePair("mode", "appearance_mode"), qMakePair("accent", "appearance_accent"), qMakePair("lyricFont", "appearance_lyric_font"), qMakePair("lyricFocus", "tweak_lyric_focus")})
            if (values.contains(QLatin1String(pair.second))) importedAppearance.insert(QLatin1String(pair.first), text(pair.second));
        for (const auto& pair : {qMakePair("mode", "tweak_mode"), qMakePair("accent", "tweak_accent")})
            if (!importedAppearance.contains(QLatin1String(pair.first)) && values.contains(QLatin1String(pair.second))) importedAppearance.insert(QLatin1String(pair.first), text(pair.second));
        if (values.contains("appearance_compact_list")) importedAppearance.insert("compact", text("appearance_compact_list") == "true");
        else if (values.contains("tweak_compact")) importedAppearance.insert("compact", text("tweak_compact") == "true");
        if (values.contains("lab_mode_enabled")) importedAppearance.insert("labMode", text("lab_mode_enabled") == "true");
        if (values.contains("sidebar_width")) importedAppearance.insert("sidebarWidth", text("sidebar_width").toInt());
        const auto skin = values.contains("appearance_skin") ? text("appearance_skin") : text("tweak_skin");
        // Both legacy skins rendered the same browse home; the imported preference
        // records that home rather than the curtain listening mode.
        if (skin == "mint" || skin == "aurora") importedAppearance.insert("presentation", "paper");
        snapshot = QJsonDocument::fromJson(text("player_queue_snapshot").toUtf8()).object();
        if (snapshot.isEmpty() && values.contains("player_queue")) snapshot = {{"queue", QJsonDocument::fromJson(text("player_queue").toUtf8()).array()}, {"currentIndex", text("player_index").toInt()}, {"queueMode", text("player_queue_mode")}};
        if (values.contains("bm_fav_anonymous")) { importedFavorites = QJsonDocument::fromJson(text("bm_fav_anonymous").toUtf8()).array(); hasFavorites = true; }
        if (values.contains("player_favorite_markers")) {
            for (const auto& marker : QJsonDocument::fromJson(text("player_favorite_markers").toUtf8()).array())
                if (marker.isString() && !marker.toString().isEmpty()) importedFavorites.append(QJsonObject{{"hash", marker.toString()}, {"title", QStringLiteral("本地收藏记录")}});
            hasFavorites = true;
        }
        if (values.contains("recent_played")) { importedRecent = QJsonDocument::fromJson(text("recent_played").toUtf8()).array(); hasRecent = true; }
        if (values.contains("player_layout")) importedLayout = QJsonDocument::fromJson(text("player_layout").toUtf8()).object();
        if (values.contains("player_eq_enabled")) importedEq.insert("enabled", text("player_eq_enabled") == "true");
        if (values.contains("player_eq_preset")) importedEq.insert("preset", text("player_eq_preset"));
        if (values.contains("player_eq_bands")) importedEq.insert("bands", text("player_eq_bands"));
    } else snapshot = QJsonDocument::fromJson(importedPreferences.value("queueSnapshot").toString().toUtf8()).object();
    emit importing();
    if (importedPreferences.value("volume").isDouble()) preferences.setValue("volume", std::clamp(importedPreferences.value("volume").toDouble(), 0.0, 1.0));
    const auto importedQuality = importedPreferences.value("quality").toString();
    const auto quality = importedQuality == "hires" ? QStringLiteral("high") : importedQuality;
    if (QStringList{"128", "320", "flac", "high", "master"}.contains(quality)) preferences.setValue("quality", quality);
    if (QStringList{"list", "single", "random"}.contains(importedPreferences.value("loopMode").toString())) preferences.setValue("loopMode", importedPreferences.value("loopMode").toString());
    if (QStringList{"light", "dark", "system"}.contains(importedAppearance.value("mode").toString())) appearance.setValue("mode", importedAppearance.value("mode").toString());
    if (QStringList{"paper", "curtain"}.contains(importedAppearance.value("presentation").toString())) appearance.setValue("presentation", importedAppearance.value("presentation").toString());
    if (QStringList{"skin", "kai", "song", "hei"}.contains(importedAppearance.value("lyricFont").toString())) appearance.setValue("lyricFont", importedAppearance.value("lyricFont").toString());
    if (QStringList{"readable", "stage"}.contains(importedAppearance.value("lyricFocus").toString())) appearance.setValue("lyricFocus", importedAppearance.value("lyricFocus").toString());
    if (importedAppearance.value("sidebarWidth").isDouble()) appearance.setValue("sidebarWidth", std::clamp(importedAppearance.value("sidebarWidth").toInt(), 200, 360));
    if (importedPreferences.value("lyricSize").isDouble()) preferences.setValue("lyricSize", std::clamp(importedPreferences.value("lyricSize").toInt(), 20, 48));
    for (const auto* key : {"vinyl", "reducedMotion"}) if (importedPreferences.value(QLatin1String(key)).isBool()) preferences.setValue(QLatin1String(key), importedPreferences.value(QLatin1String(key)).toBool());
    for (const auto* key : {"compact", "labMode"}) if (importedAppearance.value(QLatin1String(key)).isBool()) appearance.setValue(QLatin1String(key), importedAppearance.value(QLatin1String(key)).toBool());
    const auto accent = importedAppearance.value("accent").toString();
    if (importedAppearance.contains("accent") && (accent.isEmpty() || QRegularExpression("^#[0-9a-fA-F]{6}$").match(accent).hasMatch())) appearance.setValue("accent", accent);
    if (!snapshot.isEmpty()) preferences.setValue("queueSnapshot", QJsonDocument(sanitizeSnapshot(snapshot, legacy)).toJson(QJsonDocument::Compact));
    const auto sanitizeRows = [](const QJsonArray& rows, bool dates) {
        QJsonArray result; QSet<QString> seen;
        for (const auto& value : rows) {
            const auto raw = value.toObject(); auto track = Track::fromJson(raw); if (track.hash.isEmpty() || seen.contains(track.hash.toLower())) continue;
            seen.insert(track.hash.toLower()); auto row = QJsonObject::fromVariantMap(track.toVariant());
            if (dates) row.insert("playedAt", raw.value("playedAt").toDouble(0));
            result.append(row); if (result.size() >= (dates ? 100 : 2000)) break;
        }
        return result;
    };
    if (hasFavorites) { favorites.setValue("anonymous", QJsonDocument(sanitizeRows(importedFavorites, false)).toJson(QJsonDocument::Compact)); favorites.setValue("legacyImported", true); }
    if (hasRecent) history.setValue("recent", QJsonDocument(sanitizeRows(importedRecent, true)).toJson(QJsonDocument::Compact));
    if (importedLayout.value("version").toInt() == 1 && importedLayout.value("cards").isObject()) layout.setValue("layout", QJsonDocument(importedLayout).toJson(QJsonDocument::Compact));
    if (importedEq.value("enabled").isBool()) equalizer.setValue("enabled", importedEq.value("enabled").toBool());
    if (QStringList{"Flat", "Bass Boost", "Vocal", "Rock", "Harman Kardon", "125Hz Test", "Custom"}.contains(importedEq.value("preset").toString())) equalizer.setValue("preset", importedEq.value("preset").toString());
    const auto rawBands = QJsonDocument::fromJson(importedEq.value("bands").toString().toUtf8()).array();
    if (!rawBands.isEmpty()) { QJsonArray bands; for (int i = 0; i < 10; ++i) bands.append(i < rawBands.size() ? std::clamp(rawBands[i].toDouble(), -6.0, 6.0) : 0); equalizer.setValue("bands", QJsonDocument(bands).toJson(QJsonDocument::Compact)); }
    preferences.sync(); appearance.sync(); favorites.sync(); history.sync(); layout.sync(); equalizer.sync();
    restart_ = preferences.status() == QSettings::NoError && appearance.status() == QSettings::NoError && favorites.status() == QSettings::NoError && history.status() == QSettings::NoError && layout.status() == QSettings::NoError && equalizer.status() == QSettings::NoError;
    message_ = restart_ ? QStringLiteral("导入完成，备份已保存。重新启动以应用导入内容。") : QStringLiteral("部分设置未能写入，请保留导入前备份并重试"); emit changed();
}
void DataMigration::restartApp() { if (QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1))) QCoreApplication::quit(); else { message_ = QStringLiteral("未能重新启动，请手动关闭再打开"); emit changed(); } }
