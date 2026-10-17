#pragma once
#include <QObject>
#include <QJsonObject>
#include <atomic>
#include <functional>
#include <memory>

// Blocking C ABI requests run on owned workers, never the QML / scene graph thread.
class NativeGateway final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
public:
    using Callback = std::function<void(QJsonObject)>;
    explicit NativeGateway(QString dllPath, QString profile, QObject* parent = nullptr);
    ~NativeGateway() override;
    void initialize(Callback callback);
    bool ready() const;
    QString get(const QString& path, const QJsonObject& query, QObject* receiver, Callback callback);
    QString request(const QString& method, const QString& path, const QJsonObject& query,
                    const QJsonObject& body, QObject* receiver, Callback callback);
    QString stats(const QString& operation, const QJsonObject& arguments, QObject* receiver, Callback callback);
    QString recordPlay(const QJsonObject& record, QObject* receiver, Callback callback);
    QString archiveStats(const QString& source, const QString& destination, bool importing,
                         std::shared_ptr<std::atomic_bool> stopped, QObject* receiver, Callback callback);
    void cancel(const QString& id);
    QString fingerprint() const;
signals:
    void readyChanged();
private:
    struct Runtime;
    struct WorkState;
    QString dispatch(std::function<QJsonObject(const QString&)> work, QObject* receiver, Callback callback,
                     bool cancelable = true, bool nativeRequest = false);
    std::shared_ptr<Runtime> runtime_;
    std::unique_ptr<WorkState> work_;
};
