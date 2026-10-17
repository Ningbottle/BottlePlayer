#include "HistoryController.h"
#include "music/Track.h"
#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace {
QString identity(const QVariantMap& track) { return track.value("hash").toString().toLower() + "|" + track.value("audioId").toString(); }
qint64 remoteTime(const QJsonObject& entry) {
    for (const auto* key : {"time", "addtime", "play_time", "playtime", "timestamp"}) {
        const auto value = entry.value(QLatin1String(key));
        const double number = value.isDouble() ? value.toDouble() : value.toString().toDouble();
        if (std::isfinite(number) && number > 0 && number < 1e15) return qint64(number < 1e11 ? number * 1000 : number);
    }
    return 0; // Unknown dates stay unknown, never fabricate the current time.
}
}
HistoryController::HistoryController(NativeGateway& gateway, const QString& profile, QObject* parent)
    : QObject(parent), gateway_(gateway), settings_(QDir(profile).filePath("history.ini"), QSettings::IniFormat) {
    const auto stored = QJsonDocument::fromJson(settings_.value("recent").toByteArray()).array();
    for (const auto& value : stored) {
        const auto row = value.toObject().toVariantMap();
        if (!row.value("hash").toString().isEmpty()) local_.append(row);
        if (local_.size() >= 100) break;
    }
    connect(&gateway_, &NativeGateway::readyChanged, this, [this] { if (active_ && gateway_.ready()) refresh(); });
}
QVariantList HistoryController::tracks() const {
    auto boundary = QDate::currentDate();
    if (scope_ == "week") boundary = boundary.addDays(1 - boundary.dayOfWeek());
    if (scope_ == "month") boundary = QDate(boundary.year(), boundary.month(), 1);
    const auto since = scope_ == "all" ? 0 : boundary.startOfDay().toMSecsSinceEpoch();
    auto combined = local_; combined.append(remote_);
    std::stable_sort(combined.begin(), combined.end(), [](const auto& left, const auto& right) {
        return left.toMap().value("playedAt").toLongLong() > right.toMap().value("playedAt").toLongLong();
    });
    QVariantList result; QSet<QString> seen;
    for (const auto& value : combined) {
        if (scope_ != "all" && value.toMap().value("playedAt").toLongLong() < since) continue;
        const auto key = identity(value.toMap()); if (seen.contains(key)) continue;
        seen.insert(key); result.append(value); if (result.size() >= 100) break;
    }
    return result;
}
void HistoryController::setScope(const QString& value) {
    if (value == scope_ || !QStringList{"week", "month", "all"}.contains(value)) return;
    scope_ = value; emit changed();
}
void HistoryController::started(const QVariantMap& track) {
    if (track.value("hash").toString().isEmpty()) return;
    auto row = track; row.insert("playedAt", QDateTime::currentMSecsSinceEpoch());
    const auto key = identity(row);
    local_.erase(std::remove_if(local_.begin(), local_.end(), [&key](const auto& value) { return identity(value.toMap()) == key; }), local_.end());
    local_.prepend(row); while (local_.size() > 100) local_.removeLast();
    settings_.setValue("recent", QJsonDocument(QJsonArray::fromVariantList(local_)).toJson(QJsonDocument::Compact));
    settings_.sync();
    if (settings_.status() != QSettings::NoError) error_ = QStringLiteral("最近播放暂时无法保存，当前记录仍可使用");
    emit changed();
    const auto audioId = track.value("audioId").toString();
    bool validId = false; const auto numericId = audioId.toLongLong(&validId);
    if (!account_.isEmpty() && validId && numericId > 0) {
        // Match the original login-gated upload policy; failures do not affect audio.
        gateway_.request("POST", "/playhistory/upload", {{"mxid", audioId}, {"time", QDateTime::currentSecsSinceEpoch()}, {"pc", 1}}, {}, this, [](const QJsonObject&) {});
    }
}
void HistoryController::setAccount(const QString& userId) {
    if (account_ == userId) return;
    account_ = userId; ++generation_; gateway_.cancel(request_); request_.clear();
    remote_.clear(); error_.clear(); loading_ = false; emit changed(); if (active_) refresh();
}
void HistoryController::setActive(bool active) {
    active_ = active;
    if (active) refresh();
    else { ++generation_; gateway_.cancel(request_); request_.clear(); loading_ = false; emit changed(); }
}
void HistoryController::refresh() {
    ++generation_; gateway_.cancel(request_); request_.clear();
    if (!gateway_.ready() || account_.isEmpty()) { loading_ = false; emit changed(); return; }
    const auto owner = generation_; loading_ = true; error_.clear(); emit changed();
    request_ = gateway_.get("/user/history", {{"pagesize", 100}}, this, [this, owner](const QJsonObject& response) {
        if (owner != generation_) return;
        request_.clear(); loading_ = false;
        if (response.value("status").toInt() != 1) error_ = QStringLiteral("远端同步失败，已显示本地记录");
        else {
            remote_.clear();
            for (const auto& value : songList(response)) {
                const auto raw = value.toObject(); const auto song = raw.value("info").isObject() ? raw.value("info").toObject() : raw;
                const auto track = Track::fromJson(song); if (track.hash.isEmpty()) continue;
                auto row = track.toVariant(); const auto timestamp = remoteTime(raw);
                row.insert("playedAt", timestamp ? timestamp : remoteTime(song)); remote_.append(row);
            }
        }
        emit changed();
    });
}
void HistoryController::play(int index) {
    const auto rows = tracks(); if (index >= 0 && index < rows.size()) emit playRequested(rows, index);
}
