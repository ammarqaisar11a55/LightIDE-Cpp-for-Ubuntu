#pragma once

#include <QString>
#include <QStringList>

enum class SourceLanguage { C, Cpp };
enum class BuildConfiguration { Debug, Release };

struct CommandLine
{
    QString program;
    QStringList arguments;

    // The command as a user could paste it into a shell.
    QString display() const;
};

struct BuildOptions
{
    QString cStandard = QStringLiteral("c17");
    QString cxxStandard = QStringLiteral("c++17");
    QString optimization = QStringLiteral("-O0");
    bool debugInfo = true;
    QStringList defines;
    QStringList warningFlags = {QStringLiteral("-Wall"), QStringLiteral("-Wextra")};
    QStringList includeDirs;
    QStringList libraryDirs;
    QStringList libraries;   // Names without -l.
    QStringList extraArgs;
};

struct Toolchain
{
    QString gcc = QStringLiteral("gcc");
    QString gxx = QStringLiteral("g++");
};

namespace CompileCommand {

SourceLanguage languageOf(const QString &sourcePath);

// Compile and link one source file directly into an executable.
CommandLine singleFile(const Toolchain &tools, const BuildOptions &options,
                       const QString &sourcePath, const QString &outputPath);

// Compile one source into an object file, writing a -MMD dependency file next to it.
CommandLine object(const Toolchain &tools, const BuildOptions &options,
                   const QString &sourcePath, const QString &objectPath);

// Link objects into an executable or shared library.
CommandLine link(const Toolchain &tools, const BuildOptions &options, bool hasCpp,
                 const QStringList &objects, const QString &outputPath, bool shared);

// Bundle objects into a static library.
CommandLine archive(const QStringList &objects, const QString &outputPath);

// Where a single-file build places its executable. An empty outputDirectory
// means next to the source; a relative one is resolved against the source.
QString outputPathFor(const QString &sourcePath, const QString &outputDirectory,
                      const QString &customName = QString());

// Splits a user-typed argument string with shell-like quoting.
QStringList splitArguments(const QString &text);

} // namespace CompileCommand
