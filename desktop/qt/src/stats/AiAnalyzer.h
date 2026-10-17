#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QNetworkReply>
#include <QJsonObject>

class AiAnalyzer final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString result READ result NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit AiAnalyzer(QObject* parent = nullptr) : QObject(parent) {}
    ~AiAnalyzer() override;
    bool loading() const { return bool(reply_); }
    QString result() const { return result_; }
    QString error() const { return error_; }
    Q_INVOKABLE void analyze(const QString& apiKey, const QVariantMap& summary, const QVariantList& songs, const QVariantList& timeline);
    Q_INVOKABLE void cancel();
signals:
    void changed();
private:
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QString result_, error_;
    quint64 generation_ = 0;
    QByteArray response_;
    bool oversized_ = false;
};
