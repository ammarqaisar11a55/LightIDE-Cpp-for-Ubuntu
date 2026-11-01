#pragma once

#include "compiler/CompileCommand.h"

#include <QMap>
#include <QObject>
#include <QSettings>
#include <QStringList>

struct EditorSettings
{
    int tabWidth = 4;
    bool insertSpaces = true;
    bool autoIndent = true;
    bool autoCloseBrackets = true;
    bool autoComplete = true;
    bool codeFolding = true;
    bool lineNumbers = true;
    bool wordWrap = false;
    bool bracketMatching = true;
};

enum class ThemeKind { Light, Dark };

struct AppearanceSettings
{
    ThemeKind theme = ThemeKind::Dark;
    QString fontFamily;   // Empty selects the system monospace font.
    int fontSize = 11;
    QString editorBackground;              // "#rrggbb"; empty means the theme's default.
    QMap<QString, QString> syntaxColors;   // Token name to "#rrggbb"; see SyntaxHighlighter.
    bool sidebarVisible = true;
    bool bottomPanelVisible = true;
};

struct CompilerSettings
{
    QString gccPath = QStringLiteral("gcc");
    QString gxxPath = QStringLiteral("g++");
    QString gdbPath = QStringLiteral("gdb");
    QString cStandard = QStringLiteral("c17");
    QString cxxStandard = QStringLiteral("c++17");
    QString warningFlags = QStringLiteral("-Wall -Wextra");
    QString debugOptimization = QStringLiteral("-O0");
    QString releaseOptimization = QStringLiteral("-O2");
    QStringList includeDirs;
    QStringList libraryDirs;
    QString libraries;        // Space separated, without -l.
    QString additionalArgs;
    QString outputDirectory;  // Empty places the executable next to the source.
    BuildConfiguration configuration = BuildConfiguration::Debug;

    Toolchain toolchain() const;
    BuildOptions buildOptions(BuildConfiguration config) const;
};

// What a single source file remembers about how it is built and run.
struct FileRunOptions
{
    QString arguments;
    QString workingDirectory;   // Empty uses the executable's directory.
    QString outputName;         // Empty uses the source's base name.
};

class Settings : public QObject
{
    Q_OBJECT

public:
    // An empty iniPath uses the per-user location; tests pass a temporary file.
    explicit Settings(const QString &iniPath = QString(), QObject *parent = nullptr);

    EditorSettings editor;
    AppearanceSettings appearance;
    CompilerSettings compiler;

    void load();
    void save();

    QStringList recentFiles() const { return m_recentFiles; }
    QStringList recentProjects() const { return m_recentProjects; }
    void addRecentFile(const QString &path);
    void addRecentProject(const QString &path);
    void clearRecent();

    FileRunOptions runOptions(const QString &sourcePath) const;
    void setRunOptions(const QString &sourcePath, const FileRunOptions &options);

    // For window state, which only the UI layer understands.
    QSettings &store() { return *m_store; }

signals:
    void changed();
    void recentChanged();

private:
    QSettings *m_store;
    QStringList m_recentFiles;
    QStringList m_recentProjects;
};
