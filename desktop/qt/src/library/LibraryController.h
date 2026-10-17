#pragma once
#include "core/NativeGateway.h"
#include "music/Track.h"
#include <QSet>

class LibraryController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY changed)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY changed)
    Q_PROPERTY(QString accountId READ accountId NOTIFY changed)
    Q_PROPERTY(QString playlistId READ playlistId NOTIFY changed)
    Q_PROPERTY(QString playlistName READ playlistName NOTIFY changed)
    Q_PROPERTY(QString playlistImage READ playlistImage NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString playlistsError READ playlistsError NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool playlistsLoading READ playlistsLoading NOTIFY changed)
    Q_PROPERTY(bool playlistsMore READ playlistsMore NOTIFY changed)
    Q_PROPERTY(bool complete READ complete NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(bool publicPlaylist READ publicPlaylist NOTIFY changed)
    Q_PROPERTY(bool album READ album NOTIFY changed)
public:
    explicit LibraryController(NativeGateway& gateway, QObject* parent = nullptr);
    ~LibraryController() override;
    QVariantList playlists() const { return playlists_; }
    QVariantList tracks() const;
    QString accountId() const { return account_; }
    QString playlistId() const { return id_; }
    QString playlistName() const { return name_; }
    QString playlistImage() const { return image_; }
    QString error() const { return error_; }
    QString playlistsError() const { return listsError_; }
    bool loading() const { return loading_; }
    bool playlistsLoading() const { return listsLoading_; }
    bool playlistsMore() const { return listsMore_; }
    bool complete() const { return complete_; }
    int total() const { return total_; }
    bool publicPlaylist() const { return public_; }
    bool album() const { return album_; }
    void setAccount(const QString& userId);
    Q_INVOKABLE void refreshPlaylists();
    Q_INVOKABLE void morePlaylists();
    Q_INVOKABLE void openPlaylist(const QString& id);
    Q_INVOKABLE void openPublicPlaylist(const QVariantMap& playlist);
    Q_INVOKABLE void openAlbum(const QVariantMap& album);
    Q_INVOKABLE void retryTracks();
    Q_INVOKABLE void play(int index = 0);
    Q_INVOKABLE void cancelTracks();
signals:
    void changed();
    void playRequested(const QVariantList& tracks, int index);
private:
    void loadListsPage(quint64 epoch, int page);
    void loadTracksPage(quint64 epoch, int page);
    NativeGateway& gateway_;
    QVariantList playlists_;
    QVector<Track> tracks_;
    QSet<QString> listIds_, trackIds_, pageSignatures_;
    QString account_, id_, name_, image_, error_, listsError_, listRequest_, trackRequest_;
    quint64 listEpoch_ = 0, trackEpoch_ = 0;
    int listPage_ = 0, total_ = 0;
    int pendingPlay_ = -1;
    bool loading_ = false, listsLoading_ = false, listsMore_ = false, complete_ = false;
    bool public_ = false, album_ = false;
};
