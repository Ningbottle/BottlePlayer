#include "SettingsController.h"
#include <QDir>
#include <QUrl>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QClipboard>
#include <QJsonDocument>

SettingsController::SettingsController(NativeGateway& gateway, const QString& profile, QObject* parent)
    : QObject(parent), gateway_(gateway), profile_(QDir(profile).absolutePath()) {}
void SettingsController::loadDevice() {
    if (!gateway_.ready() || busy_) return;
    const auto owner = ++deviceGeneration_; gateway_.cancel(deviceRequest_); busy_ = true; emit changed();
    deviceRequest_ = gateway_.get("/settings/device", {}, this, [this, owner](const QJsonObject& response) {
        if (owner != deviceGeneration_) return;
        busy_ = false; deviceRequest_.clear();
        if (response.value("status").toInt() == 1) { device_ = response.value("data").toObject().toVariantMap(); message_.clear(); }
        else message_ = response.value("error").toString(QStringLiteral("设备参数读取失败"));
        emit changed();
    });
}
void SettingsController::saveDevice(const QString& dfid, const QString& mid, const QString& uuid) { updateDevice({{"dfid", dfid.trimmed()}, {"mid", mid.trimmed()}, {"uuid", uuid.trimmed()}}); }
void SettingsController::resetDevice() { updateDevice({{"clear", "1"}}); }
void SettingsController::updateDevice(const QJsonObject& query) {
    if (!gateway_.ready() || busy_) return;
    const auto owner = ++deviceGeneration_; gateway_.cancel(deviceRequest_); busy_ = true; message_ = QStringLiteral("正在保存…"); emit changed();
    deviceRequest_ = gateway_.request("POST", "/settings/device", query, {}, this, [this, owner](const QJsonObject& response) {
        if (owner != deviceGeneration_) return;
        busy_ = false; deviceRequest_.clear();
        if (response.value("status").toInt() == 1) { device_ = response.value("data").toObject().toVariantMap(); message_ = response.value("updated").toBool(true) ? QStringLiteral("设备参数已保存") : QStringLiteral("参数没有变化"); }
        else message_ = response.value("error").toString(QStringLiteral("保存失败，请检查参数格式"));
        emit changed();
    });
}
void SettingsController::testDevice() {
    if (!gateway_.ready() || busy_) return;
    const auto owner = ++deviceGeneration_; busy_ = true; message_ = QStringLiteral("正在检查音源连接…"); emit changed();
    deviceRequest_ = gateway_.get("/song/url", {{"hash", "F0A6BA24635A8560F96C2C2D603E8CA8"}, {"album_id", "1776319"}, {"album_audio_id", "39905465"}, {"quality", "128"}}, this,
        [this, owner](const QJsonObject& response) {
            if (owner != deviceGeneration_) return;
            busy_ = false; deviceRequest_.clear();
            if (response.value("status").toInt() != 1) message_ = QStringLiteral("连接失败：") + response.value("error").toString(QStringLiteral("未获取到音源"));
            else message_ = response.value("delivery").toString() == "full" ? QStringLiteral("已确认完整音源连接")
                : response.value("is_preview").toBool() || response.value("delivery").toString() == "preview" ? QStringLiteral("当前返回试听音源，请检查账户权益与设备状态")
                : QStringLiteral("已获取音源，完整播放权限尚未确认");
            emit changed();
        });
}
void SettingsController::loadDiagnostics() {
    if (!gateway_.ready()) return;
    const auto owner = ++memoryGeneration_; gateway_.cancel(memoryRequest_);
    memoryRequest_ = gateway_.get("/diagnostics/memory", {}, this, [this, owner](const QJsonObject& response) {
        if (owner != memoryGeneration_) return;
        memoryRequest_.clear();
        if (response.value("status").toInt() == 1) memory_ = response.value("data").toObject().toVariantMap();
        else message_ = QStringLiteral("诊断暂不可用"); emit changed();
    });
}
QString SettingsController::diagnosticText() const {
    // Device fingerprints and session credentials are excluded from clipboard diagnostics.
    return QStringLiteral("BottleMusic Qt\nQt %1\nNative %2\n").arg(QString::fromLatin1(qVersion()), gateway_.fingerprint())
        + QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(memory_)).toJson(QJsonDocument::Indented));
}
void SettingsController::copyDiagnostics() { if (QGuiApplication::clipboard()) { QGuiApplication::clipboard()->setText(diagnosticText()); message_ = QStringLiteral("诊断已复制"); emit changed(); } }
void SettingsController::openProfileFolder() { QDesktopServices::openUrl(QUrl::fromLocalFile(profile_)); }
void SettingsController::openExternal(const QString& raw) { const QUrl url(raw); if (url.isValid() && url.scheme() == "https") QDesktopServices::openUrl(url); }
