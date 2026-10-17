#include "VipController.h"
#include <QDateTime>

namespace {
qint64 expiry(const QString& raw) {
    bool numeric = false; const auto number = raw.toLongLong(&numeric);
    if (numeric && number > 0) return number < 100000000000LL ? number * 1000 : number;
    auto date = QDateTime::fromString(raw, Qt::ISODate);
    if (!date.isValid()) date = QDateTime::fromString(raw, "yyyy-MM-dd HH:mm:ss");
    if (!date.isValid()) date = QDateTime::fromString(raw, "yyyy-MM-dd");
    return date.isValid() ? date.toMSecsSinceEpoch() : 0;
}
bool active(const QJsonObject& bucket) {
    if (!bucket.value("active").toBool()) return false;
    const auto end = bucket.value("end").toString();
    return end.isEmpty() || expiry(end) > QDateTime::currentMSecsSinceEpoch();
}
bool success(const QJsonObject& value) { return value.value("status").toVariant().toInt() == 1; }
}
VipController::VipController(NativeGateway& gateway, QObject* parent) : QObject(parent), gateway_(gateway) {
    confirmTimer_.setSingleShot(true); confirmTimer_.setInterval(2000);
    connect(&confirmTimer_, &QTimer::timeout, this, &VipController::confirm);
    expiryTimer_.setInterval(60000);
    connect(&expiryTimer_, &QTimer::timeout, this, [this] {
        const auto previous = tier_;
        tier_ = active(premium_) ? "premium" : active(listen_) ? "listen" : authoritative_ ? "none" : "unknown";
        if (tier_ != previous) { end_.clear(); emit changed(); }
    });
}
VipController::~VipController() { gateway_.cancel(detailId_); gateway_.cancel(claimId_); }
QString VipController::label() const {
    return tier_ == "premium" ? QStringLiteral("豪华 VIP") : tier_ == "listen" ? QStringLiteral("畅听 VIP")
        : tier_ == "none" ? QStringLiteral("当前未见可用 VIP 权益") : QStringLiteral("权益状态待确认");
}
QString VipController::caution() const {
    return tier_ == "listen" && !listenPlayback_ ? QStringLiteral("已确认畅听权益，部分歌曲仍需豪华 VIP 才能完整播放。") : QString{};
}
void VipController::setAccount(const QString& account) {
    if (account_ == account) return;
    ++generation_; ++detailGeneration_; gateway_.cancel(detailId_); gateway_.cancel(claimId_);
    confirmTimer_.stop(); expiryTimer_.stop(); account_ = account;
    tier_ = "unknown"; end_.clear(); message_.clear(); accepted_.clear(); premium_ = {}; listen_ = {};
    busy_ = loading_ = cancelled_ = authoritative_ = listenPlayback_ = false; emit changed();
    if (!account_.isEmpty()) { expiryTimer_.start(); refresh(); }
}
bool VipController::apply(const QJsonObject& response) {
    if (!success(response)) return false;
    const auto data = response.value("data").toObject(); const auto summary = data.value("entitlement_summary").toObject();
    if (summary.isEmpty()) {
        // Older native snapshots may contain the authoritative music VIP fields.
        if (!data.contains("is_vip")) return false;
        const auto rawEnd = data.value("vip_end_time").toString(data.value("vip_end_date").toString());
        premium_ = {{"active", data.value("is_vip").toVariant().toInt() == 1 && (rawEnd.isEmpty() || expiry(rawEnd) > QDateTime::currentMSecsSinceEpoch())}, {"end", rawEnd}};
        listen_ = {}; listenPlayback_ = false;
    } else {
        premium_ = summary.value("premium").toObject(); listen_ = summary.value("listen").toObject();
        listenPlayback_ = summary.value("listen_unlocks_playback").toBool();
    }
    authoritative_ = true;
    tier_ = active(premium_) ? "premium" : active(listen_) ? "listen" : "none";
    end_ = tier_ == "premium" ? premium_.value("end").toString() : tier_ == "listen" ? listen_.value("end").toString() : QString{};
    emit changed(); return true;
}
void VipController::refresh() {
    if (account_.isEmpty() || !gateway_.ready() || busy_) return;
    gateway_.cancel(detailId_); const auto owner = generation_, detail = ++detailGeneration_;
    loading_ = true; emit changed();
    detailId_ = gateway_.get("/user/vip/detail", {}, this, [this, owner, detail](const QJsonObject& response) {
        if (owner != generation_ || detail != detailGeneration_) return;
        detailId_.clear(); loading_ = false;
        if (!apply(response)) message_ = QStringLiteral("权益查询失败，保留最后一次已确认的信息");
        else if (!busy_) message_.clear(); emit changed();
    });
}
QString VipController::failureMessage(const QJsonObject& response) {
    const auto reason = response.value("reason").toString();
    const auto detail = response.value("error_msg").toString(response.value("error").toString(response.value("message").toString()));
    if (reason == "already_claimed_today") return QStringLiteral("今天已领取，正在重新确认权益");
    if (reason == "rejected_identity_or_risk") return QStringLiteral("领取被拒：身份或风控校验未通过");
    if (reason == "session_family_mismatch") return QStringLiteral("领取被拒：当前会话不匹配");
    if (reason == "bad_receive_day") return QStringLiteral("领取被拒：上游不接受当前日期");
    const auto code = response.value("error_code").toVariant().toString();
    return !detail.isEmpty() ? QStringLiteral("领取失败：") + detail : !code.isEmpty() ? QStringLiteral("领取被拒，原因未明确（错误码 %1）").arg(code) : QStringLiteral("领取未被接受，请稍后重试");
}
void VipController::claim() {
    if (account_.isEmpty() || !gateway_.ready() || busy_) return;
    ++detailGeneration_; gateway_.cancel(detailId_); loading_ = false;
    ++generation_; busy_ = true; cancelled_ = false; attempts_ = 0; stage_ = "day";
    message_ = QStringLiteral("正在领取每日 VIP…"); emit changed(); requestClaim("/youth/day/vip", "confirm-listen");
}
void VipController::requestClaim(const QString& route, const QString& next) {
    if (cancelled_) { finish(QStringLiteral("已停止后续领取请求")); return; }
    const auto owner = generation_;
    claimId_ = gateway_.get(route, {}, this, [this, owner, route, next](const QJsonObject& response) {
        if (owner != generation_) return;
        claimId_.clear();
        const auto reason = response.value("reason").toString();
        const bool accepted = success(response);
        const bool already = (route == "/youth/day/vip" && reason == "already_claimed_today") || (route.endsWith("upgrade") && reason == "upgrade_already_claimed");
        if (accepted) accepted_ = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") + QStringLiteral(" · 已受理，等待权益确认");
        if (!accepted && !already) {
            finish(tier_ == "listen" ? QStringLiteral("畅听权益已确认，升级未成功。") + failureMessage(response) : failureMessage(response)); return;
        }
        stage_ = next; attempts_ = 0; message_ = QStringLiteral("请求已受理，正在向权威接口确认权益…"); emit changed();
        confirm(); // First confirmation wins over a cancellation racing an accepted request.
    });
}
void VipController::confirm() {
    if (!busy_ || account_.isEmpty()) return;
    const auto owner = generation_; ++attempts_;
    claimId_ = gateway_.get("/user/vip/detail", {}, this, [this, owner](const QJsonObject& response) {
        if (owner != generation_) return;
        claimId_.clear(); const bool known = apply(response);
        if (known && tier_ == "premium") { finish(QStringLiteral("豪华 VIP 权益已确认") + (end_.isEmpty() ? QString{} : QStringLiteral("，有效期至 ") + end_)); return; }
        if (known && stage_ == "confirm-listen" && tier_ == "listen") {
            if (cancelled_) { finish(QStringLiteral("已停止后续请求，畅听权益已确认")); return; }
            stage_ = "upgrade"; advance(); return;
        }
        if (cancelled_) { finish(QStringLiteral("已停止后续领取请求")); return; }
        if (attempts_ >= 4) {
            finish(known ? tier_ == "listen" ? QStringLiteral("畅听权益已确认，豪华权益尚未确认") : QStringLiteral("请求已受理，权威接口尚未见可用权益")
                : QStringLiteral("请求已受理，权益状态未确认，可稍后刷新账户")); return;
        }
        confirmTimer_.start();
    });
}
void VipController::advance() {
    if (cancelled_) { finish(QStringLiteral("已停止后续领取请求")); return; }
    const auto owner = generation_; message_ = QStringLiteral("畅听权益已确认，正在尝试升级…"); emit changed();
    claimId_ = gateway_.get("/youth/vip/ad", {}, this, [this, owner](const QJsonObject&) {
        if (owner != generation_) return;
        claimId_.clear(); requestClaim("/youth/day/vip/upgrade", "confirm-premium");
    });
}
void VipController::cancel() {
    if (!busy_) return;
    cancelled_ = true; confirmTimer_.stop();
    if (claimId_.isEmpty()) finish(QStringLiteral("已停止后续领取请求"));
    else { message_ = QStringLiteral("正在停止，确认已经发出的请求…"); emit changed(); }
}
void VipController::finish(const QString& message) { confirmTimer_.stop(); busy_ = false; message_ = message; emit changed(); }
