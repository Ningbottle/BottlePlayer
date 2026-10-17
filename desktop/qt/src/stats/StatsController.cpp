#include "StatsController.h"
#include <QJsonArray>

StatsController::StatsController(NativeGateway& gateway, QObject* parent) : QObject(parent), gateway_(gateway) {
    refreshTimer_.setSingleShot(true); refreshTimer_.setInterval(250);
    connect(&refreshTimer_, &QTimer::timeout, this, &StatsController::refresh);
    connect(&gateway_, &NativeGateway::readyChanged, this, [this] { if (active_ && gateway_.ready()) refresh(); });
}
void StatsController::setRange(const QString& value) {
    if (value != "1d" && value != "7d" && value != "30d") return;
    if (range_ == value) return;
    range_ = value; emit changed(); refresh();
}
void StatsController::setActive(bool active) {
    active_ = active;
    if (active) refresh();
    else { ++generation_; for (const auto& id : requests_) gateway_.cancel(id); requests_.clear(); pending_ = 0; refreshTimer_.stop(); emit changed(); }
}
void StatsController::refresh() {
    if (!gateway_.ready()) return;
    for (const auto& id : requests_) gateway_.cancel(id);
    requests_.clear(); const auto owner = ++generation_;
    pending_ = 5; error_.clear(); emit changed();
    for (const auto& operation : {QString("summary"), QString("song"), QString("artist"), QString("album"), QString("timeline")}) {
        const bool top = operation == "song" || operation == "artist" || operation == "album";
        requests_.append(gateway_.stats(top ? "top" : operation, {{"range", range_}, {"kind", operation}, {"limit", 10}}, this,
            [this, owner, operation](const QJsonObject& response) {
                if (owner != generation_) return;
                if (response.value("degraded").toBool()) error_ = QStringLiteral("统计暂不可用，请稍后重试");
                else if (operation == "summary") summary_ = response.toVariantMap();
                else {
                    const auto rows = response.value("items").toArray().toVariantList();
                    if (operation == "song") songs_ = rows;
                    else if (operation == "artist") artists_ = rows;
                    else if (operation == "album") albums_ = rows;
                    else timeline_ = rows;
                }
                --pending_; emit changed();
            }));
    }
}
void StatsController::record(const QJsonObject& record) {
    if (record.isEmpty()) return;
    gateway_.recordPlay(record, this, [this](const QJsonObject& response) {
        if (response.value("status").toInt() != 1) { emit notice(QStringLiteral("本次播放统计未能保存")); return; }
        emit recordCommitted(); if (active_) refreshTimer_.start();
    });
}
