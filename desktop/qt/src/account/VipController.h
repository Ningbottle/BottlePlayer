#pragma once
#include "core/NativeGateway.h"
#include <QTimer>

class VipController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString tier READ tier NOTIFY changed)
    Q_PROPERTY(QString label READ label NOTIFY changed)
    Q_PROPERTY(QString end READ end NOTIFY changed)
    Q_PROPERTY(QString caution READ caution NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString accepted READ accepted NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
public:
    explicit VipController(NativeGateway& gateway, QObject* parent = nullptr);
    ~VipController() override;
    QString tier() const { return tier_; }
    QString label() const;
    QString end() const { return end_; }
    QString caution() const;
    QString message() const { return message_; }
    QString accepted() const { return accepted_; }
    bool busy() const { return busy_; }
    bool loading() const { return loading_; }
    void setAccount(const QString& userId);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void claim();
    Q_INVOKABLE void cancel();
    static QString failureMessage(const QJsonObject& result);
signals:
    void changed();
private:
    bool apply(const QJsonObject& response);
    void requestClaim(const QString& route, const QString& next);
    void confirm();
    void advance();
    void finish(const QString& message);
    NativeGateway& gateway_;
    QString account_, tier_ = "unknown", end_, message_, accepted_, detailId_, claimId_, stage_;
    QJsonObject premium_, listen_;
    quint64 generation_ = 0, detailGeneration_ = 0;
    int attempts_ = 0;
    bool busy_ = false, loading_ = false, cancelled_ = false, listenPlayback_ = false, authoritative_ = false;
    QTimer confirmTimer_, expiryTimer_;
};
