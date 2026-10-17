#include "AiAnalyzer.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>

AiAnalyzer::~AiAnalyzer() { cancel(); }
void AiAnalyzer::cancel() { ++generation_; if (reply_) { reply_->abort(); reply_->deleteLater(); reply_.clear(); } emit changed(); }
void AiAnalyzer::analyze(const QString& key, const QVariantMap& summary, const QVariantList& songs, const QVariantList& timeline) {
    if (key.trimmed().isEmpty()) { error_ = QStringLiteral("请输入 API Key"); emit changed(); return; }
    cancel(); result_.clear(); error_.clear(); response_.clear(); oversized_ = false; const auto owner = generation_;
    const QJsonObject statistics{{"summary", QJsonObject::fromVariantMap(summary)}, {"topSongs", QJsonArray::fromVariantList(songs)}, {"timeline", QJsonArray::fromVariantList(timeline)}};
    const QJsonObject payload{{"model", "deepseek-chat"}, {"stream", false}, {"messages", QJsonArray{
        QJsonObject{{"role", "system"}, {"content", "You are a music listening analyst. Analyze music taste, listening patterns and recommendations. Be concise and respond in Chinese."}},
        QJsonObject{{"role", "user"}, {"content", QStringLiteral("请分析以下聆听统计：\n") + QString::fromUtf8(QJsonDocument(statistics).toJson(QJsonDocument::Compact))}}}}};
    QNetworkRequest request(QUrl("https://api.deepseek.com/chat/completions"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"); request.setRawHeader("Authorization", "Bearer " + key.trimmed().toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy); request.setTransferTimeout(30000);
    reply_ = network_.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    const auto reply = reply_;
    connect(reply, &QNetworkReply::readyRead, this, [this, owner, reply] {
        if (owner != generation_ || !reply) return;
        response_ += reply->readAll();
        if (response_.size() > 1024 * 1024) { oversized_ = true; response_.clear(); reply->abort(); }
    });
    connect(reply, &QNetworkReply::finished, this, [this, owner, reply] {
        if (!reply) return;
        if (owner != generation_) { reply->deleteLater(); return; }
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (oversized_) error_ = QStringLiteral("分析返回内容过大，请稍后重试");
        else if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) error_ = QStringLiteral("分析请求失败（HTTP %1），请检查网络、额度和 API Key").arg(status);
        else {
            response_ += reply->readAll();
            const auto choices = response_.size() <= 1024 * 1024 ? QJsonDocument::fromJson(response_).object().value("choices").toArray() : QJsonArray{};
            result_ = choices.isEmpty() ? QString{} : choices.first().toObject().value("message").toObject().value("content").toString();
            if (result_.isEmpty()) error_ = QStringLiteral("分析服务没有返回有效内容");
        }
        response_.clear(); reply->deleteLater(); reply_.clear(); emit changed();
    });
    emit changed();
}
