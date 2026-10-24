#pragma once
#include <QVariant>
#include <QString>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

// Persistence only: MusicController remains the owner of playback state.
// The caller submits value snapshots; a single worker serializes and writes.
class PlaybackStore final {
public:
    struct QueueSnapshot {
        QVariantList tracks;
        int index = -1;
        qint64 position = 0;
        bool radio = false;
    };
    struct Update {
        QVariantMap values;
        std::optional<QueueSnapshot> queue;
        bool empty() const { return values.isEmpty() && !queue; }
    };
    // Injectable storage boundary for deterministic blocked/failing-I/O tests.
    using Sink = std::function<bool(const Update&)>;
    explicit PlaybackStore(QString fileName, Sink sink = {});
    ~PlaybackStore();
    PlaybackStore(const PlaybackStore&) = delete;
    PlaybackStore& operator=(const PlaybackStore&) = delete;
    bool setValue(QString key, QVariant value);
    bool saveQueue(QVariantList tracks, int index, qint64 position, bool radio);
    // Caller-thread barriers, independent of the GUI event loop. A failed
    // batch stays pending; the next update or flush retries with a fresh writer.
    bool flush();
    bool stop(); // Drains and joins; rejects later updates, including after import.
private:
    bool submit(Update update);
    bool writeFile(const Update& update) const;
    void run();
    Sink sink_;
    QString fileName_;
    std::mutex mutex_;
    std::condition_variable available_, completed_;
    Update pending_; // At most one in-flight batch plus one merged pending batch.
    quint64 submitted_ = 0, attempted_ = 0;
    bool accepting_ = true, stopping_ = false, succeeded_ = true;
    std::thread worker_;
};
