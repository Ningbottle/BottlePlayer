#include "MusicController.h"
#include "core/Diagnostics.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QRandomGenerator>
#include <QJsonDocument>
#include <QJsonArray>
#include <cmath>
#include <algorithm>
#include <QSet>
#include <QDateTime>
#include <QCoreApplication>
#include <utility>

namespace {
QVariantList variants(const QVector<Track>& tracks) {
    QVariantList values; values.reserve(tracks.size());
    for (const auto& track : tracks) values.append(track.toVariant());
    return values;
}
// KuGou's own quality tokens, in ascending order. They are the wire vocabulary
// of /song/url and /privilege/lite, so the picker, the validator, the stored
// preference and the labels must all read this one list — a fourth spelling
// (the old "hires") made selections silently no-op upstream.
const QStringList kQualityCatalog{QStringLiteral("128"), QStringLiteral("320"),
                                  QStringLiteral("flac"), QStringLiteral("high"),
                                  QStringLiteral("master")};
QVariantList qualityOptions(const QStringList& present) {
    QVariantList options;
    for (const auto& key : kQualityCatalog) if (present.contains(key)) options.append(QVariantMap{{QStringLiteral("quality"), key}});
    return options;
}
}
MusicController::MusicController(QString dll, QString profile, QObject* parent)
    : QObject(parent), gateway_(std::move(dll), QDir(profile).filePath("native")),
      settings_(QDir(profile).filePath("preferences.ini"), QSettings::IniFormat) {
    QDir().mkpath(QDir(profile).filePath("native"));
    output_.setVolume(static_cast<float>(std::clamp(settings_.value("volume", 0.5).toDouble(), 0.0, 1.0)));
    lyricSize_ = std::clamp(settings_.value("lyricSize", 32).toInt(), 20, 48);
    vinyl_ = settings_.value("vinyl", false).toBool();
    reducedMotion_ = settings_.value("reducedMotion", false).toBool();
    const auto loop = settings_.value("loopMode", loopMode_).toString();
    if (loop == QLatin1String("single") || loop == QLatin1String("random")) loopMode_ = loop;
    favorites_ = settings_.value("favorites").toStringList();
    const auto storedQuality = settings_.value("quality", "128").toString();
    const auto quality = storedQuality == "hires" ? QStringLiteral("high") : storedQuality;
    if (kQualityCatalog.contains(quality)) quality_ = quality;
    committedQuality_ = quality_;
    const auto snapshot = QJsonDocument::fromJson(settings_.value("queueSnapshot").toByteArray()).object();
    if (snapshot.value("version").toInt() == 1) {
        const auto rawIndex = snapshot.value("index");
        const int selected = rawIndex.isDouble() && std::floor(rawIndex.toDouble()) == rawIndex.toDouble() ? rawIndex.toInt(-1) : -1;
        const auto rows = snapshot.value("queue").toArray();
        for (int i = 0; i < rows.size() && queue_.size() < 2000; ++i) {
            auto track = Track::fromJson(rows[i].toObject()); if (track.hash.isEmpty()) continue;
            if (i == selected) queueIndex_ = int(queue_.size());
            queue_.append(std::move(track));
        }
        radio_ = snapshot.value("radio").toBool();
        const auto savedPosition = snapshot.value("position").toDouble();
        if (queueIndex_ >= 0 && std::isfinite(savedPosition) && savedPosition > 0 && savedPosition < 86400000) {
            resumePosition_ = qint64(savedPosition); resumePending_ = true; coldResume_ = true;
            const auto known = qint64(queue_[queueIndex_].duration) * 1000;
            if (known > 0 && resumePosition_ >= known - 1000) resumePosition_ = 0;
        }
    }
    queueProjection_ = variants(queue_);
    persistTimer_.setInterval(5000);
    connect(&persistTimer_, &QTimer::timeout, this, &MusicController::saveQueue);
    resumeDeadline_.setSingleShot(true); resumeDeadline_.setInterval(5000);
    connect(&resumeDeadline_, &QTimer::timeout, this, [this] {
        if (!resumePending_) return;
        resumePosition_ = 0; applyResume(); // Undecoded/live duration cannot validate a saved offset.
    });
    loadDeadline_.setSingleShot(true); loadDeadline_.setInterval(20000);
    connect(&loadDeadline_, &QTimer::timeout, this, [this] {
        if (!loading_) return;
        tracePlayback("load_timeout"); ++generation_; gateway_.cancel(resolveId_);
        sourceNeedsReload_ = true; loading_ = resumePending_ = false; resumeDeadline_.stop(); player_.stop();
        if (qualitySwitchPending_) { quality_ = committedQuality_; qualitySwitchPending_ = false; }
        setMessage(QStringLiteral("音频加载超时，点击播放可重新获取音源")); emit playbackChanged();
    });
    // Content replacement and selection changes can both change the heart.
    connect(this, &MusicController::queueChanged, this, &MusicController::favoriteChanged);
    connect(this, &MusicController::queueIndexChanged, this, &MusicController::favoriteChanged);
    player_.setAudioOutput(&output_);
    connect(&player_, &QMediaPlayer::positionChanged, this, [this] {
        session_.position(player_.position(), player_.duration()); listenedMs_ = session_.listenedMilliseconds();
        emit positionChanged(); updateLyricIndex();
    });
    connect(&player_, &QMediaPlayer::durationChanged, this, [this] {
        if (player_.duration() > 0 && queueIndex_ >= 0 && queueIndex_ < queue_.size()) {
            const auto duration = int(player_.duration() / 1000);
            if (queue_[queueIndex_].duration != duration) {
                queue_[queueIndex_].duration = duration;
                updateQueueProjection(queueIndex_, radio_);
            }
        }
        applyResume(); emit durationChanged();
    });
    connect(&player_, &QMediaPlayer::playbackStateChanged, this, [this] {
        tracePlayback("playback_state");
        const bool firstStart = playing() && !session_.started();
        session_.setPlaying(playing(), player_.position());
        if (firstStart && queueIndex_ >= 0) emit trackStarted(current());
        if (playing()) persistTimer_.start(); else { persistTimer_.stop(); if (!loading_) saveQueue(); }
        emit playbackChanged();
    });
    connect(&player_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        tracePlayback("media_status");
        if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
            applyResume();
            if (!resumePending_) loading_ = false;
            emit playbackChanged();
        }
        if (status != QMediaPlayer::EndOfMedia) return;
        if (queueIndex_ < 0 || queueIndex_ >= queue_.size()) return;
        finalizeSession(true);
        // Single-loop replays the ended track; the radio always advances.
        if (loopMode_ == QLatin1String("single") && !radio_) { session_.intend(queue_[queueIndex_]); player_.setPosition(0); player_.play(); }
        else next();
    });
    connect(&player_, &QMediaPlayer::errorOccurred, this, [this] {
        tracePlayback("media_error");
        if (shuttingDown_ || queueIndex_ < 0 || queueIndex_ >= queue_.size() || player_.source().isEmpty()) return;
        if (!backupUrls_.isEmpty()) {
            const auto retryUrl = QUrl(backupUrls_.takeFirst());
            const auto owner = generation_; const auto failed = player_.source();
            const auto offset = position();
            QTimer::singleShot(0, this, [this, owner, failed, retryUrl, offset] {
                if (owner != generation_ || player_.source() != failed || shuttingDown_) return;
                resumePosition_ = offset; resumePending_ = true; loading_ = true; session_.seek();
                player_.setSource(retryUrl); applyResume(); if (resumePending_) resumeDeadline_.start();
                emit playbackChanged();
            });
            return;
        }
        resumePending_ = false; resumeDeadline_.stop(); loadDeadline_.stop();
        sourceNeedsReload_ = true;
        if (qualitySwitchPending_) { quality_ = committedQuality_; qualitySwitchPending_ = false; }
        loading_ = false; emit playbackChanged();
        setMessage(QStringLiteral("音频加载失败，请重试或选择其他歌曲"));
    });
}
void MusicController::initialize() {
    gateway_.initialize([this](const QJsonObject& response) {
        ready_ = response.value("status").toInt() == 1;
        tracePlayback("initialized");
        setMessage(ready_ ? QString{} : response.value("error").toString());
        emit statusChanged();
        if (ready_) refreshFeed();
    });
}
QVariantList MusicController::feed() const { return feedProjection_; }
QVariantList MusicController::searchResults() const { return searchProjection_; }
QVariantList MusicController::queue() const { return queueProjection_; }
QVariantMap MusicController::hero() const {
    if (queueIndex_ >= 0 && queueIndex_ < queueProjection_.size()) return queueProjection_.at(queueIndex_).toMap();
    return feedProjection_.isEmpty() ? QVariantMap{} : feedProjection_.first().toMap();
}
QVariantMap MusicController::current() const {
    return queueIndex_ >= 0 && queueIndex_ < queueProjection_.size() ? queueProjection_.at(queueIndex_).toMap() : QVariantMap{};
}
QVariantList MusicController::lyrics() const { return lyricProjection_; }
void MusicController::updateLyricProjection() {
    QVariantList result;
    result.reserve(cues_.size());
    for (const auto& cue : cues_) {
        QVariantList words;
        words.reserve(cue.words.size());
        for (const auto& word : cue.words) words.append(QVariantMap{{"time", word.time}, {"end", word.end}, {"start", word.start}, {"length", word.length}});
        result.append(QVariantMap{{"time", cue.time}, {"text", cue.text}, {"translation", cue.translation}, {"words", words}});
    }
    lyricProjection_ = std::move(result);
    emit lyricsChanged();
}
void MusicController::updateQueueProjection(int previousIndex, bool previousRadio) {
    auto projection = variants(queue_);
    if (projection != queueProjection_) {
        queueProjection_ = std::move(projection);
        emit queueChanged();
    }
    if (previousIndex != queueIndex_) emit queueIndexChanged();
    if (previousRadio != radio_) emit radioChanged();
}
void MusicController::setQueueIndex(int index) {
    if (index == queueIndex_) return;
    queueIndex_ = index;
    emit queueIndexChanged();
}
void MusicController::setRadio(bool enabled) {
    if (enabled == radio_) return;
    radio_ = enabled;
    emit radioChanged();
}
void MusicController::setRadioPending(bool pending) {
    if (pending == radioPending_) return;
    radioPending_ = pending;
    emit radioPendingChanged();
}
QString MusicController::cue() const {
    return lyricIndex_ >= 0 && lyricIndex_ < cues_.size() ? cues_[lyricIndex_].text : QString{};
}
QString MusicController::previousCue() const {
    return lyricIndex_ > 0 && lyricIndex_ <= cues_.size() ? cues_[lyricIndex_ - 1].text : QString{};
}
void MusicController::refreshFeed() {
    if (!ready_) return;
    if (feedRefresh_) { feedRefresh_(); return; }
    const auto generation = ++feedGeneration_;
    gateway_.cancel(feedId_);
    feedId_ = gateway_.get("/top/song", {{"pagesize", 30}}, this, [this, generation](const QJsonObject& response) {
        if (generation != feedGeneration_) return;
        const auto tracks = parseTracks(response);
        if (tracks.isEmpty()) { setMessage(QStringLiteral("推荐暂时无法加载，可重试或打开本地音乐")); return; }
        feed_ = tracks; feedProjection_ = variants(feed_); emit feedChanged(); emit heroChanged(); setMessage({});
    });
}
void MusicController::setFeed(const QVariantList& rows) {
    QVector<Track> tracks;
    for (const auto& value : rows) { auto track = Track::fromJson(QJsonObject::fromVariantMap(value.toMap())); if (!track.hash.isEmpty()) tracks.append(std::move(track)); }
    feed_ = std::move(tracks); feedProjection_ = variants(feed_); emit feedChanged(); emit heroChanged();
}
void MusicController::search(const QString& term, bool more) {
    if (!ready_ || (more && searching_)) return;
    const auto cleaned = term.trimmed();
    if (cleaned.isEmpty()) return;
    if (more && (cleaned != searchTerm_ || search_.size() >= searchTotal_)) return;
    gateway_.cancel(searchId_);
    const auto generation = ++searchGeneration_;
    if (!more) { search_.clear(); searchProjection_.clear(); searchTotal_ = 0; searchPage_ = 0; searchTerm_ = cleaned; }
    const auto page = searchPage_ + 1;
    searchError_.clear(); searching_ = true; emit searchChanged();
    searchId_ = gateway_.get("/search", {{"keywords", cleaned}, {"pagesize", 30}, {"page", page}}, this,
        [this, generation, page](const QJsonObject& response) {
        if (generation != searchGeneration_) return;
        searching_ = false;
        if (response.value("status").toInt() != 1) {
            searchError_ = QStringLiteral("搜索失败，请重试"); emit searchChanged(); return;
        }
        const auto tracks = parseTracks(response);
        QSet<QString> seen; for (const auto& track : search_) seen.insert(track.hash.toLower() + "|" + track.audioId);
        int added = 0;
        for (const auto& track : tracks) { const auto identity = track.hash.toLower() + "|" + track.audioId; if (seen.contains(identity)) continue; seen.insert(identity); search_.append(track); ++added; }
        if (added) searchProjection_ = variants(search_);
        searchPage_ = page;
        searchTotal_ = std::max(static_cast<int>(search_.size()), response.value("data").toObject().value("total").toInt());
        if (tracks.isEmpty() || (page > 1 && added == 0)) searchTotal_ = static_cast<int>(search_.size());
        emit searchChanged();
    });
}
void MusicController::playFrom(const QString& source, int index) {
    const auto& tracks = source == "search" ? search_ : feed_;
    if (index < 0 || index >= tracks.size()) return;
    cancelRadioRequest(); ++generation_; const bool previousRadio = radio_; radio_ = false;
    const int previousIndex = queueIndex_;
    queue_ = tracks; queueIndex_ = index; updateQueueProjection(previousIndex, previousRadio); resolveCurrent();
}
void MusicController::playQueue(int index) {
    if (index < 0 || index >= queue_.size()) return;
    if (!radio_) cancelRadioRequest();
    if (index == queueIndex_) { toggle(); return; }
    setQueueIndex(index); resolveCurrent();
}
void MusicController::resolveCurrent(qint64 position, bool autoplay, bool preserveSession, bool coldResume) {
    if (queueIndex_ < 0 || queueIndex_ >= queue_.size()) return;
    if (!preserveSession) finalizeSession(false);
    const auto generation = ++generation_;
    gateway_.cancel(resolveId_); gateway_.cancel(lyricId_); gateway_.cancel(privilegeId_);
    resolveId_.clear(); lyricId_.clear(); privilegeId_.clear();
    loading_ = true; desiredPlay_ = autoplay; coldResume_ = coldResume;
    sourceNeedsReload_ = false; qualitySwitchPending_ = preserveSession;
    loadDeadline_.start(); tracePlayback("resolve_start");
    if (!preserveSession) {
        backupUrls_.clear();
        // A new track must not inherit the previous track's quality options.
        availableQualities_.clear();
        player_.stop(); player_.setSource({}); listenedMs_ = 0; cues_.clear(); lyricIndex_ = -1; preview_ = false;
        updateLyricProjection(); emit lyricIndexChanged();
    }
    resumePending_ = !preserveSession; resumePosition_ = std::max<qint64>(0, position);
    resumeDeadline_.stop(); emit playbackChanged(); emit heroChanged(); setMessage({}); saveQueue();
    const auto track = queue_[queueIndex_];
    if (!preserveSession) session_.intend(track);
    const QUrl local(track.hash);
    if (local.isLocalFile()) {
        if (!QFileInfo(local.toLocalFile()).isFile()) { loading_ = false; resumePending_ = false; setMessage(QStringLiteral("本地音频文件已不存在")); emit playbackChanged(); return; }
        QFile lyricFile(QFileInfo(local.toLocalFile()).dir().filePath(QFileInfo(local.toLocalFile()).completeBaseName() + ".lrc"));
        if (lyricFile.open(QIODevice::ReadOnly)) cues_ = LyricTimeline::parseBilingual(QString::fromUtf8(lyricFile.readAll()));
        actualQuality_ = "local"; updateLyricProjection(); emit lyricIndexChanged();
        player_.setSource(local); applyResume(); if (resumePending_) resumeDeadline_.start(); saveQueue(); return;
    }
    // A quick quality selection can cancel a still-pending availability
    // request. Restart that request until this track has a resolved list.
    if (!preserveSession || availableQualities_.isEmpty()) loadQualities(track, generation);
    resolveId_ = gateway_.get("/song/url", {{"hash", track.hash}, {"album_id", track.albumId},
        {"album_audio_id", track.audioId}, {"quality", quality_}}, this, [this, generation, preserveSession](const QJsonObject& response) {
        if (generation != generation_) return;
        QUrl url(response.value("url").toString());
        const auto data = response.value("data").toObject();
        QJsonObject delivered;
        for (const auto& value : data.value("available_qualities").toArray()) {
            const auto option = value.toObject(); const QUrl candidate(option.value("url").toString());
            if (option.value("quality").toString() == quality_ && candidate.isValid() && (candidate.scheme() == "https" || candidate.scheme() == "http")) { url = candidate; delivered = option; break; }
        }
        if (response.value("status").toInt() != 1 || !url.isValid() || (url.scheme() != "http" && url.scheme() != "https")) {
            loading_ = false; resumePending_ = false;
            if (preserveSession) quality_ = committedQuality_;
            emit playbackChanged();
            setMessage(response.value("vip_required").toBool() ? QStringLiteral("这首歌需要对应的账号权限，请检查账户登录状态")
                : QStringLiteral("这首歌暂时无法播放，请选择其他歌曲")); return;
        }
        preview_ = response.value("is_preview").toBool() || response.value("delivery").toString() == "preview";
        if (!delivered.isEmpty()) preview_ = delivered.value("isPreview").toBool(delivered.value("is_preview").toBool()) || delivered.value("delivery").toString() == "preview";
        actualQuality_ = delivered.isEmpty() ? response.value("quality").toString(data.value("quality").toString("unknown")) : delivered.value("quality").toString("unknown");
        backupUrls_.clear();
        if (url == QUrl(response.value("url").toString())) for (const auto& value : data.value("backup_url").toArray()) {
            const QUrl candidate(value.toString());
            if (candidate.isValid() && (candidate.scheme() == "http" || candidate.scheme() == "https") && candidate != url && !backupUrls_.contains(candidate.toString())) backupUrls_.append(candidate.toString());
            if (backupUrls_.size() >= 2) break;
        }
        auto metadata = Track::fromJson(data);
        auto& current = queue_[queueIndex_];
        bool metadataChanged = false;
        if (current.image.isEmpty() && !metadata.image.isEmpty()) { current.image = metadata.image; metadataChanged = true; }
        if (current.album.isEmpty() && !metadata.album.isEmpty()) { current.album = metadata.album; metadataChanged = true; }
        if (metadataChanged) { updateQueueProjection(queueIndex_, radio_); emit heroChanged(); }
        if (preserveSession) { resumePosition_ = player_.position(); player_.pause(); session_.seek(); }
        resumePending_ = true;
        player_.setSource(url); applyResume(); if (resumePending_) resumeDeadline_.start(); emit playbackChanged();
    });
    tracePlayback("source_request"); loadLyrics(track, generation);
}
// The picker lists what the song actually has, never what one /song/url answer
// happened to return: production resolves end on v5, which reports a single
// quality and used to collapse the menu to that one entry.
void MusicController::loadQualities(const Track& track, quint64 generation) {
    privilegeId_ = gateway_.get("/privilege/lite", {{"hash", track.hash}, {"album_id", track.albumId}}, this,
        [this, generation](const QJsonObject& response) {
        if (generation != generation_) return;
        QStringList present;
        for (const auto& value : response.value("data").toArray().first().toObject().value("relate_goods").toArray())
            present.append(value.toObject().value("quality").toString());
        auto options = qualityOptions(present);
        // An answer that names nothing must not leave the menu empty; the full
        // catalog stays selectable and actualQuality reports what was delivered.
        if (options.isEmpty()) options = qualityOptions(kQualityCatalog);
        if (options == availableQualities_) return;
        availableQualities_ = std::move(options);
        emit playbackChanged();
    });
}
void MusicController::loadLyrics(const Track& track, quint64 generation) {
    lyricId_ = gateway_.get("/search/lyric", {{"hash", track.hash}}, this, [this, generation](const QJsonObject& response) {
        if (generation != generation_) return;
        const auto candidates = response.value("candidates").toArray();
        if (candidates.isEmpty()) return;
        const auto item = candidates.first().toObject();
        lyricId_ = gateway_.get("/lyric", {{"id", item.value("id")}, {"accesskey", item.value("accesskey")}}, this,
            [this, generation](const QJsonObject& detail) {
            if (generation != generation_) return;
            cues_ = LyricTimeline::parseBilingual(detail.value("lyric").toString());
            updateLyricProjection(); updateLyricIndex();
        });
    });
}
void MusicController::toggle() {
    if (loading_) { desiredPlay_ = !desiredPlay_; if (!desiredPlay_) player_.pause(); emit playbackChanged(); return; }
    if (queueIndex_ < 0) { playFrom("feed", 0); return; }
    if (sourceNeedsReload_ || player_.error() != QMediaPlayer::NoError || player_.mediaStatus() == QMediaPlayer::InvalidMedia) { resolveCurrent(position(), true); return; }
    if (playing()) { desiredPlay_ = false; player_.pause(); } else if (!player_.source().isEmpty()) { desiredPlay_ = true; if (!session_.started()) session_.intend(queue_[queueIndex_]); player_.play(); }
    else resolveCurrent(resumePosition_, true, false, coldResume_);
}
void MusicController::setPlaying(bool desired) {
    if (loading_) { desiredPlay_ = desired; if (!desired) player_.pause(); return; }
    if (desired != playing()) toggle();
}
void MusicController::next() {
    if (radio_) { radioNext(); return; }
    step(1);
}
void MusicController::previous() {
    if (radio_) return;
    step(-1);
}
// playbackCommandCoordinator: list and single-loop wrap around the queue on a
// manual skip; random picks any other track in either direction.
void MusicController::step(int delta) {
    const int count = static_cast<int>(queue_.size());
    if (count == 0) return;
    int index = ((queueIndex_ + delta) % count + count) % count;
    if (loopMode_ == QLatin1String("random") && count > 1) {
        do index = QRandomGenerator::global()->bounded(count); while (index == queueIndex_);
    }
    setQueueIndex(index); resolveCurrent();
}
// usePlayerControls.cycleLoopMode: list → single → random, with its toast.
void MusicController::cycleLoopMode() {
    if (radio_) return;
    loopMode_ = loopMode_ == QLatin1String("list") ? QStringLiteral("single")
        : loopMode_ == QLatin1String("single") ? QStringLiteral("random") : QStringLiteral("list");
    settings_.setValue("loopMode", loopMode_);
    emit loopModeChanged();
    emit notice(loopMode_ == QLatin1String("list") ? QStringLiteral("已切换为 列表顺序播放")
        : loopMode_ == QLatin1String("single") ? QStringLiteral("已切换为 单曲循环") : QStringLiteral("已切换为 随机播放"));
}
bool MusicController::favorite() const {
    if (favoriteRead_) return queueIndex_ >= 0 && favoriteRead_(current());
    return queueIndex_ >= 0 && queueIndex_ < queue_.size() && favorites_.contains(queue_[queueIndex_].hash);
}
// The heart without an account: favoriteStore's anonymous result keeps the
// track on this device (the Qt preview has no login to sync it to).
void MusicController::toggleFavorite() {
    if (queueIndex_ < 0 || queueIndex_ >= queue_.size() || queue_[queueIndex_].hash.isEmpty()) return;
    if (favoriteToggle_) { favoriteToggle_(current()); return; }
    const auto& hash = queue_[queueIndex_].hash;
    const bool on = !favorites_.contains(hash);
    if (on) favorites_.append(hash); else favorites_.removeAll(hash);
    settings_.setValue("favorites", favorites_);
    emit favoriteChanged();
    emit notice(on ? QStringLiteral("已收藏到本地") : QStringLiteral("已取消本地收藏"));
}
void MusicController::setFavoritePolicy(std::function<bool(const QVariantMap&)> read, std::function<void(const QVariantMap&)> toggle) {
    favoriteRead_ = std::move(read); favoriteToggle_ = std::move(toggle); emit favoriteChanged();
}
void MusicController::seek(qint64 position) {
    if (player_.isSeekable()) { session_.seek(); player_.setPosition(std::clamp<qint64>(position, 0, player_.duration())); }
}
void MusicController::startRadio() {
    if (!ready_ || radioPending_) return;
    if (radio_ && queueIndex_ >= 0) { setPlaying(true); return; }
    fetchRadio(true);
}
void MusicController::cancelRadioRequest() {
    ++radioGeneration_; gateway_.cancel(radioId_); radioId_.clear();
    radioAdvancePending_ = false; setRadioPending(false);
}
void MusicController::stopRadio() {
    cancelRadioRequest(); setRadio(false); saveQueue();
}
void MusicController::setAccount(const QString& account) {
    if (account_ == account) return;
    account_ = account; cancelRadioRequest();
    if (radio_) clearQueue();
}
void MusicController::radioNext() {
    if (queueIndex_ + 1 < queue_.size()) {
        setQueueIndex(queueIndex_ + 1); resolveCurrent();
        if (queue_.size() - queueIndex_ - 1 <= 2) fetchRadio(false);
        return;
    }
    fetchRadio(false, true);
}
void MusicController::fetchRadio(bool starting, bool advance) {
    if (radioPending_) { radioAdvancePending_ = radioAdvancePending_ || advance; return; }
    if (!ready_ || (!starting && !radio_)) return;
    const auto owner = ++radioGeneration_;
    radioAdvancePending_ = advance; setRadioPending(true);
    finishListeningInterval();
    const int remain = starting ? 0 : std::max(0, int(queue_.size()) - queueIndex_ - 1);
    QJsonObject query{{"action", "play"}, {"remain_songcnt", remain}, {"is_overplay", remain == 0 ? 1 : 0}};
    if (queueIndex_ >= 0 && queueIndex_ < queue_.size()) {
        const auto& track = queue_[queueIndex_];
        query.insert("hash", track.hash); query.insert("songid", track.audioId);
        query.insert("playtime", static_cast<int>(listenedMs_ / 1000));
    }
    radioId_ = gateway_.get("/personal/fm", query, this, [this, owner, starting](const QJsonObject& response) {
        if (owner != radioGeneration_ || (!starting && !radio_)) return;
        radioId_.clear(); const bool wasPending = std::exchange(radioPending_, false);
        const bool advance = std::exchange(radioAdvancePending_, false);
        auto tracks = parseTracks(response);
        QSet<QString> seen;
        if (!starting) for (const auto& track : std::as_const(queue_)) seen.insert(track.hash.toUpper());
        QVector<Track> fresh;
        for (auto& track : tracks) if (!seen.contains(track.hash.toUpper())) { seen.insert(track.hash.toUpper()); fresh.append(std::move(track)); }
        if (fresh.isEmpty()) { if (wasPending) emit radioPendingChanged(); setMessage(QStringLiteral("暂时没有新的红心推荐，可稍后点击下一首重试")); return; }
        const int previousIndex = queueIndex_;
        if (starting) { const bool previousRadio = radio_; radio_ = true; queue_ = std::move(fresh); queueIndex_ = 0; updateQueueProjection(previousIndex, previousRadio); if (wasPending) emit radioPendingChanged(); resolveCurrent(); return; }
        queue_ += fresh;
        // Keep a bounded recent history without discarding unplayed recommendations.
        if (queueIndex_ > 100) { const int trim = queueIndex_ - 100; queue_.remove(0, trim); queueIndex_ -= trim; }
        updateQueueProjection(previousIndex, radio_); if (wasPending) emit radioPendingChanged(); saveQueue();
        if (advance && queueIndex_ + 1 < queue_.size()) { setQueueIndex(queueIndex_ + 1); resolveCurrent(); }
    });
}
void MusicController::openLocal(const QUrl& file) {
    if (!file.isLocalFile() || !QFileInfo(file.toLocalFile()).isFile()) return;
    cancelRadioRequest(); const bool previousRadio = radio_; radio_ = false;
    Track track; track.hash = file.toString(); track.title = QFileInfo(file.toLocalFile()).completeBaseName();
    const QFileInfo local(file.toLocalFile());
    const QString sidecarBase = local.dir().filePath(local.completeBaseName());
    for (const auto& cover : {sidecarBase + ".jpg", sidecarBase + ".png", local.dir().filePath("cover.jpg")}) {
        if (QFileInfo(cover).isFile()) { track.image = QUrl::fromLocalFile(cover).toString(); break; }
    }
    const int previousIndex = queueIndex_;
    track.artist = QStringLiteral("本地音乐"); queue_ = {track}; queueIndex_ = 0;
    updateQueueProjection(previousIndex, previousRadio); resolveCurrent();
}
void MusicController::playTracks(const QVariantList& values, int index) {
    if (index < 0 || index >= values.size()) return;
    QVector<Track> tracks;
    for (const auto& value : values) {
        const auto row = value.toMap();
        auto track = Track::fromJson(QJsonObject::fromVariantMap(row));
        if (track.hash.isEmpty()) return; // Never silently change the clicked row's index.
        tracks.append(std::move(track));
    }
    cancelRadioRequest(); const bool previousRadio = radio_; radio_ = false; const int previousIndex = queueIndex_;
    queue_ = std::move(tracks); queueIndex_ = index;
    updateQueueProjection(previousIndex, previousRadio); resolveCurrent();
}
bool MusicController::enqueueTrack(const QVariantMap& value) {
    if (radio_ || queue_.size() >= 2000) return false;
    auto track = Track::fromJson(QJsonObject::fromVariantMap(value));
    if (track.hash.isEmpty()) return false;
    for (const auto& queued : queue_) if (queued.hash == track.hash) return true;
    queue_.append(std::move(track));
    // Adding a future song must not reset the current source, clock or session.
    updateQueueProjection(queueIndex_, radio_); saveQueue();
    return true;
}
void MusicController::finishListeningInterval() {
    listenedMs_ = session_.listenedMilliseconds();
}
void MusicController::finalizeSession(bool completed) {
    const auto record = session_.finish(completed, actualQuality_ == "unknown" ? committedQuality_ : actualQuality_);
    if (!record.isEmpty()) emit listenRecorded(record);
}
void MusicController::finishSession() {
    saveQueue(); shuttingDown_ = true; persistTimer_.stop(); resumeDeadline_.stop(); loadDeadline_.stop(); cancelRadioRequest();
    finalizeSession(false); player_.pause();
}
qint64 MusicController::duration() const {
    if (player_.duration() > 0) return player_.duration();
    return queueIndex_ >= 0 && queueIndex_ < queue_.size() ? qint64(queue_[queueIndex_].duration) * 1000 : 0;
}
void MusicController::applyResume() {
    if (!resumePending_ || player_.source().isEmpty()) return;
    const auto status = player_.mediaStatus();
    if (status != QMediaPlayer::LoadedMedia && status != QMediaPlayer::BufferedMedia) return;
    const auto decoded = player_.duration();
    if (resumePosition_ > 0 && decoded <= 0) return;
    qint64 position = resumePosition_;
    if (decoded > 0) {
        // Recheck against decoded metadata, including snapshots whose duration
        // was previously unknown. A finished listen starts from the beginning.
        if (coldResume_ && position >= decoded - 1000) position = 0;
        position = std::clamp<qint64>(position, 0, decoded);
    }
    resumePending_ = false; coldResume_ = false; resumeDeadline_.stop(); session_.seek();
    if (player_.isSeekable() && player_.position() != position) player_.setPosition(position);
    loading_ = false;
    loadDeadline_.stop();
    sourceNeedsReload_ = qualitySwitchPending_ = false;
    committedQuality_ = quality_; settings_.setValue("quality", quality_);
    if (desiredPlay_) player_.play(); else player_.pause();
    emit positionChanged(); emit playbackChanged(); saveQueue();
}
void MusicController::saveQueue() {
    if (shuttingDown_) return;
    const QJsonObject snapshot{{"version", 1}, {"queue", QJsonArray::fromVariantList(queue())},
        {"index", queueIndex_}, {"position", position()}, {"radio", radio_}};
    settings_.setValue("queueSnapshot", QJsonDocument(snapshot).toJson(QJsonDocument::Compact));
    settings_.sync();
}
void MusicController::setQuality(const QString& value) {
    if (!kQualityCatalog.contains(value)) return;
    if (quality_ == value) return;
    quality_ = value;
    if (queueIndex_ < 0 || QUrl(queue_[queueIndex_].hash).isLocalFile()) {
        committedQuality_ = value; settings_.setValue("quality", value); emit playbackChanged(); return;
    }
    if (!ready_) { quality_ = committedQuality_; emit playbackChanged(); return; }
    resolveCurrent(position(), playing() || (loading_ && desiredPlay_), true, false);
}
void MusicController::retry() {
    if (queueIndex_ >= 0) resolveCurrent(position(), true, false, coldResume_);
}
void MusicController::removeQueue(int index) {
    if (radio_ || index < 0 || index >= queue_.size()) return;
    if (queue_.size() == 1) { clearQueue(); return; }
    const bool removedCurrent = index == queueIndex_; const bool autoplay = playing() || (loading_ && desiredPlay_);
    const int previousIndex = queueIndex_;
    queue_.removeAt(index);
    if (index < queueIndex_) --queueIndex_;
    else if (removedCurrent) queueIndex_ = std::min(queueIndex_, int(queue_.size()) - 1);
    updateQueueProjection(previousIndex, radio_);
    if (removedCurrent) resolveCurrent(0, autoplay); else saveQueue();
}
void MusicController::moveQueue(int from, int to) {
    if (radio_ || from < 0 || to < 0 || from >= queue_.size() || to >= queue_.size() || from == to) return;
    const int previousIndex = queueIndex_;
    queue_.move(from, to);
    if (queueIndex_ == from) queueIndex_ = to;
    else if (from < queueIndex_ && to >= queueIndex_) --queueIndex_;
    else if (from > queueIndex_ && to <= queueIndex_) ++queueIndex_;
    updateQueueProjection(previousIndex, radio_); saveQueue();
}
void MusicController::clearQueue() {
    finalizeSession(false); ++generation_; gateway_.cancel(resolveId_); gateway_.cancel(lyricId_);
    gateway_.cancel(privilegeId_); privilegeId_.clear(); availableQualities_.clear(); cancelRadioRequest();
    loading_ = true; player_.stop(); player_.setSource({}); resumeDeadline_.stop(); loadDeadline_.stop();
    const int previousIndex = queueIndex_;
    const bool previousRadio = radio_;
    queue_.clear(); queueIndex_ = -1; radio_ = false; preview_ = false; resumePending_ = false; resumePosition_ = 0;
    cues_.clear(); lyricProjection_.clear(); lyricIndex_ = -1; loading_ = false;
    updateQueueProjection(previousIndex, previousRadio); emit lyricsChanged(); emit lyricIndexChanged(); emit playbackChanged(); emit positionChanged(); saveQueue();
}
void MusicController::updateLyricIndex() {
    const auto index = LyricTimeline::indexAt(cues_, player_.position());
    if (index == lyricIndex_) return;
    lyricIndex_ = index; emit lyricIndexChanged();
}
QString MusicController::timeText(qint64 millis) const {
    const auto seconds = std::max<qint64>(0, millis / 1000);
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}
QString MusicController::qualityText(const QString& quality) const {
    if (quality == "128") return QStringLiteral("标准");
    if (quality == "320") return QStringLiteral("高品质");
    if (quality == "flac") return QStringLiteral("无损");
    if (quality == "high") return QStringLiteral("Hi-Res");
    if (quality == "master") return QStringLiteral("臻品母带");
    if (quality == "local") return QStringLiteral("本地");
    return QStringLiteral("音质待确认");
}
QVariantList MusicController::qualityCatalog() const { return qualityOptions(kQualityCatalog); }
void MusicController::setMessage(QString text) { message_ = std::move(text); emit statusChanged(); }
void MusicController::tracePlayback(const QString& event) const {
    const auto track = current();
    // Never persist media URLs, lyrics, credentials or upstream payloads.
    const QJsonObject row{{"at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}, {"event", event},
        {"hash", track.value("hash").toString()}, {"title", track.value("title").toString()}, {"artist", track.value("artist").toString()},
        {"radio", radio_}, {"index", queueIndex_}, {"position", player_.position()}, {"state", int(player_.playbackState())},
        {"mediaStatus", int(player_.mediaStatus())}, {"error", int(player_.error())}};
    auto fields = row; fields.insert("requestId", resolveId_); fields.insert("generation", qint64(generation_));
    fields.insert("application", QCoreApplication::applicationFilePath());
    fields.insert("version", QCoreApplication::applicationVersion());
    writeDiagnostic(QFileInfo(settings_.fileName()).absolutePath(), "playback", fields);
}
void MusicController::setVolume(double value) {
    value = std::clamp(value, 0.0, 1.0); output_.setVolume(static_cast<float>(value));
    settings_.setValue("volume", value); emit appearanceChanged();
}
void MusicController::setLyricSize(int value) {
    lyricSize_ = std::clamp(value, 20, 48); settings_.setValue("lyricSize", lyricSize_); emit appearanceChanged();
}
void MusicController::setVinyl(bool value) { vinyl_ = value; settings_.setValue("vinyl", value); emit appearanceChanged(); }
void MusicController::setReducedMotion(bool value) {
    reducedMotion_ = value; settings_.setValue("reducedMotion", value); emit appearanceChanged();
}
