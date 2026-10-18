#pragma once
#include "Diagnostics.h"
#include <QByteArray>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

struct DiagnosticRecord {
    QString directory, stream;
    QByteArray bytes;
    quint64 sequence = 0;
};

// Internal writer; the injectable sink permits deterministic slow-I/O tests.
// The application uses the default file sink through writeDiagnostic().
class DiagnosticWriter final {
public:
    using Sink = std::function<bool(const DiagnosticRecord&)>;
    explicit DiagnosticWriter(Sink sink = {});
    ~DiagnosticWriter();
    bool enqueue(const QString& directory, const QString& stream, QJsonObject fields);
    DiagnosticStats stats() const;
    void flush();
    void shutdown();
    static constexpr qsizetype Capacity = 512;
    static constexpr qsizetype ErrorReserve = 64;
    static constexpr qsizetype MaxRecordBytes = 16 * 1024;
    static constexpr qsizetype MetadataReserve = 256;
    static constexpr qint64 MaxFileBytes = 512 * 1024;
private:
    void run();
    static bool writeFile(const DiagnosticRecord& record);
    Sink sink_;
    mutable std::mutex mutex_;
    std::condition_variable available_, completed_;
    std::deque<DiagnosticRecord> queue_;
    DiagnosticStats stats_;
    quint64 completedSequence_ = 0, reportedDropped_ = 0, reportedFailed_ = 0;
    bool stopping_ = false;
    std::once_flag shutdownOnce_;
    std::thread worker_;
};
