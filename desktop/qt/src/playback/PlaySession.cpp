#include "PlaySession.h"
#include <QUuid>
#include <QDateTime>
#include <cmath>

void PlaySession::intend(const Track& track) {
    track_ = track; id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    listened_ = 0; previous_ = -1; decodedDuration_ = 0;
    playing_ = false; started_ = false; finalized_ = false;
}
void PlaySession::setPlaying(bool playing, qint64 position) {
    if (finalized_) return;
    playing_ = playing; previous_ = playing ? position : -1;
    started_ = started_ || playing;
}
void PlaySession::position(qint64 value, qint64 decodedDuration) {
    if (!playing_ || finalized_) return;
    if (decodedDuration > 0) decodedDuration_ = decodedDuration;
    const auto delta = value - previous_;
    if (previous_ >= 0 && delta > 0 && delta < 2000) listened_ += delta;
    previous_ = value;
}
void PlaySession::seek() { previous_ = -1; }
QJsonObject PlaySession::finish(bool completed, const QString& quality) {
    if (finalized_) return {};
    finalized_ = true; playing_ = false;
    const auto seconds = qRound64(listened_ / 1000.0);
    if (!started_ || seconds <= 60 || track_.hash.isEmpty()) return {};
    return {{"session_id", id_}, {"song_hash", track_.hash}, {"song_name", track_.title},
        {"singer_name", track_.artist}, {"album_id", track_.albumId}, {"album_name", track_.album},
        {"cover_url", track_.image}, {"duration_seconds", track_.duration > 0 ? track_.duration : decodedDuration_ / 1000.0},
        {"completed", completed}, {"listened_seconds", seconds}, {"quality", quality},
        {"played_at", QDateTime::currentMSecsSinceEpoch()}};
}
