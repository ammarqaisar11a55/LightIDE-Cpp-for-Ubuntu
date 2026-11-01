#pragma once

#include "compiler/Diagnostic.h"

#include <QList>
#include <QString>

#include <optional>

// Turns GCC, G++ and ld output into structured diagnostics. Expects the
// compiler to run with LC_ALL=C and -fmessage-length=0 so that severities are
// untranslated and each diagnostic sits on a single line.
class DiagnosticsParser
{
public:
    static std::optional<Diagnostic> parseLine(const QString &line);
    static QList<Diagnostic> parse(const QString &output);
};
