#pragma once
#include <QJsonObject>
#include <QString>

struct DiagnosticStats {
    quint64 accepted = 0, written = 0, dropped = 0, criticalDropped = 0, failed = 0;
    qsizetype queued = 0, peakQueued = 0;
};

// Callers provide only explicitly allowlisted fields, never raw responses/URLs.
void writeDiagnostic(const QString& directory, const QString& stream, QJsonObject fields);
DiagnosticStats diagnosticStats();
// Explicit barriers for tests and orderly shutdown, never playback callbacks.
void flushDiagnostics();
void shutdownDiagnostics();
