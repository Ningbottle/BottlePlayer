#include "UpdateController.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QVersionNumber>
#include <QDesktopServices>
#include <QFile>
#include <QRegularExpression>
#include <QCoreApplication>
#ifdef Q_OS_LINUX
#include <cstdio>
#endif

UpdateController::UpdateController(const QString& profile, QObject* parent)
    : QObject(parent), settings_(QDir(profile).filePath("updates.ini"), QSettings::IniFormat), profile_(profile) {}
UpdateController::~UpdateController() { cancel(); }
QJsonObject UpdateController::selectRelease(const QJsonObject& release, const QString& current, const QString& skipped) {
    auto candidate = release.value("tag_name").toString(); if (candidate.startsWith('v')) candidate.remove(0, 1);
    bool qtAsset = false;
    if (!QRegularExpression("^[0-9]+\\.[0-9]+\\.[0-9]+$").match(candidate).hasMatch() || release.value("draft").toBool() || release.value("prerelease").toBool())
        return {{"message", QStringLiteral("发布信息不是受支持的稳定版本")}};
    for (const auto& value : release.value("assets").toArray()) {
        const auto asset = value.toObject(); const auto name = asset.value("name").toString();
#ifdef Q_OS_LINUX
        if (!QRegularExpression("^BottleMusicQt-[A-Za-z0-9._-]*x86_64[A-Za-z0-9._-]*\\.AppImage$").match(name).hasMatch()) continue;
#else
        if (!QRegularExpression("^BottleMusicQt-[A-Za-z0-9._-]*x64[A-Za-z0-9._-]*\\.(zip|exe)$", QRegularExpression::CaseInsensitiveOption).match(name).hasMatch()) continue;
#endif
        qtAsset = true; const auto digest = asset.value("digest").toString(); const QUrl url(asset.value("browser_download_url").toString());
        if (!QRegularExpression("^sha256:[a-fA-F0-9]{64}$").match(digest).hasMatch() || url.scheme() != "https" || url.host() != "github.com" || !url.userInfo().isEmpty()
            || url.hasQuery() || url.hasFragment() || !url.path().startsWith("/Ningbottle/BottlePlayer/releases/download/") || url.fileName() != name) continue;
        if (QVersionNumber::fromString(candidate) <= QVersionNumber::fromString(current)) return {{"message", QStringLiteral("当前 Qt 版本已是最新")}};
        if (skipped == candidate) return {{"message", QStringLiteral("已跳过此版本，可在发布页查看")}};
        return {{"message", QStringLiteral("发现 Qt 更新 v") + candidate}, {"version", candidate}, {"notes", release.value("body").toString().left(16384)},
            {"asset", url.toString()}, {"digest", digest.mid(7).toLower()}, {"name", name}};
    }
    return {{"message", qtAsset ? QStringLiteral("Qt 更新包缺少可信校验信息，请查看发布页") : QStringLiteral("当前发布尚无 Qt 更新包；其他运行时的安装包不会用于 Qt 更新")}};
}
void UpdateController::cancel() { ++generation_; if (reply_) { reply_->abort(); reply_->deleteLater(); reply_.clear(); } file_.reset(); emit changed(); }
void UpdateController::check() {
    cancel(); manifest_.clear(); version_.clear(); notes_.clear(); asset_.clear(); digest_.clear(); downloaded_.clear(); message_ = QStringLiteral("正在检查 Qt 更新渠道…"); emit changed();
    const auto owner = generation_;
    QNetworkRequest request(QUrl("https://api.github.com/repos/Ningbottle/BottlePlayer/releases/latest"));
    request.setRawHeader("User-Agent", "BottleMusicQt"); request.setTransferTimeout(15000);
    reply_ = network_.get(request); const auto reply = reply_;
    connect(reply, &QNetworkReply::readyRead, this, [this, owner, reply] {
        if (owner != generation_ || !reply) return;
        manifest_ += reply->readAll(); if (manifest_.size() > 2 * 1024 * 1024) { manifest_.clear(); reply->abort(); }
    });
    connect(reply, &QNetworkReply::finished, this, [this, owner, reply] {
        if (!reply || owner != generation_) { if (reply) reply->deleteLater(); return; }
        if (reply->error() != QNetworkReply::NoError) message_ = QStringLiteral("更新检查不可用，请稍后重试或查看发布页");
        else {
            manifest_ += reply->readAll();
            const auto selection = manifest_.size() <= 2 * 1024 * 1024 ? selectRelease(QJsonDocument::fromJson(manifest_).object(), QCoreApplication::applicationVersion(), settings_.value("skipped").toString()) : QJsonObject{{"message", QStringLiteral("发布信息超过读取容量")}};
            message_ = selection.value("message").toString(); version_ = selection.value("version").toString(); notes_ = selection.value("notes").toString();
            asset_ = selection.value("asset").toString(); digest_ = selection.value("digest").toString(); name_ = selection.value("name").toString();
        }
        manifest_.clear(); reply->deleteLater(); reply_.clear(); emit changed();
    });
}
void UpdateController::download() {
    if (asset_.isEmpty() || digest_.size() != 64 || busy()) return;
    cancel(); const auto owner = generation_; const auto directory = QDir(profile_).filePath("updates"); QDir().mkpath(directory);
    file_ = std::make_unique<QSaveFile>(QDir(directory).filePath(name_));
    if (!file_->open(QIODevice::WriteOnly)) { file_.reset(); message_ = QStringLiteral("更新目录无法写入"); emit changed(); return; }
    hash_.reset(); bytes_ = 0; progress_ = 0; message_ = QStringLiteral("正在下载 Qt 更新…");
    QNetworkRequest request{QUrl(asset_)}; request.setTransferTimeout(30000); request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    reply_ = network_.get(request); const auto reply = reply_;
    const auto drain = [this, owner, reply] {
        if (owner != generation_ || !file_) return;
        const auto bytes = reply->readAll(); bytes_ += bytes.size();
        if (bytes_ > 300 * 1024 * 1024 || file_->write(bytes) != bytes.size()) { message_ = QStringLiteral("更新包超限或磁盘写入失败"); reply->abort(); return; }
        hash_.addData(bytes);
    };
    connect(reply, &QNetworkReply::readyRead, this, drain);
    connect(reply, &QNetworkReply::downloadProgress, this, [this, owner](qint64 bytes, qint64 total) { if (owner == generation_) { progress_ = total > 0 ? int(bytes * 100 / total) : 0; emit changed(); } });
    connect(reply, &QNetworkReply::finished, this, [this, owner, reply, drain] {
        if (!reply || owner != generation_) { if (reply) reply->deleteLater(); return; }
        drain();
        if (reply->error() != QNetworkReply::NoError || hash_.result().toHex() != digest_.toLatin1() || !file_ || !file_->commit()) { message_ = QStringLiteral("更新下载或 SHA-256 校验失败，下载文件未保留"); downloaded_.clear(); }
        else { downloaded_ = file_->fileName(); progress_ = 100; message_ = QStringLiteral("Qt 更新下载并校验完成，点击打开更新包以继续安装"); }
        file_.reset(); reply->deleteLater(); reply_.clear(); emit changed();
    });
    emit changed();
}
void UpdateController::openDownloaded() {
    QFile file(downloaded_); if (!file.open(QIODevice::ReadOnly)) return;
    QCryptographicHash verify(QCryptographicHash::Sha256); if (!verify.addData(&file) || verify.result().toHex() != digest_.toLatin1()) { message_ = QStringLiteral("更新文件已变化，已拒绝打开"); downloaded_.clear(); emit changed(); return; }
#ifdef Q_OS_LINUX
    const auto appImage = qEnvironmentVariable("APPIMAGE");
    if (appImage.isEmpty()) {
        message_ = QStringLiteral("更新包已保存在：%1（当前并非从 AppImage 运行，请手动替换）").arg(downloaded_);
        emit changed();
        return;
    }
    const auto replacementPath = appImage + QStringLiteral(".new");
    QSaveFile replacement(replacementPath);
    if (!replacement.open(QIODevice::WriteOnly) || !file.seek(0)) {
        message_ = QStringLiteral("无法准备 AppImage 更新文件");
        emit changed();
        return;
    }
    while (!file.atEnd()) {
        const auto bytes = file.read(1024 * 1024);
        if ((bytes.isEmpty() && !file.atEnd()) || replacement.write(bytes) != bytes.size()) {
            message_ = QStringLiteral("无法写入 AppImage 更新文件");
            emit changed();
            return;
        }
    }
    if (file.error() != QFileDevice::NoError || !replacement.commit()) {
        message_ = QStringLiteral("无法保存 AppImage 更新文件");
        emit changed();
        return;
    }
    const auto executablePermissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
        | QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther;
    if (!QFile::setPermissions(replacementPath, executablePermissions)) {
        QFile::remove(replacementPath);
        message_ = QStringLiteral("无法设置 AppImage 更新文件权限");
        emit changed();
        return;
    }
    const auto source = QFile::encodeName(replacementPath);
    const auto target = QFile::encodeName(appImage);
    if (std::rename(source.constData(), target.constData()) != 0) {
        QFile::remove(replacementPath);
        message_ = QStringLiteral("无法替换 AppImage；下载文件仍保留在：%1").arg(downloaded_);
        emit changed();
        return;
    }
    downloaded_.clear();
    message_ = QStringLiteral("更新已应用；重新启动 BottleMusic 以完成更新");
    emit changed();
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(downloaded_));
#endif
}
void UpdateController::skip() { if (!version_.isEmpty()) { settings_.setValue("skipped", version_); version_.clear(); message_ = QStringLiteral("已跳过此版本"); emit changed(); } }
