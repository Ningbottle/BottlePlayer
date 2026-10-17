#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>
#include <QVector>
struct Track {
    QString hash, title, artist, album, albumId, audioId, image, fileId;
    int duration = 0;
    static Track fromJson(const QJsonObject& object);
    QVariantMap toVariant() const;
};
QJsonArray songList(const QJsonObject& response);
QVector<Track> parseTracks(const QJsonObject& response);
