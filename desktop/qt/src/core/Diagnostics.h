#pragma once
#include <QJsonObject>
#include <QString>

// Callers provide only explicitly allowlisted fields, never raw responses/URLs.
void writeDiagnostic(const QString& directory, const QString& stream, QJsonObject fields);
