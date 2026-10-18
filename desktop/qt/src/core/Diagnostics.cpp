#include "Diagnostics.h"
#include "DiagnosticWriter.h"

namespace { DiagnosticWriter& writer() { static DiagnosticWriter instance; return instance; } }
void writeDiagnostic(const QString& directory, const QString& stream, QJsonObject fields) { writer().enqueue(directory, stream, std::move(fields)); }
DiagnosticStats diagnosticStats() { return writer().stats(); }
void flushDiagnostics() { writer().flush(); }
void shutdownDiagnostics() { writer().shutdown(); }
