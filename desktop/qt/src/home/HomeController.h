#pragma once
#include "core/NativeGateway.h"
#include <QVariantList>
#include <QTimer>
#include <QSet>

class HomeController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY changed)
    Q_PROPERTY(QVariantList albums READ albums NOTIFY changed)
    Q_PROPERTY(QVariantList recommendedAlbums READ recommendedAlbums NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit HomeController(NativeGateway& gateway, QObject* parent = nullptr);
    QVariantList playlists() const { return playlists_; }
    QVariantList albums() const { return albums_; }
    QVariantList recommendedAlbums() const { return recommendations_; }
    bool loading() const { return pending_ > 0; }
    QString error() const { return error_; }
    void setAccount(const QString& userId);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setActive(bool active);
signals:
    void changed();
    void tracksReady(const QVariantList& tracks);
private:
    void daily(quint64 owner, bool fallback);
    void collections(quint64 owner, int sort);
    void loadRecommendedAlbums(quint64 owner);
    void loadAlbumMetadata();
    NativeGateway& gateway_;
    QVariantList playlists_, albums_, daily_, recommendations_;
    QString account_, day_, error_;
    QStringList requests_;
    quint64 generation_ = 0;
    int pending_ = 0;
    QTimer dayTimer_;
    bool active_ = true;
    QStringList metadataRequests_;
    QSet<QString> attemptedAlbums_;
    quint64 metadataGeneration_ = 0;
    int metadataPending_ = 0;
};
