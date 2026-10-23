#pragma once
#include "Track.h"
#include "core/NativeGateway.h"
#include "lyrics/LyricTimeline.h"
#include "playback/PlaySession.h"
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QSettings>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>
#include <optional>

class MusicController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList feed READ feed NOTIFY feedChanged)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchChanged)
    Q_PROPERTY(QVariantList queue READ queue NOTIFY queueChanged)
    Q_PROPERTY(QVariantMap hero READ hero NOTIFY heroChanged)
    Q_PROPERTY(QVariantMap current READ current NOTIFY playbackChanged)
    Q_PROPERTY(int queueIndex READ queueIndex NOTIFY queueIndexChanged)
    Q_PROPERTY(int searchTotal READ searchTotal NOTIFY searchChanged)
    Q_PROPERTY(QString searchTerm READ searchTerm NOTIFY searchChanged)
    Q_PROPERTY(QString searchError READ searchError NOTIFY searchChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY statusChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY playbackChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(bool radio READ radio NOTIFY radioChanged)
    Q_PROPERTY(bool radioPending READ radioPending NOTIFY radioPendingChanged)
    Q_PROPERTY(bool preview READ preview NOTIFY playbackChanged)
    Q_PROPERTY(QString quality READ quality WRITE setQuality NOTIFY playbackChanged)
    Q_PROPERTY(QString actualQuality READ actualQuality NOTIFY playbackChanged)
    Q_PROPERTY(QVariantList availableQualities READ availableQualities NOTIFY playbackChanged)
    // Full quality vocabulary; the picker falls back to it when a track's own
    // availability is unknown, so the menu can never collapse to one entry.
    Q_PROPERTY(QVariantList qualityCatalog READ qualityCatalog CONSTANT)
    // playerStore.loopMode: "list" | "single" | "random".
    Q_PROPERTY(QString loopMode READ loopMode NOTIFY loopModeChanged)
    Q_PROPERTY(bool favorite READ favorite NOTIFY favoriteChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY appearanceChanged)
    Q_PROPERTY(int lyricSize READ lyricSize WRITE setLyricSize NOTIFY appearanceChanged)
    Q_PROPERTY(bool vinyl READ vinyl WRITE setVinyl NOTIFY appearanceChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY appearanceChanged)
    Q_PROPERTY(QVariantList lyrics READ lyrics NOTIFY lyricsChanged)
    Q_PROPERTY(int lyricIndex READ lyricIndex NOTIFY lyricIndexChanged)
    Q_PROPERTY(QString cue READ cue NOTIFY lyricIndexChanged)
    Q_PROPERTY(QString previousCue READ previousCue NOTIFY lyricIndexChanged)
    Q_PROPERTY(QString message READ message NOTIFY statusChanged)
public:
    MusicController(QString nativeDll, QString profile, QObject* parent = nullptr);
    void initialize();
    QVariantList feed() const;
    QVariantList searchResults() const;
    QVariantList queue() const;
    QVariantMap hero() const;
    QVariantMap current() const;
    int queueIndex() const { return queueIndex_; }
    int searchTotal() const { return searchTotal_; }
    QString searchTerm() const { return searchTerm_; }
    QString searchError() const { return searchError_; }
    bool searching() const { return searching_; }
    bool ready() const { return ready_; }
    bool loading() const { return loading_; }
    bool playing() const { return player_.playbackState() == QMediaPlayer::PlayingState; }
    bool radio() const { return radio_; }
    bool radioPending() const { return radioPending_; }
    bool preview() const { return preview_; }
    QString loopMode() const { return loopMode_; }
    bool favorite() const;
    qint64 position() const { return resumePending_ ? resumePosition_ : player_.position(); }
    qint64 duration() const;
    QString quality() const { return quality_; }
    QString actualQuality() const { return actualQuality_; }
    QVariantList availableQualities() const { return availableQualities_; }
    QVariantList qualityCatalog() const;
    double volume() const { return output_.volume(); }
    int lyricSize() const { return lyricSize_; }
    bool vinyl() const { return vinyl_; }
    bool reducedMotion() const { return reducedMotion_; }
    QVariantList lyrics() const;
    int lyricIndex() const { return lyricIndex_; }
    QString cue() const;
    QString previousCue() const;
    QString message() const { return message_; }
    QString fingerprint() const { return gateway_.fingerprint(); }
    QMediaPlayer* mediaPlayer() { return &player_; }
    QAudioOutput* audioOutput() { return &output_; }
    NativeGateway& nativeGateway() { return gateway_; }
    Q_INVOKABLE void playTracks(const QVariantList& tracks, int index);
    Q_INVOKABLE bool enqueueTrack(const QVariantMap& track);
    Q_INVOKABLE void refreshFeed();
    Q_INVOKABLE void search(const QString& term, bool more = false);
    Q_INVOKABLE void playFrom(const QString& source, int index);
    Q_INVOKABLE void playQueue(int index);
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void setPlaying(bool playing);
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 position);
    Q_INVOKABLE void startRadio();
    Q_INVOKABLE void stopRadio();
    void setAccount(const QString& account);
    // Offline fixture injection for tests and smoke runs; no UI entry opens local files.
    Q_INVOKABLE void openLocal(const QUrl& file);
    Q_INVOKABLE void cycleLoopMode();
    Q_INVOKABLE void toggleFavorite();
    Q_INVOKABLE QString timeText(qint64 millis) const;
    Q_INVOKABLE QString qualityText(const QString& quality) const;
    Q_INVOKABLE void finishSession();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void removeQueue(int index);
    Q_INVOKABLE void moveQueue(int from, int to);
    Q_INVOKABLE void clearQueue();
    void setQuality(const QString& quality);
    void setFavoritePolicy(std::function<bool(const QVariantMap&)> read, std::function<void(const QVariantMap&)> toggle);
    void holdPersistenceForImport() { saveQueue(); shuttingDown_ = true; persistTimer_.stop(); player_.pause(); }
    void setFeed(const QVariantList& tracks);
    void setFeedRefresh(std::function<void()> refresh) { feedRefresh_ = std::move(refresh); }
    void setVolume(double value);
    void setLyricSize(int value);
    void setVinyl(bool value);
    void setReducedMotion(bool value);
