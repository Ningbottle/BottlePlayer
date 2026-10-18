#pragma once
#include <QObject>
#include <QUrl>
#include <QJsonObject>
#include <QPointer>
#include <atomic>
#include <memory>
#include <functional>
class NativeGateway;

class DataMigration final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit DataMigration(QString profile, QObject* parent = nullptr);
    ~DataMigration() override;
    QString message() const { return message_; }
    bool restartNeeded() const { return restart_; }
    bool busy() const { return busy_; }
    void setNativeGateway(NativeGateway* gateway);
    void setPrepareSnapshot(std::function<bool()> prepare) { prepareSnapshot_ = std::move(prepare); }
    Q_INVOKABLE void exportData(const QUrl& file);
    Q_INVOKABLE void importData(const QUrl& file);
    Q_INVOKABLE void restartApp();
    Q_INVOKABLE void exportStatistics(const QUrl& file);
    Q_INVOKABLE void importStatistics(const QUrl& file);
    Q_INVOKABLE void cancelStatistics();
    static QJsonObject sanitizeSnapshot(const QJsonObject& snapshot, bool legacy);
signals:
    void changed();
    void importing();
    void statisticsImported();
private:
    QString profile_, message_;
    bool restart_ = false;
    bool busy_ = false;
    QPointer<NativeGateway> gateway_;
    std::shared_ptr<std::atomic_bool> stopped_;
    std::function<bool()> prepareSnapshot_;
};
