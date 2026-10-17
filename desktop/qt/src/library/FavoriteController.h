#pragma once
#include "core/NativeGateway.h"
#include <QSettings>
#include <QVariantList>
#include <QSet>

class FavoriteController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QVariantList localTracks READ localTracks NOTIFY changed)
public:
    FavoriteController(NativeGateway& gateway, const QString& profile, QObject* parent = nullptr);
    ~FavoriteController() override;
    bool loading() const { return loading_; }
    int pendingCount() const { return outbox_.size(); }
    QString message() const { return message_; }
    QVariantList localTracks() const { return anonymous_; }
    void setAccount(const QString& userId);
    void setPlaylists(const QVariantList& playlists);
    Q_INVOKABLE bool isFavorite(const QVariantMap& track) const;
    Q_INVOKABLE void toggle(const QVariantMap& track);
    Q_INVOKABLE void retry();
    Q_INVOKABLE void syncLocal();
    Q_INVOKABLE void removeLocal(const QString& hash);
    Q_INVOKABLE void addToPlaylist(const QVariantMap& playlist, const QVariantMap& track);
    Q_INVOKABLE void removeFromPlaylist(const QVariantMap& playlist, const QVariantMap& track);
signals:
    void changed();
    void notice(const QString& message);
    void playlistChanged(const QString& id);
private:
    void loadPage(quint64 owner, int page);
    void flushNext();
    bool saveOutbox();
    void modifyPlaylist(const QVariantMap& playlist, const QVariantMap& track, bool add);
    QString key(const QString& name) const;
    NativeGateway& gateway_;
    QSettings settings_;
    QVariantList anonymous_, outbox_;
    QVariantMap liked_;
    QHash<QString, QString> fileIds_;
    QHash<QString, QVariantMap> ownedPlaylists_;
    QSet<QString> manualRequests_;
    QSet<QString> confirmed_, pages_;
    QString account_, request_, mutationId_, message_;
    quint64 generation_ = 0;
    quint64 loadGeneration_ = 0;
    int total_ = 0;
    bool loading_ = false, complete_ = false, mutating_ = false, reloadRequested_ = false;
};
