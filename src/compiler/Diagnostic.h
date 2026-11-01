#pragma once

#include <QString>

struct Diagnostic
{
    enum class Severity { Note, Warning, Error };

    Severity severity = Severity::Error;
    QString file;     // Empty for messages not tied to a file, such as linker failures.
    int line = 0;     // 1-based, 0 when unknown.
    int column = 0;   // 1-based, 0 when unknown.
    QString message;

    bool hasLocation() const { return !file.isEmpty() && line > 0; }
};
