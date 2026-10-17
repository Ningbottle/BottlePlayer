#include "HomeController.h"
#include "music/Track.h"
#include <QDate>
#include <QJsonArray>
#include <QRegularExpression>

HomeController::HomeController(NativeGateway& gateway, QObject* parent) : QObject(parent), gateway_(gateway) {
    connect(&gateway_, &NativeGateway::readyChanged, this, [this] { if (gateway_.ready() && pending_ == 0 && day_.isEmpty()) refresh(); });
    dayTimer_.setInterval(60000); connect(&dayTimer_, &QTimer::timeout, this, [this] { if (active_ && day_ != QDate::currentDate().toString(Qt::ISODate)) refresh(); }); dayTimer_.start();
}
void HomeController::setAccount(const QString& value) { if (account_ == value) return; account_ = value; ++generation_; for (const auto& id : requests_) gateway_.cancel(id); requests_.clear(); daily_.clear(); recommendations_.clear(); playlists_.clear(); albums_.clear(); pending_ = 0; emit tracksReady({}); emit changed(); if (gateway_.ready()) refresh(); }
void HomeController::refresh() {
    if (!gateway_.ready() || pending_ > 0) return;
    for (const auto& id : requests_) gateway_.cancel(id); requests_.clear(); const auto owner = ++generation_;
    ++metadataGeneration_; for (const auto& id : metadataRequests_) gateway_.cancel(id); metadataRequests_.clear(); metadataPending_ = 0; attemptedAlbums_.clear();
    day_ = QDate::currentDate().toString(Qt::ISODate); pending_ = 4; error_.clear(); emit changed();
    daily(owner, account_.isEmpty()); collections(owner, 2); collections(owner, 5); loadRecommendedAlbums(owner);
}
void HomeController::daily(quint64 owner, bool fallback) {
    requests_.append(gateway_.get(fallback ? "/top/song" : "/everyday/recommend", {{"pagesize", 30}}, this, [this, owner, fallback](const QJsonObject& response) {
        if (owner != generation_) return;
        const auto tracks = parseTracks(response);
        if (tracks.isEmpty() && !fallback) { daily(owner, true); return; }
        QVariantList result; for (const auto& track : tracks) result.append(track.toVariant());
        if (!result.isEmpty()) { daily_ = result; emit tracksReady(result); loadAlbumMetadata(); } else error_ = QStringLiteral("推荐暂不可用，可刷新或搜索歌曲");
        --pending_; emit changed();
    }));
}
void HomeController::setActive(bool active) {
    if (active_ == active) return; active_ = active;
    if (!active) { ++metadataGeneration_; for (const auto& id : metadataRequests_) gateway_.cancel(id); metadataRequests_.clear(); metadataPending_ = 0; }
    else if (day_ != QDate::currentDate().toString(Qt::ISODate)) refresh();
    else loadAlbumMetadata();
}
void HomeController::loadRecommendedAlbums(quint64 owner) {
    requests_.append(gateway_.get("/top/album", {{"pagesize", 24}}, this, [this, owner](const QJsonObject& response) {
        if (owner != generation_) return;
        QJsonArray rows; auto value = response.value("data");
        // The real endpoint groups independent new albums by market.
        for (const auto* market : {"chn", "eur", "jpn", "kor"})
            for (const auto& album : value.toObject().value(QLatin1String(market)).toArray()) rows.append(album);
        for (int depth = 0; depth < 4 && rows.isEmpty(); ++depth) {
            if (value.isArray()) { rows = value.toArray(); break; }
            const auto object = value.toObject();
            for (const auto* key : {"albums", "album_list", "albumlist", "info", "list"}) {
                if (object.value(QLatin1String(key)).isArray()) { rows = object.value(QLatin1String(key)).toArray(); break; }
            }
            value = object.value("data");
        }
        const auto text = [](const QJsonObject& object, std::initializer_list<const char*> names) {
            for (const auto* key : names) { const auto v = object.value(QLatin1String(key)); const auto s = v.isDouble() ? QString::number(v.toInteger()) : v.toString(); if (!s.isEmpty()) return s; } return QString{};
        };
        QVariantList albums; QSet<QString> seen;
        if (response.value("status").toInt() == 1) for (const auto& raw : rows) {
            auto row = raw.toObject(); if (row.value("album_info").isObject()) row = row.value("album_info").toObject();
            const auto id = text(row, {"album_id", "albumid", "AlbumID", "AlbumId", "id"});
            const auto name = text(row, {"album_name", "albumname", "AlbumName", "name"});
            if (!QRegularExpression("^[1-9][0-9]*$").match(id).hasMatch() || name.isEmpty() || seen.contains(id)) continue;
            seen.insert(id);
            albums.append(QVariantMap{{"id", id}, {"name", name}, {"album", true},
                {"image", text(row, {"sizable_cover", "imgurl", "cover", "pic", "image"}).replace("{size}", "400")},
                {"artist", text(row, {"singer_name", "singername", "SingerName", "author_name", "artist"})}});
        }
        recommendations_ = albums;
        if (albums.isEmpty()) error_ = QStringLiteral("推荐专辑暂不可用，可刷新重试");
        --pending_; emit changed();
    }));
}
void HomeController::loadAlbumMetadata() {
    if (!active_ || !gateway_.ready()) return;
    for (const auto& value : daily_) {
        if (metadataPending_ >= 2 || attemptedAlbums_.size() >= 12) break;
        const auto track = value.toMap(); const auto id = track.value("albumId").toString();
        if (!QRegularExpression("^[0-9]+$").match(id).hasMatch() || attemptedAlbums_.contains(id)
            || (!track.value("album").toString().isEmpty() && track.value("album").toString() != QStringLiteral("未知专辑"))) continue;
        attemptedAlbums_.insert(id); ++metadataPending_; const auto owner = generation_, metadataOwner = metadataGeneration_;
        metadataRequests_.append(gateway_.get("/album/detail", {{"id", id}}, this, [this, owner, metadataOwner, id](const QJsonObject& response) {
            if (owner != generation_ || metadataOwner != metadataGeneration_) return;
            --metadataPending_;
            const auto rows = response.value("data").toObject().value("info").toArray();
            const auto album = rows.isEmpty() ? QJsonObject{} : rows.first().toObject(); const auto name = album.value("AlbumName").toString();
            const auto rawId = album.contains("AlbumId") ? album.value("AlbumId") : album.value("album_id");
            const auto confirmedId = rawId.isDouble() ? QString::number(rawId.toInteger()) : rawId.toString();
            if (response.value("status").toInt() == 1 && confirmedId == id && !name.isEmpty() && name != QStringLiteral("未知专辑")) {
                for (auto& value : daily_) { auto row = value.toMap(); if (row.value("albumId").toString() == id) { row.insert("album", name); if (row.value("image").toString().isEmpty()) row.insert("image", album.value("sizable_cover").toString().replace("{size}", "400")); value = row; } }
                emit tracksReady(daily_);
            }
            loadAlbumMetadata();
        }));
    }
}
void HomeController::collections(quint64 owner, int sort) {
    requests_.append(gateway_.get("/top/playlist", {{"pagesize", 24}, {"sort", sort}}, this, [this, owner, sort](const QJsonObject& response) {
        if (owner != generation_) return;
        QVariantList rows;
        if (response.value("status").toInt() == 1) for (const auto& value : songList(response)) {
            const auto raw = value.toObject(); const auto numeric = raw.value("specialid");
            const auto id = numeric.isString() ? numeric.toString() : QString::number(numeric.toInteger());
            if (id.isEmpty() || id == "0") continue;
            auto image = raw.value("imgurl").toString(raw.value("cover").toString()); image.replace("{size}", "400");
            int count = -1;
            for (const auto* field : {"songcount", "song_count", "count", "list_song_count"}) {
                bool valid = false;
                const int value = raw.value(QLatin1String(field)).toVariant().toInt(&valid);
                if (valid && value >= 0) { count = value; break; }
            }
            rows.append(QVariantMap{{"id", id}, {"name", raw.value("specialname").toString(raw.value("name").toString())}, {"image", image}, {"count", count}});
        }
        if (rows.isEmpty()) error_ = QStringLiteral("部分推荐歌单暂不可用，可刷新重试");
        else if (sort == 2) playlists_ = rows; else albums_ = rows;
        --pending_; emit changed();
    }));
}
