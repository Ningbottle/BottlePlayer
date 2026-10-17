#include "AccountController.h"
#include "QrImage.h"
#include <QUuid>
#include <QUrl>
#include <QUrlQuery>
#include <utility>

namespace {
int status(const QJsonObject& value) {
    const auto field = value.value("status");
    return field.isString() ? field.toString().toInt() : field.toInt();
}
QString identifier(const QJsonValue& value) { return value.isDouble() ? QString::number(value.toInteger()) : value.toString(); }
}
AccountController::AccountController(NativeGateway& gateway, QObject* parent) : QObject(parent), gateway_(gateway) {
    pollTimer_.setSingleShot(true);
    connect(&pollTimer_, &QTimer::timeout, this, &AccountController::poll);
    connect(&gateway_, &NativeGateway::readyChanged, this, [this] {
        emit changed(); if (ready()) refreshSession();
    });
}
AccountController::~AccountController() { abandonAttempt(); gateway_.cancel(sessionRequest_); }
void AccountController::setState(QString state, QString message) {
    state_ = std::move(state); message_ = std::move(message); emit changed();
}
void AccountController::activate() {
    active_ = true;
    if (ready() && !sessionChecked_) { if (sessionRequest_.isEmpty()) refreshSession(); }
    else if (ready() && !loggedIn() && state_ == QLatin1String("idle")) beginLogin();
}
void AccountController::deactivate() { active_ = false; if (!attempt_.isEmpty()) cancelLogin(); }
void AccountController::abandonAttempt() {
    ++generation_; pollTimer_.stop(); gateway_.cancel(requestId_); requestId_.clear(); key_.clear(); qrImage_.clear();
    const auto attempt = std::exchange(attempt_, {});
    if (!attempt.isEmpty() && ready()) gateway_.request("POST", "/login/qr/cancel", {{"attempt_id", attempt}}, {}, this, [](QJsonObject) {});
}
void AccountController::cancelLogin() {
    abandonAttempt(); busy_ = false; setState("idle", QStringLiteral("登录已取消，可重新生成二维码"));
}
void AccountController::beginLogin() {
    if (!ready() || loggedIn() || !sessionRequest_.isEmpty()) return;
    abandonAttempt(); active_ = true; busy_ = true; failures_ = 0;
    attempt_ = QStringLiteral("qt-qr-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto epoch = generation_;
    setState("requesting", QStringLiteral("正在生成登录二维码…"));
    requestId_ = gateway_.get("/login/qr/key", {{"attempt_id", attempt_}}, this, [this, epoch](QJsonObject response) {
        if (epoch != generation_ || !active_) return;
        requestId_.clear(); busy_ = false;
        const auto data = response.value("data").toObject(); key_ = data.value("qrcode").toString();
        if (status(response) != 1 || key_.isEmpty()) { setState("error", QStringLiteral("二维码获取失败，请检查网络后重试")); return; }
        QString payload = data.value("qrcodeurl").toString();
        if (payload.isEmpty()) {
            QUrl url(QStringLiteral("https://h5.kugou.com/apps/loginQRCode/html/index.html"));
            QUrlQuery query; query.addQueryItem("qrcode", key_); url.setQuery(query); payload = url.toString(QUrl::FullyEncoded);
        }
        qrImage_ = loginQrImage(payload);
        if (qrImage_.isEmpty()) { setState("error", QStringLiteral("二维码生成失败，请刷新重试")); return; }
        qrAge_.restart(); setState("waiting", QStringLiteral("请使用酷狗音乐手机 App 扫码登录")); pollTimer_.start(2000);
    });
}
void AccountController::poll() {
    if (!active_ || key_.isEmpty() || !requestId_.isEmpty()) return;
    if (qrAge_.elapsed() >= 180000) { qrImage_.clear(); setState("expired", QStringLiteral("二维码已过期，请刷新")); return; }
    const auto epoch = generation_;
    requestId_ = gateway_.get("/login/qr/check", {{"key", key_}}, this, [this, epoch](QJsonObject response) {
        if (epoch != generation_ || !active_) return;
        requestId_.clear();
        if (status(response) != 1) {
            if (++failures_ >= 6) { setState("error", QStringLiteral("登录状态查询失败，请刷新二维码重试")); return; }
            message_ = QStringLiteral("连接暂时中断，正在重试…"); emit changed(); pollTimer_.start(qMin(10000, failures_ * 2000)); return;
        }
        failures_ = 0;
        const int remote = status(response.value("data").toObject());
        if (remote == 4) {
            attempt_.clear(); key_.clear(); qrImage_.clear();
            setState("success", QStringLiteral("登录成功，正在读取账户信息…")); refreshSession();
        } else if (remote == 0) {
            qrImage_.clear(); setState("expired", QStringLiteral("二维码已过期，请刷新"));
        } else {
            setState(remote == 2 ? "scanned" : "waiting", remote == 2 ? QStringLiteral("已扫码，请在手机上确认登录") : QStringLiteral("请使用酷狗音乐手机 App 扫码登录"));
            pollTimer_.start(2000);
        }
    });
}
void AccountController::refreshSession() {
    if (!ready()) return;
    gateway_.cancel(sessionRequest_); const auto epoch = ++sessionGeneration_;
    sessionRequest_ = gateway_.get("/user/detail", {}, this, [this, epoch](QJsonObject response) {
        if (epoch != sessionGeneration_) return;
        sessionRequest_.clear(); sessionChecked_ = true; busy_ = false;
        const auto data = response.value("data").toObject(); const auto id = identifier(data.value("userid"));
        if (status(response) == 1 && !id.isEmpty() && id != QLatin1String("0")) {
            const bool switched = userId_ != id;
            abandonAttempt();
            userId_ = id; username_ = data.value("nickname").toString(data.value("username").toString(QStringLiteral("听歌用户")));
            if (username_.isEmpty()) username_ = QStringLiteral("听歌用户");
            avatar_ = data.value("pic").toString(data.value("avatar").toString());
            busy_ = false; setState("success", QStringLiteral("账户已登录，个人歌单已连接"));
            if (switched) emit sessionChanged(userId_);
        } else if (response.value("error").toString() == QLatin1String("not logged in")) {
            const bool hadAccount = loggedIn(); userId_.clear(); username_.clear(); avatar_.clear(); emit changed();
            if (hadAccount) emit sessionChanged({});
            if (active_ && attempt_.isEmpty()) beginLogin();
        } else setState("error", QStringLiteral("账户信息暂不可用，请稍后刷新"));
    });
}
void AccountController::logout() {
    if (!ready() || !loggedIn() || busy_) return;
    abandonAttempt(); gateway_.cancel(sessionRequest_); const auto epoch = ++sessionGeneration_; busy_ = true; emit changed();
    sessionRequest_ = gateway_.request("POST", "/auth/logout", {}, {}, this, [this, epoch](QJsonObject response) {
        if (epoch != sessionGeneration_) return;
        sessionRequest_.clear(); busy_ = false;
        if (status(response) != 1) { setState("error", QStringLiteral("退出登录失败，请重试")); return; }
        userId_.clear(); username_.clear(); avatar_.clear(); emit sessionChanged({});
        setState("idle", QStringLiteral("已退出登录，个人歌单已清空"));
    });
}
