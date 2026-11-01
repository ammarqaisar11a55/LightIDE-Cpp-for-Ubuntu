#include "core/Settings.h"

#include <QCryptographicHash>
#include <QDir>

namespace {

constexpr int maxRecentEntries = 10;

void pushRecent(QStringList &list, const QString &path)
{
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > maxRecentEntries)
        list.removeLast();
}

QString runOptionsGroup(const QString &sourcePath)
{
    const QByteArray hash =
        QCryptographicHash::hash(sourcePath.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("runOptions/") + QString::fromLatin1(hash);
}

} // namespace

Toolchain CompilerSettings::toolchain() const
{
    return Toolchain{gccPath, gxxPath};
}

BuildOptions CompilerSettings::buildOptions(BuildConfiguration config) const
{
    BuildOptions options;
    options.cStandard = cStandard;
    options.cxxStandard = cxxStandard;
    options.warningFlags = CompileCommand::splitArguments(warningFlags);
    options.includeDirs = includeDirs;
    options.libraryDirs = libraryDirs;
    options.libraries = CompileCommand::splitArguments(libraries);
    options.extraArgs = CompileCommand::splitArguments(additionalArgs);
    if (config == BuildConfiguration::Debug) {
        options.optimization = debugOptimization;
        options.debugInfo = true;
    } else {
        options.optimization = releaseOptimization;
        options.debugInfo = false;
        options.defines << QStringLiteral("NDEBUG");
    }
    return options;
}

QString LeetCodeSettings::directory() const
{
    return problemsDirectory.isEmpty() ? QDir::home().filePath(QStringLiteral("leetcode"))
                                       : problemsDirectory;
}

Settings::Settings(const QString &iniPath, QObject *parent)
    : QObject(parent)
    , m_store(iniPath.isEmpty()
                  ? new QSettings(QSettings::IniFormat, QSettings::UserScope,
                                  QStringLiteral("LightIDE"), QStringLiteral("LightIDE"), this)
                  : new QSettings(iniPath, QSettings::IniFormat, this))
{
}

void Settings::load()
{
    QSettings &s = *m_store;
    const EditorSettings editorDefaults;
    s.beginGroup(QStringLiteral("editor"));
    editor.tabWidth = s.value("tabWidth", editorDefaults.tabWidth).toInt();
    editor.insertSpaces = s.value("insertSpaces", editorDefaults.insertSpaces).toBool();
    editor.autoIndent = s.value("autoIndent", editorDefaults.autoIndent).toBool();
    editor.autoCloseBrackets =
        s.value("autoCloseBrackets", editorDefaults.autoCloseBrackets).toBool();
    editor.autoComplete = s.value("autoComplete", editorDefaults.autoComplete).toBool();
    editor.codeFolding = s.value("codeFolding", editorDefaults.codeFolding).toBool();
    editor.lineNumbers = s.value("lineNumbers", editorDefaults.lineNumbers).toBool();
    editor.wordWrap = s.value("wordWrap", editorDefaults.wordWrap).toBool();
    editor.bracketMatching = s.value("bracketMatching", editorDefaults.bracketMatching).toBool();
    s.endGroup();

    const AppearanceSettings appearanceDefaults;
    s.beginGroup(QStringLiteral("appearance"));
    appearance.theme = s.value("theme", QStringLiteral("dark")).toString() == QLatin1String("light")
                           ? ThemeKind::Light
                           : ThemeKind::Dark;
    appearance.fontFamily = s.value("fontFamily").toString();
    appearance.fontSize = s.value("fontSize", appearanceDefaults.fontSize).toInt();
    appearance.editorBackground = s.value("editorBackground").toString();
    appearance.sidebarVisible = s.value("sidebarVisible", true).toBool();
    appearance.bottomPanelVisible = s.value("bottomPanelVisible", true).toBool();
    appearance.syntaxColors.clear();
    s.beginGroup(QStringLiteral("syntax"));
    const QStringList tokens = s.childKeys();
    for (const QString &token : tokens)
        appearance.syntaxColors.insert(token, s.value(token).toString());
    s.endGroup();
    s.endGroup();

    const CompilerSettings compilerDefaults;
    s.beginGroup(QStringLiteral("compiler"));
    compiler.gccPath = s.value("gccPath", compilerDefaults.gccPath).toString();
    compiler.gxxPath = s.value("gxxPath", compilerDefaults.gxxPath).toString();
    compiler.gdbPath = s.value("gdbPath", compilerDefaults.gdbPath).toString();
    compiler.cStandard = s.value("cStandard", compilerDefaults.cStandard).toString();
    compiler.cxxStandard = s.value("cxxStandard", compilerDefaults.cxxStandard).toString();
    compiler.warningFlags = s.value("warningFlags", compilerDefaults.warningFlags).toString();
    compiler.debugOptimization =
        s.value("debugOptimization", compilerDefaults.debugOptimization).toString();
    compiler.releaseOptimization =
        s.value("releaseOptimization", compilerDefaults.releaseOptimization).toString();
    compiler.includeDirs = s.value("includeDirs").toStringList();
    compiler.libraryDirs = s.value("libraryDirs").toStringList();
    compiler.libraries = s.value("libraries").toString();
    compiler.additionalArgs = s.value("additionalArgs").toString();
    compiler.outputDirectory = s.value("outputDirectory").toString();
    compiler.configuration = s.value("configuration").toString() == QLatin1String("release")
                                 ? BuildConfiguration::Release
                                 : BuildConfiguration::Debug;
    s.endGroup();

    const LeetCodeSettings leetcodeDefaults;
    s.beginGroup(QStringLiteral("leetcode"));
    leetcode.problemsDirectory = s.value("problemsDirectory").toString();
    leetcode.standard = s.value("standard", leetcodeDefaults.standard).toString();
    leetcode.flags = s.value("flags", leetcodeDefaults.flags).toString();
    leetcode.sanitizers = s.value("sanitizers", leetcodeDefaults.sanitizers).toBool();
    leetcode.timeoutMs = s.value("timeoutMs", leetcodeDefaults.timeoutMs).toInt();
    s.endGroup();

    m_recentFiles = s.value("recent/files").toStringList();
    m_recentProjects = s.value("recent/projects").toStringList();
    m_recentFolders = s.value("recent/folders").toStringList();
}

