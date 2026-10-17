#pragma once
#include "core/NativeGateway.h"
#include <QVariantMap>

class SettingsController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap device READ device NOTIFY changed)
    Q_PROPERTY(QVariantMap memory READ memory NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString profilePath READ profilePath CONSTANT)
    Q_PROPERTY(QString diagnosticText READ diagnosticText NOTIFY changed)
public:
    SettingsController(NativeGateway& gateway, const QString& profile, QObject* parent = nullptr);
    QVariantMap device() const { return device_; }
    QVariantMap memory() const { return memory_; }
    bool busy() const { return busy_; }
    QString message() const { return message_; }
    QString profilePath() const { return profile_; }
    QString diagnosticText() const;
    Q_INVOKABLE void loadDevice();
    Q_INVOKABLE void saveDevice(const QString& dfid, const QString& mid, const QString& uuid);
    Q_INVOKABLE void resetDevice();
    Q_INVOKABLE void testDevice();
    Q_INVOKABLE void loadDiagnostics();
    Q_INVOKABLE void copyDiagnostics();
    Q_INVOKABLE void openProfileFolder();
    Q_INVOKABLE void openExternal(const QString& url);
signals:
    void changed();
private:
    void updateDevice(const QJsonObject& query);
    NativeGateway& gateway_;
    QString profile_, message_;
    QVariantMap device_, memory_;
    bool busy_ = false;
    quint64 deviceGeneration_ = 0, memoryGeneration_ = 0;
    QString deviceRequest_, memoryRequest_;
};
