#pragma once
#include "core/NativeGateway.h"
#include <QVariantList>
#include <QTimer>

class StatsController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString range READ range WRITE setRange NOTIFY changed)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY changed)
    Q_PROPERTY(QVariantList songs READ songs NOTIFY changed)
    Q_PROPERTY(QVariantList artists READ artists NOTIFY changed)
    Q_PROPERTY(QVariantList albums READ albums NOTIFY changed)
    Q_PROPERTY(QVariantList timeline READ timeline NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit StatsController(NativeGateway& gateway, QObject* parent = nullptr);
    QString range() const { return range_; }
    QVariantMap summary() const { return summary_; }
    QVariantList songs() const { return songs_; }
    QVariantList artists() const { return artists_; }
    QVariantList albums() const { return albums_; }
    QVariantList timeline() const { return timeline_; }
    bool loading() const { return pending_ > 0; }
    QString error() const { return error_; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setActive(bool active);
    void setRange(const QString& range);
    void record(const QJsonObject& record);
signals:
    void changed();
    void recordCommitted();
    void notice(const QString& message);
private:
    NativeGateway& gateway_;
    QString range_ = "30d", error_;
    QVariantMap summary_;
    QVariantList songs_, artists_, albums_, timeline_;
    QStringList requests_;
    quint64 generation_ = 0;
    int pending_ = 0;
    bool active_ = false;
    QTimer refreshTimer_;
};
