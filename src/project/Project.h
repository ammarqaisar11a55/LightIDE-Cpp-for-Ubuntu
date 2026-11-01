#pragma once

#include "compiler/BuildProcess.h"
#include "compiler/CompileCommand.h"

#include <QString>
#include <QStringList>

enum class TargetType { Executable, StaticLibrary, SharedLibrary };

// A .lideproj file: plain JSON, with paths relative to the file itself.
class Project
{
public:
    static constexpr auto extension = "lideproj";

    QString name;
    QString targetName;
    TargetType targetType = TargetType::Executable;
    QString cStandard = QStringLiteral("c17");
    QString cxxStandard = QStringLiteral("c++17");
    QString debugOptimization = QStringLiteral("-O0");
    QString releaseOptimization = QStringLiteral("-O2");
    QStringList warningFlags = {QStringLiteral("-Wall"), QStringLiteral("-Wextra")};
    QStringList defines;
    QStringList includeDirs;
    QStringList libraryDirs;
    QStringList libraries;
    QString extraArgs;
    QString runArguments;
    QString workingDirectory;   // Relative to the project; empty uses the project directory.
    QStringList sources;
    QStringList headers;
    QStringList folders;        // Kept so that empty folders survive a reload.

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path) { m_filePath = path; }
    QString directory() const;

    bool load(const QString &path, QString *error);
    bool save(QString *error) const;

    QString absolutePath(const QString &relativePath) const;
    QString relativePath(const QString &absolutePath) const;

    bool contains(const QString &absolutePath) const;
    bool addFile(const QString &absolutePath);     // False if already present or unsupported.
    bool removeFile(const QString &absolutePath);

    BuildOptions buildOptions(BuildConfiguration configuration) const;
    QString buildDirectory(BuildConfiguration configuration) const;
    QString outputPath(BuildConfiguration configuration) const;
    QString runWorkingDirectory() const;

    // Works out what needs rebuilding, from timestamps and compiler-written
    // dependency files, and records the flags used so that changing them
    // forces a rebuild.
    BuildPlan makePlan(const Toolchain &tools, BuildConfiguration configuration) const;

    // A template for a new project: one main source in the project directory.
    static Project create(const QString &directory, const QString &name, bool cpp);

    // Prerequisites of a .d file written by -MMD -MP.
    static QStringList parseDependencyFile(const QByteArray &contents);

private:
    QString m_filePath;
};
