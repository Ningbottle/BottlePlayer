#include "LibraryController.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <utility>

namespace {
constexpr int pageSize = 50, pageLimit = 40;
QString text(const QJsonObject& object, std::initializer_list<const char*> aliases) {
    for (const auto* key : aliases) {
        const auto value = object.value(QLatin1String(key));
        const auto string = value.isDouble() ? QString::number(value.toInteger()) : value.toString().trimmed();
        if (!string.isEmpty()) return string;
    }
    return {};
}
bool success(const QJsonObject& response) { return response.value("status").toVariant().toInt() == 1; }
}
LibraryController::LibraryController(NativeGateway& gateway, QObject* parent) : QObject(parent), gateway_(gateway) {}
LibraryController::~LibraryController() { gateway_.cancel(listRequest_); gateway_.cancel(trackRequest_); }
QVariantList LibraryController::tracks() const {
    QVariantList result; result.reserve(tracks_.size());
    for (const auto& track : tracks_) result.append(track.toVariant());
    return result;
}
void LibraryController::setAccount(const QString& userId) {
    if (account_ == userId) return;
    gateway_.cancel(listRequest_); gateway_.cancel(trackRequest_); ++listEpoch_; ++trackEpoch_;
    account_ = userId; pendingPlay_ = -1; playlists_.clear(); tracks_.clear(); listIds_.clear(); trackIds_.clear();
    id_.clear(); name_.clear(); image_.clear(); error_.clear(); listsError_.clear();
    loading_ = listsLoading_ = listsMore_ = complete_ = public_ = album_ = false; total_ = 0; emit changed();
    if (!account_.isEmpty()) refreshPlaylists();
}
void LibraryController::refreshPlaylists() {
    if (account_.isEmpty() || !gateway_.ready()) return;
    gateway_.cancel(listRequest_); ++listEpoch_; listPage_ = 0; listsLoading_ = true; listsError_.clear(); emit changed();
    loadListsPage(listEpoch_, 1);
}
void LibraryController::morePlaylists() {
    if (!listsMore_ || listsLoading_ || listPage_ >= pageLimit) return;
    listsLoading_ = true; emit changed(); loadListsPage(listEpoch_, listPage_ + 1);
}
void LibraryController::loadListsPage(quint64 epoch, int page) {
    listRequest_ = gateway_.get("/user/playlist", {{"page", page}, {"pagesize", pageSize}}, this, [this, epoch, page](QJsonObject response) {
        if (epoch != listEpoch_) return;
        listRequest_.clear(); listsLoading_ = false;
        if (!success(response)) { listsError_ = QStringLiteral("个人歌单获取失败，请刷新账户或稍后重试"); emit changed(); return; }
        const auto data = response.value("data").toObject();
        auto rows = data.value("info").toArray(); if (rows.isEmpty()) rows = data.value("list").toArray();
        if (page == 1) { playlists_.clear(); listIds_.clear(); }
        int added = 0, invalid = 0;
        for (const auto& raw : rows) {
            const auto row = raw.toObject();
            const auto id = text(row, {"global_collection_id", "global_collectionid", "gid"});
            if (!id.startsWith(QLatin1String("collection_"))) { ++invalid; continue; }
            if (listIds_.contains(id)) continue;
            listIds_.insert(id); ++added;
            const auto name = text(row, {"name", "listname", "list_name", "specialname", "special_name", "title"});
            auto cover = text(row, {"pic", "cover", "imgurl", "sizable_cover"}); cover.replace("{size}", "400");
            const int count = qMax(0, text(row, {"count", "song_count", "songcount", "list_song_count"}).toInt());
            playlists_.append(QVariantMap{{"id", id}, {"listid", text(row, {"listid", "list_id", "list_create_listid"})}, {"name", name.isEmpty() ? QStringLiteral("未命名歌单") : name}, {"image", cover}, {"count", count}});
        }
        listPage_ = page; listsMore_ = rows.size() >= pageSize && added > 0 && page < pageLimit;
        if (invalid) listsError_ = QStringLiteral("部分歌单缺少有效标识，已跳过");
        if (rows.size() >= pageSize && added == 0) listsError_ = QStringLiteral("歌单分页返回重复数据，请刷新重试");
        emit changed();
    });
}
void LibraryController::openPlaylist(const QString& id) {
    for (const auto& raw : playlists_) {
        const auto row = raw.toMap(); if (row.value("id").toString() != id) continue;
        public_ = album_ = false; id_ = id; name_ = row.value("name").toString(); image_ = row.value("image").toString(); retryTracks(); return;
    }
}
void LibraryController::openPublicPlaylist(const QVariantMap& playlist) {
    const auto id = playlist.value("id").toString();
    if (!QRegularExpression("^[0-9]+$").match(id).hasMatch() || id == "0") return;
    album_ = false; public_ = true; id_ = id; name_ = playlist.value("name").toString(); image_ = playlist.value("image").toString(); retryTracks();
}
void LibraryController::openAlbum(const QVariantMap& album) {
    const auto id = album.value("id").toString();
    if (!QRegularExpression("^[1-9][0-9]*$").match(id).hasMatch()) return;
    public_ = album_ = true; id_ = id; name_ = album.value("name").toString(); image_ = album.value("image").toString(); retryTracks();
}
void LibraryController::retryTracks() {
    if ((!public_ && account_.isEmpty()) || id_.isEmpty() || !gateway_.ready()) return;
    gateway_.cancel(trackRequest_); ++trackEpoch_; pendingPlay_ = -1; tracks_.clear(); trackIds_.clear(); pageSignatures_.clear();
    total_ = 0; loading_ = true; complete_ = false; error_.clear(); emit changed(); loadTracksPage(trackEpoch_, 1);
}
void LibraryController::cancelTracks() {
    gateway_.cancel(trackRequest_); ++trackEpoch_; pendingPlay_ = -1; trackRequest_.clear(); loading_ = false;
    if (!complete_) error_ = QStringLiteral("曲目加载已停止，可重新加载"); emit changed();
}
void LibraryController::loadTracksPage(quint64 epoch, int page) {
    trackRequest_ = gateway_.get(album_ ? "/album/songs" : "/playlist/track/all", {{"id", id_}, {"page", page}, {"pagesize", pageSize}}, this, [this, epoch, page](QJsonObject response) {
        if (epoch != trackEpoch_) return;
        trackRequest_.clear();
        if (!success(response)) { loading_ = false; error_ = QStringLiteral("部分曲目未能载入，请重新加载"); emit changed(); return; }
        const auto rows = songList(response); const auto data = response.value("data").toObject();
        if (data.value("total").isDouble()) total_ = qMax(total_, data.value("total").toInt());
        const auto signature = QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(rows).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
        if (!rows.isEmpty() && pageSignatures_.contains(signature)) { loading_ = false; error_ = QStringLiteral("分页返回重复曲目，歌单尚未完整加载"); emit changed(); return; }
        pageSignatures_.insert(signature); int added = 0;
        for (const auto& raw : rows) {
            const auto object = raw.toObject(); auto track = Track::fromJson(object);
            if (track.hash.isEmpty()) continue;
            const auto identity = text(object, {"fileid", "FileID", "songmid", "SongMid"});
            const auto stable = identity.isEmpty() ? track.hash.toLower() + "|" + track.audioId : identity;
            if (trackIds_.contains(stable)) continue;
            trackIds_.insert(stable); tracks_.append(std::move(track)); ++added;
        }
        if ((total_ > 0 && tracks_.size() >= total_) || rows.size() < pageSize) {
            // A short page is the upstream's last page. Its `total` still counts
            // rows dropped above (no hash, duplicates), so it is not an error.
            loading_ = false; complete_ = true;
            total_ = int(tracks_.size());
        } else if (page >= pageLimit || added == 0) {
            loading_ = false; error_ = QStringLiteral("已停止分页，歌单尚未完整加载");
        }
        emit changed();
        if (loading_) loadTracksPage(epoch, page + 1);
        else if (complete_ && pendingPlay_ >= 0) { const int index = std::exchange(pendingPlay_, -1); play(index); }
    });
}
void LibraryController::play(int index) {
    if (index < 0 || index >= tracks_.size()) return;
    if (loading_) { pendingPlay_ = index; return; }
    if (!complete_) { pendingPlay_ = -1; error_ = QStringLiteral("曲目尚未完整加载，请重新加载后播放"); emit changed(); return; }
    emit playRequested(tracks(), index);
}
