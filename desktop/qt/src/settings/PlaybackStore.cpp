#include "PlaybackStore.h"
#include "core/Diagnostics.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <utility>

PlaybackStore::PlaybackStore(QString fileName, Sink sink)
    : sink_(std::move(sink)), fileName_(std::move(fileName)) {
    try { worker_ = std::thread([this] { run(); }); }
    catch (...) { accepting_ = false; succeeded_ = false; }
}
PlaybackStore::~PlaybackStore() { stop(); }
bool PlaybackStore::setValue(QString key, QVariant value) {
    Update update; update.values.insert(std::move(key), std::move(value));
    return submit(std::move(update));
}
bool PlaybackStore::saveQueue(QVariantList tracks, int index, qint64 position, bool radio) {
    Update update; update.queue = QueueSnapshot{std::move(tracks), index, position, radio};
    return submit(std::move(update));
}
bool PlaybackStore::submit(Update update) {
    std::lock_guard lock(mutex_);
    if (!accepting_) return false;
    for (auto it = update.values.cbegin(); it != update.values.cend(); ++it)
        pending_.values.insert(it.key(), it.value());
    if (update.queue) pending_.queue = std::move(update.queue);
    ++submitted_; available_.notify_one(); return true;
}
bool PlaybackStore::flush() {
    std::unique_lock lock(mutex_);
    if (!worker_.joinable() || stopping_) return succeeded_;
    if (attempted_ == submitted_ && !pending_.empty()) {
        ++submitted_; available_.notify_one(); // Retry a previously failed batch once.
    }
    const auto target = submitted_;
    completed_.wait(lock, [this, target] { return attempted_ >= target; });
    return succeeded_;
}
bool PlaybackStore::stop() {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) return succeeded_;
        accepting_ = false;
    }
    const bool saved = flush();
    { std::lock_guard lock(mutex_); stopping_ = true; }
    available_.notify_one();
    if (worker_.joinable()) worker_.join();
    return saved;
}
bool PlaybackStore::writeFile(const Update& update) const {
    // QSettings instances are confined to this worker. The GUI-side reader
    // never calls setValue/sync, which could otherwise flush this large file.
    QSettings writer(fileName_, QSettings::IniFormat);
    for (auto it = update.values.cbegin(); it != update.values.cend(); ++it)
        writer.setValue(it.key(), it.value());
    if (update.queue) {
        const auto& queue = *update.queue;
        const QJsonObject snapshot{{"version", 1}, {"queue", QJsonArray::fromVariantList(queue.tracks)},
            {"index", queue.index}, {"position", queue.position}, {"radio", queue.radio}};
        writer.setValue("queueSnapshot", QJsonDocument(snapshot).toJson(QJsonDocument::Compact));
    }
    writer.sync();
    const auto status = writer.status();
    if (status != QSettings::NoError)
        writeDiagnostic(QFileInfo(fileName_).absolutePath(), "playback",
            {{"event", "preferences_save_failed"}, {"status", 0}, {"storageStatus", int(status)}});
    return status == QSettings::NoError;
}
void PlaybackStore::run() {
    for (;;) {
        Update update; quint64 revision;
        {
            std::unique_lock lock(mutex_);
            available_.wait(lock, [this] { return stopping_ || submitted_ > attempted_; });
            if (stopping_) return;
            update = std::exchange(pending_, {}); revision = submitted_;
        }
        bool saved = false;
        try { saved = sink_ ? sink_(update) : writeFile(update); }
        catch (...) { /* Retain the batch and report failure at the next barrier. */ }
        {
            std::lock_guard lock(mutex_);
            if (!saved) {
                // Preserve failed keys, but never overwrite a newer caller value.
                for (auto it = update.values.cbegin(); it != update.values.cend(); ++it)
                    if (!pending_.values.contains(it.key())) pending_.values.insert(it.key(), it.value());
                if (!pending_.queue) pending_.queue = std::move(update.queue);
            }
            succeeded_ = saved; attempted_ = revision;
        }
        completed_.notify_all();
    }
}