signals:
    void feedChanged(); void searchChanged(); void queueChanged(); void heroChanged();
    void queueIndexChanged(); void radioChanged(); void radioPendingChanged();
    void playbackChanged(); void positionChanged(); void durationChanged();
    void appearanceChanged(); void lyricsChanged(); void lyricIndexChanged(); void statusChanged();
    void loopModeChanged(); void favoriteChanged();
    // Transient player-bar toast (usePlayerControls showToast: 2s, not sticky).
    void notice(const QString& text);
    void listenRecorded(const QJsonObject& record);
    void trackStarted(const QVariantMap& track);
private:
    void resolveCurrent(qint64 position = 0, bool autoplay = true, bool preserveSession = false, bool coldResume = false);
    void applyResume();
    bool recoverQualitySwitch(const QString& reason);
    void saveQueue();
    void step(int delta);
    void loadLyrics(const Track& track, quint64 generation);
    void loadQualities(const Track& track, quint64 generation);
    void radioNext();
    void fetchRadio(bool starting, bool advance = false);
    void cancelRadioRequest();
    void setMessage(QString text);
    void updateLyricIndex();
    void finishListeningInterval();
    void finalizeSession(bool completed);
    void tracePlayback(const QString& event) const;
    void updateQueueProjection(int previousIndex, bool previousRadio);
    void updateLyricProjection();
    void setQueueIndex(int index);
    void setRadio(bool enabled);
    void setRadioPending(bool pending);
    NativeGateway gateway_;
    QSettings settings_;
    QAudioOutput output_;
    QMediaPlayer player_;
    QVector<Track> feed_, search_, queue_;
    QVector<LyricCue> cues_;
    // Read-only projections of the vectors above; business decisions never use these caches.
    QVariantList feedProjection_, searchProjection_, queueProjection_, lyricProjection_;
    int queueIndex_ = -1, searchTotal_ = 0, searchPage_ = 0, lyricIndex_ = -1, lyricSize_ = 32;
    bool ready_ = false, loading_ = false, searching_ = false, radio_ = false, preview_ = false;
    bool vinyl_ = false, reducedMotion_ = false;
    QString searchTerm_, searchError_, message_, resolveId_, searchId_, lyricId_, feedId_, radioId_, privilegeId_;
    QString loopMode_ = QStringLiteral("list");
    QStringList favorites_;
    QStringList backupUrls_;
    std::function<bool(const QVariantMap&)> favoriteRead_;
    std::function<void(const QVariantMap&)> favoriteToggle_;
    std::function<void()> feedRefresh_;
    quint64 generation_ = 0, searchGeneration_ = 0, feedGeneration_ = 0;
    quint64 radioGeneration_ = 0;
    QString account_;
    bool radioPending_ = false, radioAdvancePending_ = false;
    PlaySession session_;
    QTimer persistTimer_, resumeDeadline_, loadDeadline_;
    QString quality_ = "128", committedQuality_ = "128", actualQuality_ = "unknown";
    // A quality change is committed only after the replacement media loads.
    // Keep the last usable source until that point so a failed decode can recover.
    struct QualitySource { QUrl url; QStringList backups; qint64 position; };
    std::optional<QualitySource> qualitySource_;
    QUrl pendingSource_;
    QString pendingActualQuality_ = "unknown", qualityRecoveryReason_;
    bool pendingPreview_ = false, pendingLoadObserved_ = false, recoveringQuality_ = false;
    QVariantList availableQualities_;
    qint64 resumePosition_ = 0;
    bool resumePending_ = false, coldResume_ = false, desiredPlay_ = true, shuttingDown_ = false;
    bool sourceNeedsReload_ = false, qualitySwitchPending_ = false;
    qint64 listenedMs_ = 0;
};
