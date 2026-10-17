#pragma once
#include "core/NativeGateway.h"
#include <QTimer>
#include <QElapsedTimer>

class AccountController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString userId READ userId NOTIFY changed)
    Q_PROPERTY(QString username READ username NOTIFY changed)
    Q_PROPERTY(QString avatar READ avatar NOTIFY changed)
    Q_PROPERTY(QString qrImage READ qrImage NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
public:
    explicit AccountController(NativeGateway& gateway, QObject* parent = nullptr);
    ~AccountController() override;
    bool ready() const { return gateway_.ready(); }
    bool loggedIn() const { return !userId_.isEmpty(); }
    bool busy() const { return busy_; }
    QString userId() const { return userId_; }
    QString username() const { return username_.isEmpty() ? QStringLiteral("未登录") : username_; }
    QString avatar() const { return avatar_; }
    QString qrImage() const { return qrImage_; }
    QString state() const { return state_; }
    QString message() const { return message_; }
    Q_INVOKABLE void activate();
    Q_INVOKABLE void deactivate();
    Q_INVOKABLE void beginLogin();
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void refreshSession();
    Q_INVOKABLE void logout();
signals:
    void changed();
    void sessionChanged(const QString& userId);
private:
    void abandonAttempt();
    void poll();
    void setState(QString state, QString message);
    NativeGateway& gateway_;
    QTimer pollTimer_;
    QElapsedTimer qrAge_;
    QString userId_, username_, avatar_, qrImage_, key_, attempt_, requestId_, sessionRequest_;
    QString state_ = QStringLiteral("idle"), message_ = QStringLiteral("扫码登录后查看个人歌单");
    quint64 generation_ = 0, sessionGeneration_ = 0;
    int failures_ = 0;
    bool active_ = false, busy_ = false, sessionChecked_ = false;
};
