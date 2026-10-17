#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QNetworkReply>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QSettings>
#include <QJsonObject>
#include <memory>

class UpdateController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString version READ version NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY changed)
    Q_PROPERTY(bool downloaded READ downloaded NOTIFY changed)
public:
    explicit UpdateController(const QString& profile, QObject* parent = nullptr);
    ~UpdateController() override;
    bool busy() const { return bool(reply_); }
    QString message() const { return message_; }
    QString version() const { return version_; }
    QString notes() const { return notes_; }
    int progress() const { return progress_; }
    bool downloaded() const { return !downloaded_.isEmpty(); }
    Q_INVOKABLE void check();
    Q_INVOKABLE void download();
    Q_INVOKABLE void openDownloaded();
    Q_INVOKABLE void skip();
    Q_INVOKABLE void cancel();
    static QJsonObject selectRelease(const QJsonObject& release, const QString& currentVersion, const QString& skippedVersion);
signals:
    void changed();
private:
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QSettings settings_;
    QString profile_, message_, version_, notes_, asset_, digest_, name_, downloaded_;
    std::unique_ptr<QSaveFile> file_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
    int progress_ = 0;
    qint64 bytes_ = 0;
    quint64 generation_ = 0;
    QByteArray manifest_;
};