void Settings::save()
{
    QSettings &s = *m_store;
    s.beginGroup(QStringLiteral("editor"));
    s.setValue("tabWidth", editor.tabWidth);
    s.setValue("insertSpaces", editor.insertSpaces);
    s.setValue("autoIndent", editor.autoIndent);
    s.setValue("autoCloseBrackets", editor.autoCloseBrackets);
    s.setValue("autoComplete", editor.autoComplete);
    s.setValue("codeFolding", editor.codeFolding);
    s.setValue("lineNumbers", editor.lineNumbers);
    s.setValue("wordWrap", editor.wordWrap);
    s.setValue("bracketMatching", editor.bracketMatching);
    s.endGroup();

    s.beginGroup(QStringLiteral("appearance"));
    s.setValue("theme", appearance.theme == ThemeKind::Light ? QStringLiteral("light")
                                                             : QStringLiteral("dark"));
    s.setValue("fontFamily", appearance.fontFamily);
    s.setValue("fontSize", appearance.fontSize);
    s.setValue("editorBackground", appearance.editorBackground);
    s.setValue("sidebarVisible", appearance.sidebarVisible);
    s.setValue("bottomPanelVisible", appearance.bottomPanelVisible);
    s.remove(QStringLiteral("syntax"));
    s.beginGroup(QStringLiteral("syntax"));
    for (auto it = appearance.syntaxColors.cbegin(); it != appearance.syntaxColors.cend(); ++it)
        s.setValue(it.key(), it.value());
    s.endGroup();
    s.endGroup();

    s.beginGroup(QStringLiteral("compiler"));
    s.setValue("gccPath", compiler.gccPath);
    s.setValue("gxxPath", compiler.gxxPath);
    s.setValue("gdbPath", compiler.gdbPath);
    s.setValue("cStandard", compiler.cStandard);
    s.setValue("cxxStandard", compiler.cxxStandard);
    s.setValue("warningFlags", compiler.warningFlags);
    s.setValue("debugOptimization", compiler.debugOptimization);
    s.setValue("releaseOptimization", compiler.releaseOptimization);
    s.setValue("includeDirs", compiler.includeDirs);
    s.setValue("libraryDirs", compiler.libraryDirs);
    s.setValue("libraries", compiler.libraries);
    s.setValue("additionalArgs", compiler.additionalArgs);
    s.setValue("outputDirectory", compiler.outputDirectory);
    s.setValue("configuration", compiler.configuration == BuildConfiguration::Release
                                    ? QStringLiteral("release")
                                    : QStringLiteral("debug"));
    s.endGroup();

    s.beginGroup(QStringLiteral("leetcode"));
    s.setValue("problemsDirectory", leetcode.problemsDirectory);
    s.setValue("standard", leetcode.standard);
    s.setValue("flags", leetcode.flags);
    s.setValue("sanitizers", leetcode.sanitizers);
    s.setValue("timeoutMs", leetcode.timeoutMs);
    s.endGroup();

    s.setValue("recent/files", m_recentFiles);
    s.setValue("recent/projects", m_recentProjects);
    s.setValue("recent/folders", m_recentFolders);
    s.sync();
}

void Settings::addRecentFile(const QString &path)
{
    pushRecent(m_recentFiles, path);
    m_store->setValue("recent/files", m_recentFiles);
    emit recentChanged();
}

void Settings::addRecentProject(const QString &path)
{
    pushRecent(m_recentProjects, path);
    m_store->setValue("recent/projects", m_recentProjects);
    emit recentChanged();
}

void Settings::addRecentFolder(const QString &path)
{
    pushRecent(m_recentFolders, path);
    m_store->setValue("recent/folders", m_recentFolders);
    emit recentChanged();
}

void Settings::clearRecent()
{
    m_recentFiles.clear();
    m_recentProjects.clear();
    m_recentFolders.clear();
    m_store->remove(QStringLiteral("recent"));
    emit recentChanged();
}

FileRunOptions Settings::runOptions(const QString &sourcePath) const
{
    QSettings &s = *m_store;
    s.beginGroup(runOptionsGroup(sourcePath));
    FileRunOptions options;
    options.arguments = s.value("arguments").toString();
    options.workingDirectory = s.value("workingDirectory").toString();
    options.outputName = s.value("outputName").toString();
    s.endGroup();
    return options;
}

void Settings::setRunOptions(const QString &sourcePath, const FileRunOptions &options)
{
    QSettings &s = *m_store;
    const QString group = runOptionsGroup(sourcePath);
    if (options.arguments.isEmpty() && options.workingDirectory.isEmpty()
        && options.outputName.isEmpty()) {
        s.remove(group);
        return;
    }
    s.beginGroup(group);
    s.setValue("arguments", options.arguments);
    s.setValue("workingDirectory", options.workingDirectory);
    s.setValue("outputName", options.outputName);
    s.endGroup();
}
