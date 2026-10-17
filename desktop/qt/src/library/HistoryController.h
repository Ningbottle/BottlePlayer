#pragma once
#include "core/NativeGateway.h"
#include <QSettings>
#include <QVariantList>

class HistoryController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString scope READ scope WRITE setScope NOTIFY changed)
public:
    HistoryController(NativeGateway& gateway, const QString& profile, QObject* parent = nullptr);
    QVariantList tracks() const;
    bool loading() const { return loading_; }
    QString error() const { return error_; }
    QString scope() const { return scope_; }
    void setScope(const QString& scope);
    void setAccount(const QString& userId);
    void started(const QVariantMap& track);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setActive(bool active);
    Q_INVOKABLE void play(int index);
signals:
    void changed();
    void playRequested(const QVariantList& tracks, int index);
private:
    NativeGateway& gateway_;
    QSettings settings_;
    QVariantList local_, remote_;
    QString account_, request_, error_, scope_ = "all";
    quint64 generation_ = 0;
    bool active_ = false, loading_ = false;
};
