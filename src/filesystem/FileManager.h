#pragma once

#include <QString>
#include <QStringList>

enum class EndOfLine { Unix, Windows, ClassicMac };

struct LoadedFile
{
    QString text;
    EndOfLine eol = EndOfLine::Unix;
    bool lossyEncoding = false;   // Not valid UTF-8, so it was read as Latin-1.
};

namespace FileManager {

constexpr qint64 maxFileSize = 64 * 1024 * 1024;

bool load(const QString &path, LoadedFile *file, QString *error);

// Atomic: a crash or a full disk cannot leave a half-written source file.
bool save(const QString &path, const QString &text, QString *error);

EndOfLine detectEol(const QString &text);

bool isSourceFile(const QString &path);
bool isHeaderFile(const QString &path);
bool isSupportedFile(const QString &path);

QStringList supportedPatterns();   // "*.cpp", "*.h", ...
QString openFileFilter();          // For file dialogs.

QString cTemplate();
QString cppTemplate();

} // namespace FileManager
