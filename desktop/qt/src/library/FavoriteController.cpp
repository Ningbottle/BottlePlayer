#include "FavoriteController.h"
#include "music/Track.h"
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QUuid>
#include <algorithm>

namespace {
QString hash(const QVariantMap& row) { return row.value("hash").toString().trimmed().toLower(); }
bool numeric(const QString& value) { return QRegularExpression("^[0-9]+$").match(value).hasMatch(); }
bool cloudTrack(const QVariantMap& row) { return QRegularExpression("^[a-f0-9]{32}$").match(hash(row)).hasMatch(); }
bool success(const QJsonObject& response) { return response.value("status").toVariant().toInt() == 1; }
bool transport(const QJsonObject& response) {
    return response.value("_transportError").toBool() || response.value("_httpStatus").toInt() >= 500
        || QStringList{"timeout", "deadline_exceeded", "circuit_open", "network_error", "request_failed"}.contains(response.value("error_code").toString());
}
}
FavoriteController::FavoriteController(NativeGateway& gateway, const QString& profile, QObject* parent)
    : QObject(parent), gateway_(gateway), settings_(QDir(profile).filePath("favorites.ini"), QSettings::IniFormat) {
    anonymous_ = QJsonDocument::fromJson(settings_.value("anonymous").toByteArray()).array().toVariantList();
    QSettings legacy(QDir(profile).filePath("preferences.ini"), QSettings::IniFormat);
    if (settings_.value("legacyImported", false).toBool()) return;
    for (const auto& value : legacy.value("favorites").toStringList()) {
        if (std::none_of(anonymous_.begin(), anonymous_.end(), [&value](const auto& entry) { return hash(entry.toMap()) == value.toLower(); }))
            anonymous_.append(QVariantMap{{"hash", value}, {"title", QStringLiteral("本地收藏记录")}});
    }
    settings_.setValue("anonymous", QJsonDocument(QJsonArray::fromVariantList(anonymous_)).toJson(QJsonDocument::Compact));
    settings_.setValue("legacyImported", true); settings_.sync();
}
void FavoriteController::removeLocal(const QString& value) {
    const auto identity = value.trimmed().toLower();
    anonymous_.erase(std::remove_if(anonymous_.begin(), anonymous_.end(), [&identity](const auto& row) { return hash(row.toMap()) == identity; }), anonymous_.end());
    settings_.setValue("anonymous", QJsonDocument(QJsonArray::fromVariantList(anonymous_)).toJson(QJsonDocument::Compact)); settings_.sync();
    if (settings_.status() != QSettings::NoError) emit notice(QStringLiteral("本地收藏暂时无法保存"));
    emit changed();
}
FavoriteController::~FavoriteController() { gateway_.cancel(request_); gateway_.cancel(mutationId_); for (const auto& id : manualRequests_) gateway_.cancel(id); }
QString FavoriteController::key(const QString& name) const { return QString::fromLatin1(account_.toUtf8().toHex()) + "/" + name; }
void FavoriteController::setAccount(const QString& account) {
    if (account_ == account) return;
    ++generation_; ++loadGeneration_; gateway_.cancel(request_); gateway_.cancel(mutationId_); for (const auto& id : manualRequests_) gateway_.cancel(id); manualRequests_.clear(); account_ = account;
    confirmed_.clear(); fileIds_.clear(); pages_.clear(); liked_.clear();
    ownedPlaylists_.clear(); loading_ = complete_ = mutating_ = reloadRequested_ = false; message_.clear(); total_ = 0; outbox_.clear();
    if (!account_.isEmpty()) for (const auto& value : QJsonDocument::fromJson(settings_.value(key("outbox")).toByteArray()).array()) {
        const auto row = value.toObject();
        if (row.value("desired").isBool() && !row.value("revision").toString().isEmpty() && cloudTrack(row.value("track").toObject().toVariantMap())) outbox_.append(row.toVariantMap());
    }
    emit changed();
}
void FavoriteController::setPlaylists(const QVariantList& playlists) {
    if (account_.isEmpty()) return;
    const auto remembered = settings_.value(key("likedListId")).toString();
    ownedPlaylists_.clear();
    for (const auto& value : playlists) { const auto row = value.toMap(); if (numeric(row.value("listid").toString())) ownedPlaylists_.insert(row.value("id").toString(), row); }
    QVariantMap chosen;
    for (const auto& value : playlists) {
        const auto row = value.toMap(); const auto listId = row.value("listid").toString();
        if (!numeric(listId)) continue;
        if ((!remembered.isEmpty() && remembered == listId) || (remembered.isEmpty()
            && QRegularExpression("我喜欢|喜欢的音乐|我的最爱|我的收藏|favorites?|liked\\s*songs?", QRegularExpression::CaseInsensitiveOption).match(row.value("name").toString()).hasMatch())) { chosen = row; break; }
    }
    if (chosen.isEmpty()) return;
    if (liked_.value("id") == chosen.value("id") && (loading_ || complete_)) return;
    liked_ = chosen; settings_.setValue(key("likedListId"), chosen.value("listid")); retry();
}
bool FavoriteController::isFavorite(const QVariantMap& track) const {
    const auto identity = hash(track);
    if (account_.isEmpty() || !cloudTrack(track)) return std::any_of(anonymous_.begin(), anonymous_.end(), [&identity](const auto& row) { return hash(row.toMap()) == identity; });
    for (const auto& value : outbox_) { const auto operation = value.toMap(); if (hash(operation.value("track").toMap()) == identity) return operation.value("desired").toBool(); }
    return confirmed_.contains(identity);
}
void FavoriteController::toggle(const QVariantMap& track) {
    if (hash(track).isEmpty()) return;
    if (account_.isEmpty() || !cloudTrack(track)) {
        const bool desired = !isFavorite(track); const auto identity = hash(track);
        anonymous_.erase(std::remove_if(anonymous_.begin(), anonymous_.end(), [&identity](const auto& value) { return hash(value.toMap()) == identity; }), anonymous_.end());
        if (desired) anonymous_.prepend(track);
        settings_.setValue("anonymous", QJsonDocument(QJsonArray::fromVariantList(anonymous_)).toJson(QJsonDocument::Compact)); settings_.sync();
        message_ = settings_.status() == QSettings::NoError ? desired ? QStringLiteral("已收藏到本地") : QStringLiteral("已取消本地收藏") : QStringLiteral("收藏仅保留在内存，本地保存失败");
        emit changed(); emit notice(message_); return;
    }
    if (!complete_) { message_ = liked_.isEmpty() ? QStringLiteral("尚未找到「我喜欢的音乐」歌单，请刷新个人歌单") : QStringLiteral("收藏歌单尚未完整加载，请稍后重试"); emit notice(message_); return; }
    const auto identity = hash(track); const bool desired = !isFavorite(track);
    outbox_.erase(std::remove_if(outbox_.begin(), outbox_.end(), [&identity](const auto& value) { return hash(value.toMap().value("track").toMap()) == identity; }), outbox_.end());
    outbox_.append(QVariantMap{{"revision", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"track", track}, {"desired", desired}});
    const bool persisted = saveOutbox(); message_ = persisted ? QStringLiteral("正在同步收藏…") : QStringLiteral("待同步操作只保留在内存，退出后不会自动重试"); emit changed(); flushNext();
}
bool FavoriteController::saveOutbox() {
    settings_.setValue(key("outbox"), QJsonDocument(QJsonArray::fromVariantList(outbox_)).toJson(QJsonDocument::Compact)); settings_.sync();
    return settings_.status() == QSettings::NoError;
}
void FavoriteController::retry() {
    if (account_.isEmpty() || liked_.isEmpty() || !gateway_.ready()) return;
    if (mutating_) { reloadRequested_ = true; return; }
    reloadRequested_ = false;
    ++loadGeneration_; gateway_.cancel(request_); pages_.clear(); total_ = 0;
    loading_ = true; complete_ = false; message_.clear(); emit changed(); loadPage(loadGeneration_, 1);
}
void FavoriteController::loadPage(quint64 owner, int page) {
    request_ = gateway_.get("/playlist/track/all", {{"id", liked_.value("id").toString()}, {"page", page}, {"pagesize", 50}}, this, [this, owner, page](const QJsonObject& response) {
        if (owner != loadGeneration_) return;
        request_.clear();
        if (!success(response)) { loading_ = false; message_ = QStringLiteral("收藏歌单加载失败，可重试"); emit changed(); return; }
        if (page == 1) { confirmed_.clear(); fileIds_.clear(); }
        const auto rows = songList(response); const auto data = response.value("data").toObject(); total_ = qMax(total_, data.value("total").toInt());
        const auto signature = QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(rows).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
        if (!rows.isEmpty() && pages_.contains(signature)) { loading_ = false; message_ = QStringLiteral("收藏歌单分页重复，无法确认完整状态"); emit changed(); return; }
        pages_.insert(signature);
        for (const auto& value : rows) { const auto track = Track::fromJson(value.toObject()); if (track.hash.isEmpty()) continue; const auto identity = track.hash.toLower(); confirmed_.insert(identity); if (numeric(track.fileId)) fileIds_.insert(identity, track.fileId); }
        if ((total_ > 0 && confirmed_.size() >= total_) || rows.size() < 50) {
            loading_ = false; complete_ = total_ == 0 || confirmed_.size() >= total_;
            if (!complete_) message_ = QStringLiteral("收藏歌单提前结束，无法确认完整状态");
        } else if (page >= 40) { loading_ = false; message_ = QStringLiteral("收藏歌单达到分页上限，无法确认完整状态"); }
        emit changed();
        if (loading_) loadPage(owner, page + 1); else if (complete_) flushNext();
    });
}
void FavoriteController::flushNext() {
    if (mutating_ || !complete_ || account_.isEmpty() || outbox_.isEmpty()) return;
    const auto operation = outbox_.first().toMap(), track = operation.value("track").toMap();
    const auto identity = hash(track), revision = operation.value("revision").toString(); const bool desired = operation.value("desired").toBool();
    if (confirmed_.contains(identity) == desired) { outbox_.removeFirst(); saveOutbox(); emit changed(); flushNext(); return; }
    const auto fileId = fileIds_.value(identity);
    if (!desired && !numeric(fileId)) { message_ = QStringLiteral("缺少曲目 fileid，无法取消云收藏"); emit notice(message_); return; }
    const auto listId = liked_.value("listid").toString(); if (!numeric(listId)) return;
    const auto owner = generation_; mutating_ = true;
    const QJsonObject body = desired ? QJsonObject{{"listid", listId}, {"data", QJsonArray{QJsonObject{{"name", track.value("title").toString()}, {"hash", track.value("hash").toString()}, {"album_id", track.value("albumId").toString()}, {"mixsongid", track.value("audioId").toString()}}}}} : QJsonObject{};
    mutationId_ = gateway_.request("POST", desired ? "/playlist/tracks/add" : "/playlist/tracks/del", desired ? QJsonObject{} : QJsonObject{{"listid", listId}, {"fileids", fileId}}, body, this,
        [this, owner, identity, revision, desired](const QJsonObject& response) {
            if (owner != generation_) return;
            mutationId_.clear(); mutating_ = false;
            if (success(response)) {
                if (desired) confirmed_.insert(identity); else { confirmed_.remove(identity); fileIds_.remove(identity); }
                message_ = desired ? QStringLiteral("云收藏已确认") : QStringLiteral("已取消云收藏");
            } else if (transport(response)) {
                message_ = settings_.status() == QSettings::NoError ? QStringLiteral("网络异常，收藏操作等待同步") : QStringLiteral("网络异常，操作仅保留在内存"); emit changed(); emit notice(message_); return;
            } else message_ = response.value("error").toString(QStringLiteral("云收藏未被接受"));
            outbox_.erase(std::remove_if(outbox_.begin(), outbox_.end(), [&revision](const auto& value) { return value.toMap().value("revision").toString() == revision; }), outbox_.end());
            saveOutbox(); emit changed(); emit notice(message_);
            if (success(response)) emit playlistChanged(liked_.value("id").toString());
            // Refresh to obtain authoritative fileids after adds and before replaying pending operations.
            if ((desired && success(response)) || reloadRequested_) retry(); else flushNext();
        });
}
void FavoriteController::syncLocal() {
    if (account_.isEmpty() || !complete_) return;
    for (const auto& value : anonymous_) {
        const auto row = value.toMap(); if (!cloudTrack(row) || isFavorite(row)) continue;
        outbox_.append(QVariantMap{{"revision", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"track", row}, {"desired", true}});
    }
    saveOutbox(); emit changed(); flushNext();
}
void FavoriteController::addToPlaylist(const QVariantMap& playlist, const QVariantMap& track) { modifyPlaylist(playlist, track, true); }
void FavoriteController::removeFromPlaylist(const QVariantMap& playlist, const QVariantMap& track) { modifyPlaylist(playlist, track, false); }
void FavoriteController::modifyPlaylist(const QVariantMap& playlist, const QVariantMap& track, bool add) {
    const auto listId = playlist.value("listid").toString(), fileId = track.value("fileId").toString();
    if (account_.isEmpty() || !numeric(listId) || ownedPlaylists_.value(playlist.value("id").toString()).value("listid").toString() != listId
        || !cloudTrack(track) || (!add && !numeric(fileId))) { emit notice(QStringLiteral("缺少账户、歌单或曲目有效标识")); return; }
    const auto owner = generation_;
    const QJsonObject body = add ? QJsonObject{{"listid", listId}, {"data", QJsonArray{QJsonObject{{"name", track.value("title").toString()}, {"hash", track.value("hash").toString()}, {"album_id", track.value("albumId").toString()}, {"mixsongid", track.value("audioId").toString()}}}}} : QJsonObject{};
    const auto ticket = std::make_shared<QString>();
    *ticket = gateway_.request("POST", add ? "/playlist/tracks/add" : "/playlist/tracks/del", add ? QJsonObject{} : QJsonObject{{"listid", listId}, {"fileids", fileId}}, body, this,
        [this, owner, ticket, id = playlist.value("id").toString(), add](const QJsonObject& response) {
            manualRequests_.remove(*ticket);
            if (owner != generation_) return;
            if (success(response)) { emit notice(add ? QStringLiteral("已添加到歌单") : QStringLiteral("已从歌单移除")); emit playlistChanged(id); if (id == liked_.value("id").toString()) retry(); }
            else emit notice(response.value("error").toString(QStringLiteral("歌单修改失败，请重试")));
        });
    if (!ticket->isEmpty()) manualRequests_.insert(*ticket);
}
