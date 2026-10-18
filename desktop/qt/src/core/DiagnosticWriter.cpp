#include "DiagnosticWriter.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <algorithm>

namespace {
bool critical(const QJsonObject& fields) {
    const auto error = fields.value("error");
    const auto event = fields.value("event").toString();
    return (error.isDouble() && error.toDouble() != 0) || (error.isString() && !error.toString().isEmpty())
        || error.toBool() || fields.value("http").toInt() >= 400
        || (fields.value("status").isDouble() && fields.value("status").toInt() == 0)
        || event.contains("error", Qt::CaseInsensitive) || event.contains("fail", Qt::CaseInsensitive)
        || event.contains("timeout", Qt::CaseInsensitive);
}
}
DiagnosticWriter::DiagnosticWriter(Sink sink) : sink_(sink ? std::move(sink) : writeFile) {
    try { worker_ = std::thread([this] { run(); }); }
    catch (...) { stopping_ = true; ++stats_.failed; }
}
DiagnosticWriter::~DiagnosticWriter() { shutdown(); }
bool DiagnosticWriter::enqueue(const QString& directory, const QString& stream, QJsonObject fields) {
    const bool important = critical(fields);
    const bool valid = !directory.isEmpty() && directory.size() <= 4096
        && (stream == "playback" || stream == "audio" || stream == "requests");
    QByteArray bytes;
    if (valid) {
        fields.insert("at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        bytes = QJsonDocument(fields).toJson(QJsonDocument::Compact) + '\n';
    }
    std::lock_guard lock(mutex_);
    const auto limit = important ? Capacity : Capacity - ErrorReserve;
    if (!valid || bytes.size() > MaxRecordBytes - MetadataReserve || stopping_ || qsizetype(queue_.size()) >= limit) {
        ++stats_.dropped; if (important) ++stats_.criticalDropped;
        return false;
    }
    const auto sequence = stats_.accepted + 1;
    try { queue_.push_back({directory, stream, std::move(bytes), sequence}); }
    catch (...) { ++stats_.dropped; if (important) ++stats_.criticalDropped; return false; }
    stats_.accepted = sequence;
    stats_.peakQueued = std::max(stats_.peakQueued, qsizetype(queue_.size()));
    available_.notify_one(); return true;
}
DiagnosticStats DiagnosticWriter::stats() const {
    std::lock_guard lock(mutex_); auto result = stats_; result.queued = qsizetype(queue_.size()); return result;
}
void DiagnosticWriter::flush() {
    std::unique_lock lock(mutex_); const auto target = stats_.accepted;
    completed_.wait(lock, [this, target] { return completedSequence_ >= target; });
}
void DiagnosticWriter::shutdown() {
    std::call_once(shutdownOnce_, [this] {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        available_.notify_one(); if (worker_.joinable()) worker_.join();
    });
}
bool DiagnosticWriter::writeFile(const DiagnosticRecord& record) {
    if (!QDir().mkpath(record.directory)) return false;
    const auto path = QDir(record.directory).filePath(record.stream + "-diagnostics.jsonl");
    QFile file(path);
    if (file.exists() && file.size() + record.bytes.size() > MaxFileBytes) {
        const auto previous = path + ".1";
        if (QFile::exists(previous) && !QFile::remove(previous)) return false;
        if (!file.rename(previous)) return false;
        file.setFileName(path);
    }
    if (!file.open(QIODevice::ReadWrite | QIODevice::Append)) return false;
    if (file.size() > 0) {
        file.seek(file.size() - 1);
        if (file.read(1) != "\n") {
            // A prior short write or interrupted process can leave a torn
            // tail. Preserve complete lines before appending another record.
            file.seek(0); const auto existing = file.readAll();
            if (!file.resize(existing.lastIndexOf('\n') + 1)) return false;
        }
    }
    const auto originalSize = file.size();
    if (file.write(record.bytes) == record.bytes.size() && file.flush()) return true;
    file.resize(originalSize); // A failed rollback is repaired on the next attempt.
    return false;
}
void DiagnosticWriter::run() {
    for (;;) {
        DiagnosticRecord record; quint64 dropped, failed, criticalDropped;
        {
            std::unique_lock lock(mutex_);
            available_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) return;
            record = std::move(queue_.front()); queue_.pop_front();
            dropped = stats_.dropped; failed = stats_.failed; criticalDropped = stats_.criticalDropped;
        }
        bool written = false;
        try {
            // Loss and write failures become visible in the next successful
            // record as well as stats(); no raw payload or URL is added.
            if (dropped != reportedDropped_ || failed != reportedFailed_) {
                auto fields = QJsonDocument::fromJson(record.bytes).object();
                fields.insert("diagnosticDropped", qint64(dropped));
                fields.insert("diagnosticCriticalDropped", qint64(criticalDropped));
                fields.insert("diagnosticWriteFailures", qint64(failed));
                record.bytes = QJsonDocument(fields).toJson(QJsonDocument::Compact) + '\n';
            }
            written = sink_(record);
        } catch (...) { /* An optional diagnostic must not terminate playback. */ }
        {
            std::lock_guard lock(mutex_);
            if (written) { ++stats_.written; reportedDropped_ = dropped; reportedFailed_ = failed; }
            else ++stats_.failed;
            completedSequence_ = record.sequence;
        }
        completed_.notify_all();
    }
}
