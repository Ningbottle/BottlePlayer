#include "Track.h"
#include <QRegularExpression>
#include <QStringList>
namespace {
QString alias(const QJsonObject& object, std::initializer_list<const char*> names) {
    for (const auto* name : names) {
        const auto value = object.value(QLatin1String(name));
        const auto result = value.isDouble() ? QString::number(value.toInteger()) : value.toString().trimmed();
        if (!result.isEmpty()) return result;
    }
    return {};
}
}
Track Track::fromJson(const QJsonObject& o) {
    Track t;
    t.hash = alias(o, {"FileHash", "hash", "Filehash"});
    t.title = alias(o, {"SongName", "songname", "song_name", "songName", "filename", "FileName", "file_name", "fileName", "title", "name"});
    t.artist = alias(o, {"SingerName", "singername", "singer_name", "singerName", "author_name", "artist", "singer"});
    if (t.artist.isEmpty()) {
        QStringList names;
        for (const auto author : o.value("authors").toArray()) {
            const auto name = author.toObject().value("author_name").toString();
            if (!name.isEmpty() && !names.contains(name)) names.append(name);
        }
        t.artist = names.join(QStringLiteral("、"));
    }
    if (t.title.contains(" - ")) {
        if (t.artist.isEmpty()) t.artist = t.title.section(" - ", 0, 0);
        t.title = t.title.section(" - ", 1);
    }
    t.title.remove(QRegularExpression("<[^>]+>"));
    t.artist.remove(QRegularExpression("<[^>]+>"));
    if (t.title.isEmpty()) t.title = QStringLiteral("未知歌曲");
    if (t.artist.isEmpty()) t.artist = QStringLiteral("未知歌手");
    t.album = alias(o, {"AlbumName", "album_name", "albumname", "album"});
    t.albumId = alias(o, {"AlbumID", "album_id", "albumid", "albumId"});
    t.audioId = alias(o, {"AlbumAudioID", "album_audio_id", "mixsongid", "MixSongID", "audioId"});
    t.fileId = alias(o, {"fileid", "FileID", "fileId", "audio_id"});
    const auto nestedAlbum = o.value("album_info").isObject() ? o.value("album_info").toObject() : o.value("albuminfo").toObject();
    if (t.album.isEmpty()) t.album = alias(nestedAlbum, {"album_name", "name"});
    if (t.albumId.isEmpty()) t.albumId = alias(nestedAlbum, {"album_id", "id"});
    t.duration = alias(o, {"Duration", "duration", "time_length", "timeLength"}).toInt();
    if (!t.duration) t.duration = o.value("timelen").toInt() / 1000;
    if (t.duration > 18000) t.duration /= 1000;
    t.image = alias(o, {"Image", "image", "imgurl", "img", "cover", "cover_url", "pic_url", "pic", "sizable_cover", "album_sizable_cover", "album_cover"});
    if (t.image.isEmpty()) t.image = o.value("trans_param").toObject().value("union_cover").toString();
    if (t.image.isEmpty()) t.image = o.value("album_info").toObject().value("sizable_cover").toString();
    t.image.replace("{size}", "400");
    return t;
}
QVariantMap Track::toVariant() const {
    return {{"hash", hash}, {"title", title}, {"artist", artist}, {"album", album}, {"image", image}, {"duration", duration},
            {"albumId", albumId}, {"audioId", audioId}, {"fileId", fileId}};
}
QJsonArray songList(const QJsonObject& response) {
    auto object = response;
    for (int depth = 0; depth < 4; ++depth) {
        for (const auto* key : {"info", "lists", "song_list", "songlist", "list", "songs"})
            if (object.value(QLatin1String(key)).isArray()) return object.value(QLatin1String(key)).toArray();
        if (object.value("data").isArray()) return object.value("data").toArray();
        if (!object.value("data").isObject()) break;
        object = object.value("data").toObject();
    }
    return {};
}
QVector<Track> parseTracks(const QJsonObject& response) {
    QVector<Track> tracks;
    if (response.value("status").toInt() != 1) return tracks;
    for (const auto raw : songList(response)) {
        auto track = Track::fromJson(raw.toObject());
        if (!track.hash.isEmpty()) tracks.append(std::move(track));
    }
    return tracks;
}
