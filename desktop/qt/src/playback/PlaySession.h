#pragma once
#include "music/Track.h"
#include <QJsonObject>

// Pure media-clock state. Seeking and stalls never count as listening.
class PlaySession {
public:
    void intend(const Track& track);
    void setPlaying(bool playing, qint64 position);
    void position(qint64 value, qint64 decodedDuration);
    void seek();
    QJsonObject finish(bool completed, const QString& quality);
    qint64 listenedMilliseconds() const { return listened_; }
    bool started() const { return started_; }
private:
    Track track_;
    QString id_;
    qint64 listened_ = 0, previous_ = -1, decodedDuration_ = 0;
    bool playing_ = false, started_ = false, finalized_ = true;
};
